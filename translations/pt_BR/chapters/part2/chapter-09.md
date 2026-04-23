---
title: "Leitura e Escrita em Dispositivos"
description: "Como d_read e d_write movem bytes com segurança entre o espaço do usuário e o kernel por meio de uio e uiomove."
partNumber: 2
partName: "Building Your First Driver"
chapter: 9
lastUpdated: "2026-04-17"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "pt-BR"
---
# Lendo e Escrevendo em Dispositivos

## Orientações ao Leitor e Resultados

O Capítulo 7 ensinou você a colocar um driver de pé. O Capítulo 8 ensinou como esse driver se conecta ao userland por meio de `/dev`. O driver que você terminou no capítulo anterior se conecta como um dispositivo Newbus, cria `/dev/myfirst/0`, mantém um alias em `/dev/myfirst`, aloca estado por abertura, registra mensagens de forma limpa e se desconecta sem vazamentos. Cada uma dessas peças foi importante, mas nenhuma delas moveu um único byte de fato.

Neste capítulo é onde os bytes começam a se mover.

Quando um programa do usuário chama `read(2)` ou `write(2)` em um de seus nós de dispositivo, o kernel precisa transferir dados reais entre o espaço de endereços do usuário e a memória do driver. Essa transferência não é um simples `memcpy`. Ela cruza um limite de confiança. O ponteiro de buffer passado pelo usuário pode ser inválido. Nem todo o buffer pode estar residente na memória. O tamanho pode ser zero, enorme, ou parte de uma lista scatter-gather. O usuário pode estar em uma jail, pode ter um sinal pendente, pode estar lendo com `O_NONBLOCK`, pode ter redirecionado o resultado por um pipe. Seu driver não precisa entender cada um desses casos isoladamente, mas precisa cooperar com a abstração única do kernel que os resolve todos. Essa abstração é `struct uio`, e a ferramenta principal para utilizá-la é `uiomove(9)`.

Neste capítulo é onde finalmente implementamos os pontos de entrada `d_read` e `d_write` que o Capítulo 7 deixou como stubs. Ao longo do caminho, examinaremos com cuidado como o kernel descreve uma requisição de I/O, por que este livro vem dizendo "não toque em ponteiros do usuário diretamente" desde o Capítulo 5, e como estruturar um driver para que transferências parciais, buffers desalinhados, leituras interrompidas por sinal e escritas curtas se comportem da forma que um arquivo UNIX clássico se comportaria.

### Por Que Este Capítulo Merece um Lugar Próprio

Seria tentador escrever um capítulo curto que apenas dissesse "chame `uiomove`" e seguisse em frente. Isso deixaria o leitor com um driver que passa no teste mais simples e depois falha de vinte formas sutis. O motivo deste capítulo ter o tamanho que tem é que I/O é onde drivers de iniciantes mais frequentemente erram, e os lugares onde erram não são aqueles onde o código parece arriscado. Os erros costumam estar nos valores de retorno, no tratamento de `uio_resid`, no tratamento de uma transferência de tamanho zero, no que acontece quando o driver acorda de `msleep(9)` porque o processo foi encerrado, na direção em que uma leitura parcial deve drenar.

Um driver que erra nesses detalhes compila sem erros, passa em um único `cat /dev/myfirst`, e depois produz dados corrompidos quando um programa real começa a passar bytes por ele. Esse é o tipo de bug que consome dias. O objetivo deste capítulo é eliminar essa classe de bug na origem.

### Onde o Capítulo 8 Deixou o Driver

No final do Capítulo 8, seu driver `myfirst` tinha a seguinte forma. Vale a pena fazer um ponto de verificação porque o Capítulo 9 constrói diretamente sobre ele:

- Um único filho Newbus, criado em `device_identify`, registrado sob `nexus0`.
- Um `struct myfirst_softc` alocado pelo Newbus e inicializado em `attach`.
- Um mutex nomeado de acordo com o dispositivo, usado para proteger os contadores do softc.
- Uma árvore sysctl em `dev.myfirst.0.stats` expondo `attach_ticks`, `open_count`, `active_fhs` e `bytes_read`.
- Um cdev primário em `/dev/myfirst/0` com proprietário `root:operator` e modo `0660`.
- Um cdev alias em `/dev/myfirst` apontando para o primário.
- Um `struct myfirst_fh` alocado por `open(2)`, registrado via `devfs_set_cdevpriv(9)`, e liberado por um destrutor que é acionado exatamente uma vez por descritor.
- Handlers stub de `d_read` e `d_write` que recuperam o estado por abertura, opcionalmente o examinam e retornam imediatamente: `d_read` retorna zero bytes (EOF), `d_write` afirma ter consumido todos os bytes definindo `uio_resid = 0`.

O Capítulo 9 pega esses stubs e os torna reais. A estrutura externa do driver não muda muito. Um novo leitor ainda deve ver `/dev/myfirst/0`, ainda ver o alias, ainda ver os sysctls. O que muda é que um `cat /dev/myfirst/0` agora produzirá saída, um `echo hello > /dev/myfirst/0` agora armazenará o texto na memória do driver, e um segundo `cat` lerá de volta exatamente o que a primeira escrita depositou. Ao final do capítulo, seu driver será um buffer em memória pequeno e disciplinado pelo qual você pode empurrar bytes e do qual pode puxá-los. Ainda não será um buffer circular com leituras bloqueantes; esse é o trabalho do Capítulo 10. Será um driver que move bytes corretamente.

### O Que Você Vai Aprender

Após concluir este capítulo, você será capaz de:

- Explicar como `read(2)` e `write(2)` fluem do espaço do usuário, passando pelo devfs, até seus handlers de `cdevsw`.
- Ler e escrever os campos de `struct uio` sem precisar memorizá-los.
- Usar `uiomove(9)` para transferir bytes entre um buffer do kernel e o buffer do chamador em qualquer direção.
- Usar `uiomove_frombuf(9)` quando o buffer do kernel tem tamanho fixo e você quer contabilidade automática de offset.
- Decidir quando usar `copyin(9)` ou `copyout(9)` em vez de `uiomove(9)`.
- Retornar contagens de bytes corretas para transferências curtas, transferências vazias, fim de arquivo e leituras interrompidas.
- Escolher um valor de errno apropriado para cada caminho de erro que uma leitura ou escrita de driver pode percorrer.
- Projetar um buffer interno que o driver preenche via `d_write` e esvazia via `d_read`.
- Identificar e corrigir os bugs mais comuns de `d_read` e `d_write`.
- Exercitar o driver a partir das ferramentas do sistema base (`cat`, `echo`, `dd`, `od`, `hexdump`) e de um pequeno programa em C.

### O Que Você Vai Construir

Você vai levar o driver `myfirst` do final do Capítulo 8 por três estágios incrementais.

1. **Estágio 1, um leitor de mensagem estática.** `d_read` retorna o conteúdo de uma string fixa no espaço do kernel. Cada abertura começa no offset zero e lê ao longo da mensagem. Este é o "hello world" das leituras de dispositivo, mas feito com tratamento correto de offset.
2. **Estágio 2, um buffer de escrita única e leitura múltipla.** O driver possui um buffer de tamanho fixo no kernel. `d_write` acrescenta dados nele. `d_read` retorna o que foi escrito até então, a partir de um offset por descritor que lembra até onde cada leitor consumiu. Dois leitores concorrentes ainda veem seu próprio progresso de forma independente.
3. **Estágio 3, um pequeno driver de eco.** O mesmo buffer, agora usado como um armazenamento FIFO (primeiro a entrar, primeiro a sair). Cada `write(2)` acrescenta bytes ao final. Cada `read(2)` remove bytes do início. Um script de teste com dois processos escreve em um terminal e lê os dados ecoados em outro. Este é o ponto de transição para o Capítulo 10, onde vamos reconstruir o mesmo driver em torno de um buffer circular verdadeiro, adicionar suporte a I/O parcial e não bloqueante, e conectar `poll(2)` e `kqueue(9)`.

Os três estágios compilam, carregam e se comportam de forma previsível. Você vai exercitar cada um deles com `cat`, `echo`, e um pequeno programa do espaço do usuário chamado `rw_myfirst.c` que exercita os casos limite que `cat` não alcança por conta própria.

### O Que Este Capítulo Não Abrange

Vários tópicos se relacionam com `read` e `write`, mas são deliberadamente adiados:

- **Buffers circulares e wrap-around**: O Capítulo 10 implementa um buffer circular verdadeiro. O Estágio 3 aqui usa um buffer linear simples para que possamos manter o foco no próprio caminho de I/O.
- **Leituras bloqueantes e `poll(2)`**: O Capítulo 10 apresenta o bloqueio baseado em `msleep(9)` e o handler `d_poll`. Este capítulo mantém todas as leituras não bloqueantes no nível do driver; um buffer vazio produz uma leitura imediata de zero bytes.
- **`ioctl(2)`**: O Capítulo 25 desenvolve `d_ioctl`. Tocamos nele apenas onde o leitor precisa entender por que certos caminhos de controle pertencem a ele em vez de ao `write`.
- **Registradores de hardware e DMA**: A Parte 4 trata de recursos de barramento, `bus_space(9)` e DMA. A memória que lemos e escrevemos neste capítulo é o heap comum do kernel, alocado com `malloc(9)` a partir de `M_DEVBUF`.
- **Correção de concorrência sob carga**: A Parte 3 é dedicada a condições de corrida, locking e verificação. Tomamos cuidado com proteção de mutex onde uma condição de corrida corromperia o buffer do Estágio 3, mas a discussão mais aprofundada é adiada.

Manter-se dentro dessas linhas é como mantemos o capítulo honesto. Um capítulo para iniciantes que se aventura em `ioctl`, DMA e `kqueue` é um capítulo para iniciantes que não ensina nenhum deles bem.

### Estimativa de Tempo

- **Apenas leitura**: aproximadamente uma hora.
- **Leitura mais digitação dos três estágios**: cerca de três horas, incluindo alguns ciclos de carga e descarga por estágio.
- **Leitura mais todos os laboratórios e desafios**: de cinco a sete horas ao longo de duas ou três sessões.

Comece com um boot de laboratório limpo. Não tenha pressa. Os estágios são pequenos de propósito, e o valor real vem de observar o `dmesg`, observar o `sysctl` e sondar o dispositivo do espaço do usuário após cada mudança.

### Pré-requisitos

Antes de iniciar este capítulo, confirme:

- Você tem um driver `myfirst` funcional equivalente ao código-fonte do estágio 2 do Capítulo 8 em `examples/part-02/ch08-working-with-device-files/stage2-perhandle/`. Se você ainda não chegou ao final do Capítulo 8, pause aqui e volte depois.
- Sua máquina de laboratório está rodando FreeBSD 14.3 com um `/usr/src` correspondente.
- Você já leu a discussão do Capítulo 4 sobre ponteiros, estruturas e layout de memória, e a discussão do Capítulo 5 sobre idiomas do espaço do kernel e segurança.
- Você entende o que é um `struct cdev` e como ele se relaciona com um `cdevsw`. O Capítulo 8 cobriu isso em detalhes.

Se você tiver dúvidas sobre qualquer um desses pontos, o restante do capítulo será mais difícil do que precisa ser. Revise as seções relevantes primeiro.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos trazem retorno imediato.

Primeiro, mantenha `/usr/src/sys/dev/null/null.c` aberto em um segundo terminal. É o exemplo mais curto, mais limpo e mais legível de `d_read` e `d_write` na árvore. Cada ideia que este capítulo apresenta aparece em algum lugar de `null.c` em cinquenta linhas ou menos. Os drivers reais do FreeBSD são o livro-texto; este livro é o guia de leitura.

Segundo, mantenha `/usr/src/sys/sys/uio.h` e `/usr/src/sys/sys/_uio.h` abertos. As declarações lá são curtas e estáveis. Leia-as uma vez agora, de forma que quando o capítulo se referir a `uio_iov`, `uio_iovcnt`, `uio_offset` e `uio_resid`, você não precise confiar apenas no texto.

Terceiro, recompile entre as mudanças e confirme o comportamento a partir do espaço do usuário antes da próxima mudança. Esse é o hábito que separa escrever drivers de escrever prosa sobre drivers. Você vai executar `cat`, `echo`, `dd`, `stat`, `sysctl`, `dmesg` e um programa C curto a cada ponto de verificação. Não os pule. Os modos de falha que este capítulo está ensinando você a reconhecer só ficam visíveis quando você executa o código.

### Mapa do Capítulo

As seções em ordem são:

1. Um mapa visual do caminho completo de I/O, desde `read(2)` no espaço do usuário até `uiomove(9)` dentro do seu handler.
2. Uma revisão rápida do que `read` e `write` significam no UNIX, e o que significam especificamente para quem escreve drivers.
3. A anatomia de `d_read`: sua assinatura, o que se espera que ele faça e o que se espera que ele retorne.
4. A anatomia de `d_write`: o espelho de `d_read`, mais alguns detalhes que se aplicam apenas na direção de escrita.
5. Um protocolo de leitura para handlers desconhecidos, seguido de um segundo percurso detalhado por um driver real (`mem(4)`) para mostrar uma forma diferente.
6. O argumento `ioflag`: de onde ele vem, quais bits importam e por que o Capítulo 9 o ignora em grande parte.
7. Uma análise detalhada de `struct uio`, o objeto de descrição de I/O do kernel, campo a campo, incluindo três instantâneos do mesmo uio ao longo de uma chamada.
8. `uiomove(9)` e suas funções companheiras, as funções que efetivamente movem os bytes.
9. `copyin(9)` e `copyout(9)`: quando recorrer a eles e quando deixá-los de lado em favor de `uiomove`. Mais um estudo de caso cautelar sobre dados estruturados.
10. Buffers internos: estáticos, dinâmicos e de tamanho fixo. Como escolher um, como gerenciá-lo com segurança e os auxiliares do kernel que você deve conhecer.
11. Tratamento de erros: os valores de errno que importam para I/O, como sinalizar fim de arquivo e como pensar em transferências parciais.
12. A implementação em três estágios do `myfirst`, com o código-fonte do driver incluído.
13. Um rastreamento passo a passo de `read(2)` desde o espaço do usuário pelo kernel até o seu handler, mais um rastreamento espelhado de escrita.
14. Um fluxo de trabalho prático para testes: `cat`, `echo`, `dd`, `truss`, `ktrace` e a disciplina que os transforma em um ritmo de desenvolvimento.
15. Observabilidade: sysctl, dmesg e `vmstat -m`, com um instantâneo concreto do driver sob carga leve.
16. Signed, unsigned e os perigos do off-by-one, uma seção curta, mas de alto valor.
17. Notas de troubleshooting para os erros que o material deste capítulo tem maior probabilidade de produzir, e uma tabela comparativa de padrões de handler corretos versus com bugs.
18. Laboratórios práticos (sete) que guiam você por cada estágio e consolidam o fluxo de trabalho de observabilidade.
19. Exercícios desafio (oito) que ampliam os padrões.
20. Encerrando e uma ponte para o Capítulo 10.

Se esta é a sua primeira vez neste capítulo, leia de forma linear e faça os laboratórios conforme os encontrar. Se você está revisitando o material para consolidar o aprendizado, as seções em estilo de referência ao final funcionam de forma independente.

## Um Mapa Visual do Caminho de I/O

Antes de a leitura se aprofundar, uma única imagem merece ser guardada mentalmente. O diagrama abaixo mostra o caminho que uma chamada `read(2)` percorre, desde um programa do usuário até o seu driver e de volta ao chamador. Cada caixa representa um trecho real de código do kernel que você pode encontrar em `/usr/src/sys/`. Cada seta representa uma chamada de função real. Nada aqui é metáfora.

```text
                         user space
      +----------------------------------------------+
      |   user program                               |
      |                                              |
      |     n = read(fd, buf, 1024);                 |
      |            |                                 |
      |            v                                 |
      |     libc read() wrapper                      |
      |     (syscall trap instruction)               |
      +-------------|--------------------------------+
                    |
     ==============| kernel trust boundary |===============
                    |
                    v
      +----------------------------------------------+
      |  sys_read()                                   |
      |  /usr/src/sys/kern/sys_generic.c              |
      |  - lookup fd in file table                    |
      |  - fget(fd) -> struct file *                  |
      |  - build a uio around buf, count              |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  struct file ops -> vn_read                   |
      |  /usr/src/sys/kern/vfs_vnops.c                |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  devfs_read_f()                               |
      |  /usr/src/sys/fs/devfs/devfs_vnops.c          |
      |  - devfs_fp_check -> cdev + cdevsw            |
      |  - acquire thread-count ref                   |
      |  - compose ioflag from f_flag                 |
      |  - call cdevsw->d_read(dev, uio, ioflag)      |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  YOUR HANDLER (myfirst_read)                  |
      |  - devfs_get_cdevpriv(&fh)                    |
      |  - verify is_attached                         |
      |  - call uiomove(9) to transfer bytes          |
      |            |                                  |
      |            v                                  |
      |     +-----------------------------------+     |
      |     |  uiomove_faultflag()              |     |
      |     |  /usr/src/sys/kern/subr_uio.c     |     |
      |     |  - for each iovec entry           |     |
      |     |    copyout(kaddr, uaddr, n)  ===> |====|====> user's buf
      |     |    decrement uio_resid            |     |
      |     |    advance uio_offset             |     |
      |     +-----------------------------------+     |
      |  - return 0 or an errno                       |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  devfs_read_f continues                       |
      |  - release thread-count ref                   |
      |  - update atime if bytes moved                |
      +-------------|--------------------------------+
                    |
                    v
      +----------------------------------------------+
      |  sys_read finalises                           |
      |  - compute count = orig_resid - uio_resid     |
      |  - return to userland                         |
      +-------------|--------------------------------+
                    |
     ==============| kernel trust boundary |===============
                    |
                    v
      +----------------------------------------------+
      |   user program sees the return value         |
      |   in n                                        |
      +----------------------------------------------+
```

Algumas características do diagrama merecem ser fixadas, pois se repetem ao longo do capítulo.

**A fronteira de confiança é cruzada exatamente duas vezes.** Uma vez na descida (o usuário entra no kernel por meio de um trap de syscall) e uma vez na subida (o kernel devolve o controle ao espaço do usuário). Tudo o que acontece entre esses dois momentos é execução exclusiva do kernel. Seu handler executa inteiramente dentro do kernel, em uma stack do kernel, com os registradores do usuário devidamente salvos.

**Seu handler é o único ponto em que o conhecimento do driver entra no caminho.** Tudo acima dele é maquinário do kernel que funciona de forma idêntica para todo dispositivo de caracteres na árvore. Tudo abaixo dele é `uiomove` e `copyout`, também maquinário do kernel. Seu handler é a única função em que a resposta para "quais bytes esta leitura deve produzir?" é calculada.

**O buffer do usuário nunca é tocado diretamente pelo seu driver.** Ele é acessado pelo `copyout`, chamado internamente pelo `uiomove`. Seu driver passa um ponteiro de kernel para `uiomove`, e `uiomove` é o único código que desreferencia o ponteiro do usuário em seu nome. Esta é a forma da fronteira de confiança, expressa em código: a memória do usuário é acessada apenas pela API que sabe como fazê-lo com segurança.

**Cada passo tem um passo correspondente na subida.** A referência de contagem de threads adquirida pelo devfs é liberada após o retorno do seu handler; o estado do uio é inspecionado para calcular a contagem de bytes; o controle é desenrolado por cada camada e retorna ao espaço do usuário. Compreender essa simetria é o que faz a contagem de referências parecer natural em vez de arbitrária.

Imprima esse diagrama ou esboce-o no papel. Quando você ler um driver desconhecido mais adiante no livro, consulte-o novamente. Todo `d_read` ou `d_write` que você estudar estará exatamente nesse ponto da cadeia de chamadas. As diferenças entre drivers estão no handler; o caminho ao redor do handler é constante.

Para o `d_write`, o diagrama é a imagem espelhada. `devfs_write_f` despacha para `cdevsw->d_write`, seu handler chama `uiomove(9)` na direção contrária, `uiomove` chama `copyin` em vez de `copyout`, e o kernel se desenrola de volta para `write(2)`. Cada seta no diagrama tem uma correspondente; todas as propriedades listadas acima também se aplicam a escritas.



## Dispositivos em UNIX: Uma Revisão Rápida

Vale a pena dedicar dez minutos a uma revisão antes de começarmos a escrever código. O Capítulo 6 introduziu o modelo de I/O do UNIX em nível conceitual; o Capítulo 7 o colocou em prática; o Capítulo 8 tornou a interface de arquivos de dispositivo mais organizada. Todos esses tratamentos tinham razões para não se aprofundar no comportamento de `read(2)` e `write(2)` em si, porque os drivers daqueles capítulos não transportavam dados reais. Agora sim transportamos, e uma revisão objetiva prepara o terreno para tudo que vem a seguir.

### O que Torna um Dispositivo Diferente de um Arquivo?

Por fora, eles parecem idênticos. Ambos são abertos com `open(2)`. Ambos são lidos com `read(2)` e escritos com `write(2)`. Ambos são fechados com `close(2)`. Um programa do usuário que funciona com um arquivo comum quase sempre funciona com um arquivo de dispositivo sem alterações no código-fonte, porque a API do espaço do usuário não faz distinção entre eles.

Por dentro, existem diferenças reais, e quem desenvolve drivers precisa internalizá-las.

Um arquivo comum tem um armazenamento de suporte, geralmente bytes em disco gerenciados por um sistema de arquivos. O kernel decide quando fazer leitura antecipada, quando fazer cache, quando fazer flush. Os dados têm uma identidade persistente; dois programas que leem o byte zero de um arquivo veem o mesmo byte. O seeking é barato e ilimitado dentro do tamanho do arquivo.

Um arquivo de dispositivo não tem armazenamento de suporte no sentido de sistema de arquivos. Quando um programa do usuário lê a partir dele, o driver decide quais bytes produzir. Quando um programa do usuário escreve nele, o driver decide o que fazer com eles. A identidade dos dados é o que seu driver define. Dois programas lendo do mesmo dispositivo não necessariamente veem os mesmos bytes; dependendo do driver, podem ver os mesmos bytes, podem ver metades disjuntas de um único stream, ou podem ver streams completamente independentes. O seeking pode ser significativo, sem sentido, ou ativamente proibido.

A consequência prática para seus handlers `d_read` e `d_write` é que **o driver é a definição autoritativa do que `read` e `write` significam** neste dispositivo. O kernel entregará a você uma requisição de I/O; ele não dirá o que fazer com ela. As convenções que os programas UNIX esperam, um stream de bytes, valores de retorno consistentes, códigos de erro honestos, fim de arquivo como zero bytes retornados, são convenções que seu driver precisa honrar intencionalmente. O kernel não as impõe.

### Como o UNIX Trata Dispositivos como Streams de Dados

A palavra "stream" merece ser fixada, pois aparece em toda discussão sobre I/O no UNIX e carrega pelo menos três significados diferentes dependendo do contexto.

Para nossos propósitos, um stream é uma **sequência de bytes entregues em ordem**. Nem o chamador nem o driver conhecem o comprimento total com antecedência. Qualquer um dos lados pode parar a qualquer momento. A sequência pode ter um fim natural (um arquivo que foi completamente lido) ou pode continuar indefinidamente (um terminal, um socket de rede, um sensor). As regras são as mesmas em ambos os casos: o leitor solicita um certo número de bytes, o escritor solicita que um certo número de bytes seja aceito, e o kernel informa quantos bytes realmente foram transferidos.

Um stream não tem efeitos colaterais além da própria transferência de dados. Se o seu driver precisar expor uma superfície de controle, uma forma de alterar configurações, redefinir estado ou negociar parâmetros, essa superfície não pertence ao `read` e ao `write`. A interface para controle é `ioctl(2)`, abordada no Capítulo 25. Não introduza comandos de controle sorrateiramente no stream de dados. Isso torna seu driver mais difícil de usar, de testar e de evoluir.

Um stream é unidirecional por chamada. `read(2)` move bytes do driver para o usuário. `write(2)` move bytes do usuário para o driver. Uma única chamada de sistema nunca faz os dois. Se você precisar de comportamento duplex, por exemplo um padrão de requisição-resposta, implemente-o como uma escrita seguida de uma leitura, com a coordenação que seu driver exigir internamente.

### Acesso Sequencial vs. Acesso Aleatório

A maioria dos drivers produz streams sequenciais: os bytes saem na ordem em que chegam, e `lseek(2)` ou não faz nada interessante ou é recusado. Um terminal, uma porta serial, um dispositivo de captura de pacotes, um stream de log, todos esses são sequenciais.

Alguns drivers são de acesso aleatório: o chamador pode endereçar qualquer byte por meio de `lseek(2)`, e o mesmo offset sempre lê os mesmos dados. Um driver de disco em memória, `/dev/mem`, e alguns outros se encaixam nesse modelo. Eles se parecem mais com arquivos comuns do que com dispositivos na maioria dos aspectos.

Quem desenvolve um driver escolhe onde no espectro o driver se posiciona. O seu driver `myfirst` ficará no extremo sequencial durante a maior parte deste capítulo, com uma nuance: cada descritor aberto carrega seu próprio offset de leitura, de modo que dois processos lendo concorrentemente começam de pontos diferentes no stream. Esse é o compromisso que a maioria dos pequenos dispositivos de caracteres usa. Ele dá a cada leitor uma visão consistente do que consumiu, sem impor um contrato de acesso aleatório verdadeiro ao driver.

A escolha aparece em dois lugares no código:

- **Seu `d_read` atualiza `uio->uio_offset`** (o que `uiomove(9)` faz por você) se e somente se o offset for relevante para você. Para um dispositivo verdadeiramente sequencial em que o offset não tem significado, o valor é ignorado.
- **Seu driver honra ou ignora o `uio->uio_offset` recebido** no início de cada leitura. Drivers sequenciais o ignoram e servem a partir de onde estiverem. Drivers de acesso aleatório o tratam como um endereço em um espaço linear.

Para o `myfirst` de três estágios, trataremos `uio->uio_offset` como um instantâneo por chamada de onde este descritor está no stream e atualizaremos nossos contadores internos de acordo.

### O Papel de read() e write() em Drivers de Dispositivo

Dentro do kernel, `read(2)` e `write(2)` em um arquivo de dispositivo eventualmente chamam seus ponteiros de função `cdevsw->d_read` e `cdevsw->d_write`. Tudo entre a chamada de sistema e a sua função é maquinário do devfs e do VFS; tudo após o retorno da sua função é o kernel entregando o resultado de volta ao userland. Seu handler é o único lugar onde a resposta específica do driver para "o que acontece nesta chamada?" é calculada.

O trabalho do handler não é complicado em termos abstratos:

1. Examine a requisição. Quantos bytes estão sendo solicitados ou entregues a você?
2. Mova os bytes. Use `uiomove(9)` para transferir dados entre o seu buffer do kernel e o do usuário.
3. Retorne um resultado. Zero para sucesso (com `uio_resid` atualizado adequadamente) ou um valor de errno para falha.

O que torna o handler não trivial é que o passo 2 é a fronteira de confiança entre a memória do usuário e a memória do kernel, e toda interação com a memória do usuário precisa ser segura contra programas do usuário mal-comportados ou maliciosos. É por isso que `uiomove(9)` existe. Você não escreve a lógica de segurança; o kernel a escreve, desde que você solicite pela API correta.

### Dispositivos de Caracteres vs. Dispositivos de Blocos Revisitados

O Capítulo 8 observou que o FreeBSD não disponibiliza nós de dispositivo especiais de bloco para o userland há muitos anos. Drivers de armazenamento vivem no GEOM e se publicam como dispositivos de caracteres. Para os fins deste capítulo, o dispositivo de caracteres é a única forma que nos interessa.

A consequência prática é que tudo neste capítulo se aplica a todo driver que você provavelmente escreverá nas Partes 2 a 4. `d_read` e `d_write` são os pontos de entrada. `struct uio` é o transportador. `uiomove(9)` é o executor da transferência. Quando chegarmos à Parte 6 e examinarmos os drivers de armazenamento baseados em GEOM, o caminho de dados deles terá uma aparência diferente, mas ainda será construído sobre os mesmos primitivos que estamos estudando agora.

### Exercício: Classificando Dispositivos Reais no Seu Sistema FreeBSD

Antes de o restante do capítulo mergulhar no código, reserve cinco minutos na sua máquina de laboratório. Abra um terminal e percorra o `/dev`:

```sh
% ls /dev
% ls -l /dev/null /dev/zero /dev/random /dev/urandom /dev/console
```

Para cada nó que você ver, faça a si mesmo três perguntas:

1. É de acesso sequencial ou aleatório?
2. Se eu executar `cat` nele, ele deve produzir algum byte? Quais bytes?
3. Se eu executar `echo something >` nele, algo deve ser visível? Onde?

Experimente alguns:

```sh
% head -c 16 /dev/zero | od -An -tx1
% head -c 16 /dev/random | od -An -tx1
% echo "hello" > /dev/null
% echo $?
```

Observe que `/dev/zero` é inesgotável, `/dev/random` entrega bytes imprevisíveis, `/dev/null` engole escritas silenciosamente e retorna sucesso, e nenhum desses três suporta seeking de forma útil. Esses comportamentos não são acidentes. Eles são os handlers `d_read` e `d_write` desses drivers, fazendo exatamente o que estamos prestes a estudar.

Se você abrir `/usr/src/sys/dev/null/null.c` e olhar para `null_write`, verá a implementação de uma linha: `uio->uio_resid = 0; return 0;`. Esse é um handler `write` totalmente funcional. O driver anunciou "consumi todos os bytes; sem erro". Essa é a menor implementação significativa de escrita no FreeBSD, e ao final deste capítulo você será capaz de escrevê-la, e muitas outras maiores, sem hesitação.



## A Anatomia de `d_read()`

O caminho de leitura do seu driver começa no momento em que devfs despacha uma chamada para `cdevsw->d_read`. A assinatura é fixa, declarada em `/usr/src/sys/sys/conf.h`:

```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
```

Toda função `d_read` na árvore do FreeBSD tem exatamente esta forma. Os três argumentos são a descrição completa da chamada:

- `dev` é o `struct cdev *` que representa o nó de dispositivo que foi aberto. Em um driver que gerencia mais de um cdev por instância, ele indica sobre qual deles a chamada está sendo realizada. Em `myfirst`, em que o dispositivo primário e seu alias despacham pelo mesmo handler, ambos resolvem para o mesmo softc subjacente via `dev->si_drv1`.
- `uio` é o `struct uio *` que descreve a requisição de I/O: quais buffers o usuário forneceu, qual é o tamanho deles, em que ponto do stream a leitura deve começar e quantos bytes ainda precisam ser transferidos. Vamos dissecar o uio na próxima seção.
- `ioflag` é uma máscara de bits de flags definida em `/usr/src/sys/sys/vnode.h`. O que importa para I/O não bloqueante é `IO_NDELAY`, que é definido quando o usuário abriu o descritor com `O_NONBLOCK` (ou passou `O_NONBLOCK` posteriormente via `fcntl(F_SETFL, ...)`). Existem alguns outros flags relacionados a I/O de sistema de arquivos baseado em vnode, mas para drivers de dispositivos de caracteres você normalmente vai inspecionar apenas `IO_NDELAY`.

O valor de retorno é um inteiro no estilo errno: zero em caso de sucesso, um código errno positivo em caso de falha. Ele **não** é uma contagem de bytes. O kernel calcula a contagem de bytes observando o quanto `uio_resid` diminuiu durante a chamada e reporta esse valor ao espaço do usuário como valor de retorno de `read(2)`. Essa inversão é uma das duas ou três coisas mais importantes a internalizar deste capítulo. `d_read` retorna um código de erro; o número de bytes transferidos está implícito no uio.

### O Que `d_read` Deve Fazer

Resumindo em uma única frase, o trabalho é: **produzir até `uio->uio_resid` bytes a partir do dispositivo, entregá-los via `uiomove(9)` ao buffer que o `uio` descreve e retornar zero**.

Alguns corolários decorrem dessa frase e vale a pena torná-los explícitos.

A função pode produzir menos bytes do que foram solicitados. Leituras parciais são legítimas e esperadas. Um programa de usuário que pediu 4096 bytes e recebeu 17 não trata isso como erro; trata como "o driver tinha 17 bytes para fornecer neste momento". O número é visível para o chamador porque `uiomove(9)` decrementou `uio_resid` em 17 ao mover os bytes.

A função pode produzir zero bytes. Uma leitura de zero bytes é a forma como o UNIX sinaliza fim de arquivo. Se o seu driver não tem mais dados para fornecer e não vai ter, retorne zero e deixe `uio_resid` intocado. O chamador recebe um `read(2)` de zero bytes e sabe que o stream terminou.

A função não deve produzir mais bytes do que foram solicitados. `uiomove(9)` garante isso por você; ele não moverá mais do que `MIN(uio_resid, n)` bytes em uma única chamada. Se você chamar `uiomove` repetidamente dentro de um único `d_read`, certifique-se de que o seu loop também respeite `uio_resid`.

A função deve retornar um errno em caso de falha. Em caso de sucesso, o valor de retorno é zero. Valores não nulos são interpretados pelo kernel como erros; o kernel os propaga para o espaço do usuário via `errno`. Os valores mais comuns são `ENXIO`, `EFAULT`, `EIO`, `EINTR` e `EAGAIN`. Percorreremos cada um na seção de tratamento de erros.

A função pode dormir. `d_read` é executada em um contexto de processo (o contexto do chamador), portanto `msleep(9)` e funções similares são permitidas. É assim que os drivers implementam leituras bloqueantes que aguardam dados. Não usaremos `msleep(9)` neste capítulo (o Capítulo 10 o introduz formalmente), mas vale saber que você tem o direito de bloquear.

### O Que `d_read` **Não** Deve Fazer

Uma breve lista de coisas pelas quais o handler explicitamente não é responsável, porque o kernel ou o devfs cuida delas:

- **Localizar a memória do usuário**. O `uio` já descreve o buffer de destino. O seu handler não precisa consultar tabelas de páginas nem validar endereços.
- **Verificar permissões**. As credenciais do usuário foram verificadas pelo `open(2)`; no momento em que `d_read` é executada, o chamador já tem permissão para ler desse descritor.
- **Contar bytes para o chamador**. O kernel calcula a contagem de bytes a partir de `uio_resid`. Você nunca retorna uma contagem de bytes.
- **Aplicar o limite de tamanho global**. O kernel já limitou `uio_resid` a um valor que o sistema consegue lidar.

Cada um desses pontos é uma tentação em algum momento. Resista a todos. Cada um é um lugar onde um handler pode introduzir um bug sutil que um uso correto de `uiomove` evita por construção.

### Um Primeiro `d_read` Real

Aqui está o menor `d_read` útil na árvore do FreeBSD. É a função `zero_read` de `/usr/src/sys/dev/null/null.c`, e é assim que `/dev/zero` produz um stream infinito de bytes nulos:

```c
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        void *zbuf;
        ssize_t len;
        int error = 0;

        zbuf = __DECONST(void *, zero_region);
        while (uio->uio_resid > 0 && error == 0) {
                len = uio->uio_resid;
                if (len > ZERO_REGION_SIZE)
                        len = ZERO_REGION_SIZE;
                error = uiomove(zbuf, len, uio);
        }
        return (error);
}
```

Pause um momento nesse código. O corpo do loop tem três linhas. A condição de término é dupla: ou `uio_resid` chega a zero (transferimos tudo o que o chamador pediu) ou `uiomove` retorna um erro. Cada iteração move o máximo da região preenchida com zeros que a requisição comporta. A função retorna o último código de erro, que é zero se a transferência foi concluída com sucesso.

O loop é necessário porque a região de zeros é finita: uma única chamada a `uiomove` não consegue mover uma quantidade arbitrária de bytes dela, então o loop divide a transferência em partes. Para um driver cujos dados de origem cabem em um único buffer do kernel de tamanho modesto, o loop se reduz a uma única chamada. O Estágio 1 de `myfirst` terá exatamente essa forma.

Observe também o que a função **não** faz. Ela não examina `uio_offset`. Não se importa com onde no stream imaginário a leitura está começando; toda leitura de `/dev/zero` produz bytes nulos. Ela não verifica o cdev. Não verifica as flags. Ela faz exatamente um trabalho, e o faz usando uma única API.

Esse é o modelo. O seu `d_read` geralmente terá a forma de alguma variação desse loop.

### Uma Variante: `uiomove_frombuf`

Quando os dados de origem são um buffer do kernel de tamanho fixo e você quer que o driver se comporte como um arquivo respaldado por esse buffer, a função auxiliar `uiomove_frombuf(9)` faz a aritmética de offset por você.

Sua declaração, em `/usr/src/sys/sys/uio.h`:

```c
int uiomove_frombuf(void *buf, int buflen, struct uio *uio);
```

Sua implementação, em `/usr/src/sys/kern/subr_uio.c`, é curta o suficiente para reproduzir aqui:

```c
int
uiomove_frombuf(void *buf, int buflen, struct uio *uio)
{
        size_t offset, n;

        if (uio->uio_offset < 0 || uio->uio_resid < 0 ||
            (offset = uio->uio_offset) != uio->uio_offset)
                return (EINVAL);
        if (buflen <= 0 || offset >= buflen)
                return (0);
        if ((n = buflen - offset) > IOSIZE_MAX)
                return (EINVAL);
        return (uiomove((char *)buf + offset, n, uio));
}
```

Leia com atenção, porque o comportamento é preciso. A função recebe um ponteiro `buf` para um buffer do kernel de tamanho `buflen`, consulta `uio->uio_offset` e:

- Se o offset for negativo ou de alguma forma sem sentido, retorna `EINVAL`.
- Se o offset estiver além do fim do buffer, retorna zero sem mover nenhum byte. Isso é fim de arquivo: o chamador receberá uma leitura de zero bytes.
- Caso contrário, chama `uiomove(9)` com um ponteiro para dentro de `buf` no offset atual e um comprimento igual à cauda restante do buffer.

A função não usa loop; `uiomove` moverá tantos bytes quantos `uio_resid` comportar, e decrementará `uio_resid` de acordo. O driver nunca precisa tocar em `uio_offset` depois da chamada, porque `uiomove` já faz isso.

Se o seu driver expõe um buffer fixo como arquivo legível, um `d_read` de uma linha é suficiente:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        return (uiomove_frombuf(sc->buf, sc->buflen, uio));
}
```

O Estágio 1 deste capítulo usa exatamente esse padrão, com um pequeno ajuste para rastrear o offset de leitura por descritor, de modo que dois leitores concorrentes vejam seu próprio progresso.

### A Assinatura em Uso: myfirst_read Estágio 1

Aqui está como ficará o nosso `d_read` do Estágio 1. Não o digite ainda; percorreremos o código-fonte completo na seção de implementação. Vê-lo aqui e agora serve principalmente para ancorar a discussão.

Antes de ler o código, pause em um detalhe que vai se repetir em quase todos os handlers pelo resto deste capítulo. As primeiras quatro linhas de qualquer handler que conhece o estado por descritor seguem um **padrão de boilerplate** fixo:

```c
struct myfirst_fh *fh;
int error;

error = devfs_get_cdevpriv((void **)&fh);
if (error != 0)
        return (error);
```

Esse padrão recupera o `fh` por descritor que `d_open` registrou via `devfs_set_cdevpriv(9)`, e propaga qualquer falha de volta ao kernel sem alteração. Você o verá no topo de `myfirst_read`, `myfirst_write`, `myfirst_ioctl`, `myfirst_poll` e nos auxiliares de `kqfilter`. Quando um laboratório posterior disser "recupere o estado por abertura com o boilerplate usual de `devfs_get_cdevpriv`", é esse bloco ao qual se refere, e o restante do capítulo não o reexplicará. Se um handler alguma vez reordenar essas linhas, trate isso como um sinal de alerta: executar qualquer lógica antes dessa chamada significa que o handler ainda não sabe qual abertura está atendendo. A sutileza que vale lembrar é que a verificação de atividade `sc == NULL` vem *depois* desse boilerplate, não antes, porque você precisa recuperar o estado por abertura de forma segura mesmo em um dispositivo que está sendo desmontado.

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        before = uio->uio_offset;
        error = uiomove_frombuf(__DECONST(void *, sc->message),
            sc->message_len, uio);
        if (error == 0)
                fh->reads += (uio->uio_offset - before);
        fh->read_off = uio->uio_offset;
        return (error);
}
```

Alguns pontos merecem atenção antes de continuarmos. A função recupera a estrutura por abertura via `devfs_get_cdevpriv(9)`, verifica se o softc está ativo e então delega o trabalho real para `uiomove_frombuf`. Fazemos um snapshot de `uio->uio_offset` em uma variável local `before` na entrada, para que depois da chamada possamos calcular o número de bytes que o kernel acabou de mover como `uio->uio_offset - before`. Esse incremento é registrado no contador por descritor. A atribuição final a `fh->read_off` armazena a posição no stream para que o restante do driver possa reportá-la mais tarde.

Se o driver não tem dados para entregar, `uiomove_frombuf` retorna zero e `uio_resid` permanece inalterado, que é a forma como o fim de arquivo é sinalizado. Se ocorrer um erro dentro de `uiomove`, propagamos esse erro para cima retornando o código de erro. Nada nesse handler precisa de `copyin` ou `copyout` diretamente. A segurança da transferência é gerenciada por `uiomove` em nosso nome.

### Lendo `d_read` na Árvore

Um bom exercício de leitura, após concluir esta seção, é fazer um grep por `d_read` em `/usr/src/sys/dev` e observar o que outros drivers fazem dentro dele. Você encontrará três formas recorrentes:

- **Drivers que leem de um buffer fixo.** Eles usam `uiomove_frombuf(9)` ou um equivalente feito à mão, uma chamada, pronto. `/usr/src/sys/fs/pseudofs/pseudofs_vnops.c` usa o auxiliar extensamente; o padrão é idêntico para dispositivos de caracteres.
- **Drivers que leem de um buffer dinâmico.** Eles adquirem um lock interno, registram quanto dado está disponível, chamam `uiomove(9)` com esse comprimento, liberam o lock e retornam. Construiremos um desses no Estágio 2.
- **Drivers que leem de uma fonte bloqueante.** Eles verificam se há dados disponíveis e, se não houver, ou dormem em uma variável de condição (modo bloqueante) ou retornam `EAGAIN` (modo não bloqueante). Esse é o território do Capítulo 10.

As três formas compartilham a mesma espinha de quatro linhas: recuperar o estado por abertura se você o usa, verificar se está ativo, chamar `uiomove` (ou um parente dele), retornar o código de erro. As diferenças estão em como preparam o buffer, não em como o transferem.



## A Anatomia de `d_write()`

O handler de escrita é a imagem espelhada do handler de leitura, com algumas pequenas diferenças nas bordas. A assinatura, em `/usr/src/sys/sys/conf.h`:

```c
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

A forma é idêntica. Os três argumentos carregam o mesmo significado. O valor de retorno é um errno, zero em caso de sucesso. A contagem de bytes ainda é calculada a partir de `uio_resid`: o kernel observa quanto `uio_resid` diminuiu durante a chamada e reporta isso como o valor de retorno de `write(2)`.

### O Que `d_write` Deve Fazer

Uma frase novamente: **consumir até `uio->uio_resid` bytes do usuário, entregá-los via `uiomove(9)` para onde quer que o seu driver armazene seus dados e retornar zero**.

Os corolários são quase idênticos aos de leituras, com duas diferenças notáveis:

- Uma escrita parcial é legítima, mas incomum. Um driver que aceita menos bytes do que foram oferecidos deve atualizar `uio_resid` para refletir a realidade, e o kernel reportará a contagem parcial ao espaço do usuário. A maioria dos programas de usuário bem comportados fará um loop e tentará novamente o restante; muitos não farão. A regra geral é: aceite tudo o que puder e, se não puder aceitar mais, retorne `EAGAIN` para chamadores não bloqueantes e (eventualmente) durma para chamadores bloqueantes.
- Uma escrita de zero bytes não é fim de arquivo. É simplesmente uma escrita que moveu zero bytes. `d_write` não tem o conceito de EOF; apenas as leituras têm. Um driver que queira recusar uma escrita retorna um errno não nulo.

O valor de retorno mais comum no lado do erro é `ENOSPC` (sem espaço no dispositivo) quando o buffer do driver está cheio, `EFAULT` quando ocorre uma falha relacionada a ponteiro dentro de `uiomove`, e `EIO` para erros de hardware genéricos. Um driver que impõe um limite de comprimento por escrita pode retornar `EINVAL` ou `EMSGSIZE` para escritas que excedam o limite; veremos qual escolher mais adiante no capítulo.

### O Que `d_write` **Não** Deve Fazer

A mesma lista de `d_read`: ele não localiza a memória do usuário, não verifica permissões, não conta bytes para o chamador e não aplica limites globais do sistema. O kernel cuida de todos os quatro.

Uma adição específica para escritas: **não assuma que os dados recebidos são terminados em nulo ou de alguma forma estruturados**. Os usuários podem escrever bytes arbitrários. Se o seu driver espera entrada estruturada, ele deve analisá-la de forma defensiva. Se o seu driver espera dados binários, ele deve lidar com escritas que não estejam alinhadas com nenhuma fronteira natural. `write(2)` é um stream de bytes, não uma fila de mensagens. O caminho de `ioctl` do Capítulo 25 é onde pertencem os comandos estruturados e delimitados.

### Um Primeiro `d_write` Real

O `d_write` não trivial mais simples na árvore é `null_write` de `/usr/src/sys/dev/null/null.c`:

```c
static int
null_write(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        uio->uio_resid = 0;

        return (0);
}
```

Duas linhas. O handler diz ao kernel "consumi todos os bytes" ao definir `uio_resid` como zero e retorna sucesso. O kernel informa ao espaço do usuário o comprimento original da requisição como o número de bytes escritos. `/dev/null` não faz absolutamente nada com os bytes; esse é exatamente o propósito de `/dev/null`. Mas o padrão é instrutivo: **definir `uio_resid = 0` é a forma mais curta de marcar uma escrita como totalmente consumida**, e é exatamente o que `uiomove(9)` teria feito se tivéssemos fornecido um destino.

Um caso um pouco mais interessante é `full_write`, também em `null.c`:

```c
static int
full_write(struct cdev *dev __unused, struct uio *uio __unused, int flags __unused)
{
        return (ENOSPC);
}
```

Essa é a implementação por trás de `/dev/full`, um dispositivo que está cheio para sempre. Toda escrita falha com `ENOSPC`, e quem chamou vê o valor correspondente de `errno`. O handler não toca em `uio_resid`; o kernel percebe que nenhum byte foi movido e reporta um valor de retorno -1 com `errno = ENOSPC`.

Juntos, esses dois handlers ilustram os dois extremos do lado da escrita: aceitar tudo ou rejeitar tudo. Drivers reais ficam em algum ponto intermediário, decidindo quantos dos bytes oferecidos conseguem aceitar e armazenando esses bytes em algum lugar.

### Um Write que Realmente Armazena Dados

Esta é a forma do handler de escrita que implementaremos ao final deste capítulo. Não o digite ainda; isto é apenas uma prévia para orientação.

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        avail = sc->buflen - sc->bufused;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + sc->bufused, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

O handler bloqueia o mutex do softc, verifica quanto espaço de buffer resta, limita a transferência ao que couber, chama `uiomove(9)` com esse comprimento e contabiliza a transferência bem-sucedida avançando `bufused`. Se o buffer estiver cheio, retorna `ENOSPC` para sinalizar o chamador. Tudo o que o handler faz para lidar com concorrência ou escritas parciais está capturado na combinação do lock e do limite.

Observe que `uiomove` é chamado **com o mutex bloqueado**. Isso é permitido, desde que o mutex seja um mutex `MTX_DEF` comum (como o de `myfirst`) e o contexto de chamada seja uma thread de kernel regular que pode dormir. `uiomove` pode causar page faults ao copiar de ou para a memória do usuário, e page faults podem exigir que o kernel durma aguardando uma leitura de disco. Dormir enquanto se mantém um mutex `MTX_DEF` é legal; dormir enquanto se mantém um spinlock (`MTX_SPIN`) seria um bug. A Parte 3 aborda as regras de locking formalmente; por ora, confie no tipo de lock que você escolheu no Capítulo 7.

### Simetria com `d_read`

Leituras e escritas são quase idênticas do ponto de vista do driver. Os dados fluem em direções opostas, e o campo `uio->uio_rw` informa ao `uiomove` qual direção mover os bytes. No lado do driver, você passa os mesmos argumentos: um ponteiro para a memória do kernel, um comprimento e o uio. No lado do usuário, `uiomove` ou copia para fora do buffer do kernel (em uma leitura) ou para dentro dele (em uma escrita). Raramente é necessário pensar na direção; `uio_rw` já está definido.

O que muda entre os dois handlers é a **intenção**. Uma leitura é a oportunidade do driver de produzir dados. Uma escrita é a oportunidade do driver de consumir dados. O seu código em cada handler sabe o papel que está desempenhando e realiza a contabilidade apropriada: um leitor rastreia quanto entregou, um escritor rastreia quanto armazenou.

### Lendo `d_write` na Árvore

Após ler esta seção, dedique alguns minutos ao comando `grep d_write /usr/src/sys/dev | head -20` e observe o que outros drivers fazem. Três formatos aparecem:

- **Drivers que descartam escritas.** Normalmente uma linha: define `uio_resid = 0` e retorna zero. O `null_write` do driver `null` é o protótipo.
- **Drivers que armazenam escritas.** Eles bloqueiam, verificam a capacidade, chamam `uiomove(9)`, contabilizam, desbloqueiam e retornam. Nosso handler do Estágio 3 tem esse formato.
- **Drivers que encaminham escritas ao hardware.** Eles extraem dados do uio, os colocam em um buffer de DMA ou em um anel de propriedade do hardware e disparam o hardware. Esse formato está fora do escopo até a Parte 4; a mecânica do `uiomove` é a mesma, mas o destino é uma região mapeada por DMA em vez de um buffer alocado com `malloc`.

Todo driver real se encaixa em um desses três. Os que acumulam ou reformatam dados primeiro e depois os encaminham para algum lugar tendem a combinar os formatos 2 e 3, mas as primitivas são idênticas.

### Lendo um `d_read` ou `d_write` Desconhecido na Prática

Um capítulo como este é mais útil quando ajuda você a ler o código de outras pessoas, não apenas o seu próprio. Ao explorar a árvore do FreeBSD, você encontrará handlers que não se parecem em nada com `null_write` ou `zero_read`. A estrutura ainda estará lá; a decoração será diferente. Aqui está um pequeno protocolo de leitura que elimina as suposições.

**Passo um: encontre o tipo de retorno e os nomes dos argumentos.** Todo `d_read_t` e `d_write_t` recebe os mesmos três argumentos. Se o handler os renomeou de `dev`, `uio` e `ioflag`, anote o que o autor escolheu (`cdev`, `u`, `flags` são todos comuns). Tenha esses nomes em mente enquanto lê.

**Passo dois: encontre a chamada a `uiomove` (ou equivalente).** Rastreie de trás para frente para entender qual ponteiro do kernel está sendo passado a ela e qual comprimento. Esse par é o núcleo do handler. Tudo antes da chamada a `uiomove` é preparação do ponteiro e do comprimento; tudo depois é contabilidade.

**Passo três: encontre a aquisição e a liberação do lock.** Um handler que adquire um lock antes de `uiomove` e o libera depois está serializando com outros handlers. Um handler sem lock está operando em dados somente de leitura ou usando alguma outra primitiva de sincronização (uma variável de condição, um refcount, um lock de leitura). Identifique qual é.

**Passo quatro: encontre os retornos de errno.** Liste os valores de errno que o handler pode produzir. Se a lista for curta e cada valor tiver um gatilho óbvio, o handler está bem escrito. Se a lista for longa ou opaca, o autor provavelmente deixou pontas soltas.

**Passo cinco: encontre as transições de estado.** Quais contadores o handler incrementa? Quais campos por-handle ele toca? Essas transições são a assinatura comportamental do driver, e geralmente são a parte que mais difere de um driver para outro.

Aplique este protocolo ao `zero_read` em `/usr/src/sys/dev/null/null.c`. Os nomes dos argumentos são os padrão. A chamada a `uiomove` passa o ponteiro do kernel `zbuf` (apontando para `zero_region`) e um comprimento limitado por `ZERO_REGION_SIZE`. Não há lock; os dados são constantes. O único errno que o handler pode retornar é o que `uiomove` retornou. Não há transições de estado; `/dev/zero` é sem estado.

Agora aplique o mesmo protocolo ao `myfirst_write` no Estágio 3. Nomes dos argumentos: padrão. Chamada a `uiomove`: ponteiro do kernel `sc->buf + bufhead + bufused`, comprimento `MIN((size_t)uio->uio_resid, avail)`. Lock: `sc->mtx` adquirido antes e liberado depois. Retornos de errno: `ENXIO` (dispositivo desaparecido), `ENOSPC` (buffer cheio), `EFAULT` via `uiomove`, ou zero. Transições de estado: `sc->bufused += towrite`, `sc->bytes_written += towrite`, `fh->writes += towrite`.

Dois drivers, mesmo protocolo, duas descrições coerentes do que o handler faz. Quando você aplicar esse hábito de leitura umas seis vezes, handlers desconhecidos deixam de parecer desconhecidos.

### O Que Acontece se Você Não Definir `d_read` ou `d_write`?

Um detalhe sobre o qual iniciantes às vezes se perguntam: o que acontece se o seu `cdevsw` não define `.d_read` ou `.d_write`? A resposta curta é que o kernel substitui por um padrão que retorna `ENODEV` ou age como um no-op, dependendo de qual slot está vazio e quais outros `d_flags` estão definidos. A resposta longa vale a pena conhecer, porque drivers reais usam os padrões, intencionalmente, quando querem expressar "este dispositivo não faz leituras" ou "escritas são descartadas silenciosamente".

Veja como `/usr/src/sys/dev/null/null.c` conecta seus três drivers:

```c
static struct cdevsw null_cdevsw = {
        .d_version =    D_VERSION,
        .d_read =       (d_read_t *)nullop,
        .d_write =      null_write,
        .d_ioctl =      null_ioctl,
        .d_name =       "null",
};
```

`.d_read` é definido como o helper do kernel `nullop`, com cast para `d_read_t *`. `nullop` é uma função universal "não faça nada, retorne zero" declarada em `/usr/src/sys/sys/systm.h` e definida em `/usr/src/sys/kern/kern_conf.c`; ela não recebe argumentos e retorna zero. É usada em todo o kernel onde um slot de método precisa de um padrão inofensivo. O cast funciona porque `d_read_t` espera uma função que retorne `int`, e a forma `int (*)(void)` de `nullop` é suficientemente compatível para que o dispatch do cdevsw a chame sem surpresas.

Para `/dev/null`, `(d_read_t *)nullop` significa "toda leitura retorna zero bytes, para sempre". Um usuário que execute `cat /dev/null` verá um EOF imediato. Isso é diferente de `/dev/zero`, que instala `zero_read` para produzir um fluxo infinito de bytes zero. O contraste entre os dois drivers é um contraste entre dois comportamentos de leitura padrão, e ambos consistem em exatamente uma linha no `cdevsw`.

Se você omitir `.d_read` e `.d_write` completamente, o kernel os preenche com padrões que retornam `ENODEV`. Essa é a escolha certa quando o dispositivo genuinamente não suporta transferência de dados; os chamadores recebem um erro claro em vez de um sucesso silencioso. Mas para dispositivos que devem aceitar escritas silenciosamente ou produzir leituras de zero bytes, definir o slot como `(d_read_t *)nullop` é o gesto idiomático no FreeBSD.

**Regra prática:** decida deliberadamente. Ou implemente o handler (para comportamento real), ou defina-o como `(d_read_t *)nullop` / `(d_write_t *)nullop` (para padrões inofensivos), ou deixe-o completamente indefinido (para `ENODEV`). Todo driver real na árvore escolhe uma dessas três opções conscientemente, e a escolha é visível para os usuários.

### Um Segundo Driver Real: Como `mem(4)` Usa Um Único Handler para Ambas as Direções

`null.c` é o exemplo mínimo canônico. Um exemplo ligeiramente mais rico vale a pena examinar antes de seguirmos em frente, porque demonstra um padrão que você encontrará frequentemente na árvore: **um único handler que serve tanto `d_read` quanto `d_write`**, dependendo de `uio->uio_rw` para distinguir as duas direções.

O driver é `mem(4)`, que expõe `/dev/mem` e `/dev/kmem`. As partes comuns ficam em `/usr/src/sys/dev/mem/memdev.c`, e a lógica de leitura e escrita específica de arquitetura fica em `/usr/src/sys/<arch>/<arch>/mem.c`. No amd64, o arquivo é `/usr/src/sys/amd64/amd64/mem.c`, e a função é `memrw`.

Olhe primeiro para o `cdevsw`:

```c
static struct cdevsw mem_cdevsw = {
        .d_version =    D_VERSION,
        .d_flags =      D_MEM,
        .d_open =       memopen,
        .d_read =       memrw,
        .d_write =      memrw,
        .d_ioctl =      memioctl,
        .d_mmap =       memmmap,
        .d_name =       "mem",
};
```

Tanto `.d_read` quanto `.d_write` apontam para a mesma função. Isso é válido porque os typedefs `d_read_t` e `d_write_t` são idênticos (ambos são `int (*)(struct cdev *, struct uio *, int)`), então uma única função pode satisfazer ambos. O truque está em ler `uio->uio_rw` dentro do handler para decidir em qual direção mover os bytes.

Um esboço condensado de `memrw` tem esta aparência:

```c
int
memrw(struct cdev *dev, struct uio *uio, int flags)
{
        struct iovec *iov;
        /* ... locals ... */
        ssize_t orig_resid;
        int error;

        error = 0;
        orig_resid = uio->uio_resid;
        while (uio->uio_resid > 0 && error == 0) {
                iov = uio->uio_iov;
                if (iov->iov_len == 0) {
                        uio->uio_iov++;
                        uio->uio_iovcnt--;
                        continue;
                }
                /* compute a page-bounded chunk size into c */
                /* ... direction-independent mapping logic ... */
                error = uiomove(kernel_pointer, c, uio);
        }
        /*
         * Don't return error if any byte was written.  Read and write
         * can return error only if no i/o was performed.
         */
        if (uio->uio_resid != orig_resid)
                error = 0;
        return (error);
}
```

Há três ideias neste esboço que se generalizam para seus próprios drivers.

**Primeiro, um único handler para ambas as direções economiza código quando o trabalho por byte é idêntico.** A lógica de mapeamento em `memrw` resolve um offset do espaço do usuário para uma parte da memória acessível ao kernel; se você está lendo ou escrevendo nessa memória é decidido depois, por `uiomove` ao verificar `uio->uio_rw`. Você evita a duplicação de um par leitura-escrita quase idêntico ao custo de uma única função que precisa ser clara sobre em qual direção está operando. Se as duas direções não compartilham quase nada, escreva duas funções; se compartilham quase tudo, combine-as.

**Segundo, `memrw` percorre o iovec diretamente.** Ao contrário de `myfirst`, que passa toda a transferência para `uiomove` em uma ou duas chamadas, `memrw` percorre entradas do iovec explicitamente para que possa mapear cada offset solicitado para a memória do kernel e então chamar `uiomove` na região mapeada. Este é o padrão que você usa quando o *ponteiro do kernel* que seu driver passa para `uiomove` depende do offset sendo atendido. É menos comum que o estilo de `myfirst`, mas é a forma correta quando cada trecho da transferência corresponde a uma parte diferente do armazenamento de apoio do driver.

**Terceiro, observe o truque de orig_resid ao final.** O handler salva `uio_resid` na entrada e, após o loop, verifica se alguma coisa foi movida. Se algo foi, retorna zero (sucesso) mesmo que um erro tenha ocorrido depois, porque as convenções UNIX exigem que uma leitura ou escrita com uma contagem de bytes não nula retorne essa contagem ao chamador em vez de falhar a chamada inteira. Este é o idioma do "sucesso parcial": se algum byte foi movido, informe a contagem; só falhe quando nenhum byte for movido.

Seus handlers de `myfirst` não precisam desse idioma, porque chamam `uiomove` exatamente uma vez. Se `uiomove` tem sucesso, tudo foi movido; se falha, nada foi movido (do ponto de vista da contabilidade do driver). O idioma orig_resid importa quando seu handler faz um loop e o loop pode ser interrompido no meio por um erro de `uiomove`. Guarde esse padrão; você o usará em capítulos posteriores quando seu driver servir dados de múltiplas fontes.

### Por Que Este Percurso Valeu o Desvio

Dois drivers. Dois armazenamentos de apoio muito diferentes. Uma primitiva. Em `null.c`, `zero_read` serve uma região zero pré-alocada; em `memrw`, o handler serve memória física mapeada sob demanda. O código parece diferente no meio, porque o meio é onde o conhecimento único do driver reside. As extremidades parecem iguais: ambas as funções recebem um uio, ambas fazem loop em `uio_resid`, ambas chamam `uiomove(9)` para realizar a transferência real, ambas retornam zero em caso de sucesso ou um errno em caso de falha.

Essa uniformidade é exatamente o ponto. Toda leitura e escrita de dispositivo de caracteres na árvore obedece a essa forma. Assim que você a reconhece, pode abrir qualquer driver desconhecido em `/usr/src/sys/dev` e ler o handler com confiança: a parte que você ainda não entende é sempre o meio, nunca as extremidades.

## Entendendo o Argumento `ioflag`

Tanto `d_read` quanto `d_write` recebem um terceiro argumento que o restante do capítulo mal utilizou até agora. Esta seção é a explicação curta, mas útil, sobre o que é `ioflag`, de onde ele vem e quando um driver de dispositivo de caracteres deve de fato consultá-lo.

### De Onde Vem o `ioflag`

Toda vez que um processo realiza um `read(2)` ou `write(2)` em um nó devfs, o kernel compõe um valor `ioflag` a partir dos flags atuais do descritor de arquivo antes de chamar seu handler. Essa composição vive no próprio devfs, em `/usr/src/sys/fs/devfs/devfs_vnops.c`. As linhas relevantes de `devfs_read_f` são:

```c
ioflag = fp->f_flag & (O_NONBLOCK | O_DIRECT);
if (ioflag & O_DIRECT)
        ioflag |= IO_DIRECT;
```

O padrão em `devfs_write_f` é a imagem espelhada. O kernel pega os bits da palavra `f_flag` da tabela de arquivos que são interessantes para I/O, os mascara e passa esse subconjunto como `ioflag`.

Isso é importante por dois motivos. Primeiro, significa que o `ioflag` que seu driver recebe é um *snapshot*. Se o programa do usuário alterar sua configuração de não-bloqueante (via `fcntl(F_SETFL, O_NONBLOCK)`) entre duas chamadas a `read(2)`, cada chamada carregará seu próprio `ioflag` atualizado. Você não precisa cachear o estado nem monitorar mudanças; o kernel recalcula o valor a cada despacho.

Segundo, significa que a maioria das constantes que você esperaria ver nunca chega ao seu handler. Coisas como `O_APPEND`, `O_TRUNC`, `O_CLOEXEC` e os vários flags no estilo `O_EXLOCK` pertencem às camadas do sistema de arquivos e da tabela de arquivos. Eles não influenciam o I/O de dispositivos de caracteres e não são repassados.

### Os Bits de Flag Que Importam

Os flags `IO_*` são declarados em `/usr/src/sys/sys/vnode.h`. Para drivers de dispositivos de caracteres, apenas um pequeno subconjunto vale a pena memorizar:

```c
#define	IO_UNIT		0x0001		/* do I/O as atomic unit */
#define	IO_APPEND	0x0002		/* append write to end */
#define	IO_NDELAY	0x0004		/* FNDELAY flag set in file table */
#define	IO_DIRECT	0x0010		/* attempt to bypass buffer cache */
```

Desses, **somente `IO_NDELAY` e `IO_DIRECT` são compostos no `ioflag` que seu handler recebe**. Os três primeiros bits existem para I/O de sistemas de arquivos. Um driver de dispositivo de caracteres que inspeciona `IO_UNIT` ou `IO_APPEND` está olhando para valores que sempre serão zero.

`IO_NDELAY` é o caso mais comum. Ele é definido quando o descritor está em modo não-bloqueante. Um driver que implementa leituras bloqueantes (Capítulo 10) usa esse bit para decidir entre dormir e retornar `EAGAIN`. Um driver do Capítulo 9 não dorme em nada, então o bit é apenas informativo, mas os capítulos futuros dependem dele.

`IO_DIRECT` é uma dica de que o programa do usuário abriu o descritor com `O_DIRECT`, pedindo ao kernel que ignore os caches de buffer onde possível. Para um driver de caracteres simples, isso é quase sempre irrelevante. Drivers próximos a subsistemas de armazenamento podem optar por respeitá-lo; a maioria não o faz.

Note a identidade numérica: `O_NONBLOCK` em `/usr/src/sys/sys/fcntl.h` tem o valor `0x0004`, e `IO_NDELAY` em `/usr/src/sys/sys/vnode.h` tem o mesmo valor. Isso não é coincidência. O comentário de cabeçalho acima das definições de `IO_*` afirma explicitamente que `IO_NDELAY` e `IO_DIRECT` estão alinhados com os bits correspondentes de `fcntl(2)` para que o devfs não precise traduzir. Seu driver pode inspecionar o bit de qualquer jeito e obterá a mesma resposta.

### Um Handler Que Verifica o `ioflag`

Aqui está como um handler de leitura com suporte a não-bloqueante se parece no nível de esqueleto. Não usaremos esse formato no Capítulo 9 porque nunca dormimos, mas estudá-lo agora torna a introdução do Capítulo 10 mais rápida.

```c
static int
myfirst_read_nb(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        mtx_lock(&sc->mtx);
        while (sc->bufused == 0) {
                if (ioflag & IO_NDELAY) {
                        mtx_unlock(&sc->mtx);
                        return (EAGAIN);
                }
                /* ... would msleep(9) here in Chapter 10 ... */
        }
        /* ... drain buffer, uiomove, unlock, return ... */
        error = 0;
        mtx_unlock(&sc->mtx);
        return (error);
}
```

O desvio em `IO_NDELAY` é a única decisão que o handler toma sobre bloqueio. Todo o resto da função é código de I/O comum. Essa concisão é parte do motivo pelo qual `ioflag` é um único inteiro: a resposta de um driver aos bits de flag é geralmente um único `if` perto do topo do handler, não uma máquina de estados elaborada.

### O Que os Estágios do Capítulo 9 Fazem com o `ioflag`

O Estágio 1, o Estágio 2 e o Estágio 3 **não** inspecionam o `ioflag`. Eles não podem bloquear, portanto o bit de não-bloqueante não tem sentido; eles também não se preocupam com `IO_DIRECT`. O argumento está presente nas assinaturas de seus handlers porque o typedef o exige, e ele é silenciosamente ignorado.

Ignorar silenciosamente um argumento não é um bug quando o comportamento ignorado é obviamente correto. Um leitor que abre um de nossos descritores com `O_NONBLOCK` verá um comportamento idêntico ao de um leitor que não o fez: nenhuma chamada dorme, portanto o flag não tem efeito observável. O Capítulo 10 é onde vamos conectar o flag de verdade.

### Um Pequeno Auxílio para Depuração

Se você estiver curioso sobre o que `ioflag` contém durante um teste, um único `device_printf` na entrada vai te dizer:

```c
device_printf(sc->dev, "d_read: ioflag=0x%x resid=%zd offset=%jd\n",
    ioflag, (ssize_t)uio->uio_resid, (intmax_t)uio->uio_offset);
```

Carregue o driver, execute `cat /dev/myfirst/0` e observe o valor hexadecimal. Em seguida, execute um pequeno programa que use `fcntl(fd, F_SETFL, O_NONBLOCK)` antes de ler e observe a diferença. Esse é um desvio instrutivo de dois minutos quando você está tornando o mecanismo real em sua mente pela primeira vez.

### `ioflag` na Árvore

Pesquise `IO_NDELAY` em `/usr/src/sys/dev` e você encontrará dezenas de resultados. Quase todos seguem o mesmo padrão: verifica o bit, retorna `EAGAIN` se estiver definido e o driver não tiver nada a servir, caso contrário dorme. A uniformidade é deliberada. Drivers FreeBSD tratam I/O não-bloqueante da mesma forma, sejam pseudo-dispositivos, linhas TTY, endpoints USB ou armazenamento apoiado por GEOM, e essa consistência é parte do motivo pelo qual programas de usuário escritos para um tipo de dispositivo portam facilmente para outro.

---

## Entendendo `struct uio` em Profundidade

`struct uio` é a representação do kernel de uma requisição de I/O. Ela é passada para toda invocação de `d_read` e `d_write`. Cada chamada bem-sucedida a `uiomove(9)` a modifica. Todo desenvolvedor de drivers que você já encontrou ficou olhando para seus campos em algum momento, perguntando-se em quais confiar. Esta seção é onde tornamos a estrutura menos misteriosa.

### A Declaração

De `/usr/src/sys/sys/uio.h`:

```c
struct uio {
        struct  iovec *uio_iov;         /* scatter/gather list */
        int     uio_iovcnt;             /* length of scatter/gather list */
        off_t   uio_offset;             /* offset in target object */
        ssize_t uio_resid;              /* remaining bytes to process */
        enum    uio_seg uio_segflg;     /* address space */
        enum    uio_rw uio_rw;          /* operation */
        struct  thread *uio_td;         /* owner */
};
```

Sete campos. Cada um tem uma finalidade específica, e há exatamente uma função, `uiomove(9)`, que usa todos eles em conjunto. Seu driver lerá alguns dos campos diretamente; alguns você nunca tocará.

### `uio_iov` e `uio_iovcnt`: A Lista de Scatter-Gather

Um único `read(2)` ou `write(2)` opera sobre um buffer de usuário contíguo. Os relacionados `readv(2)` e `writev(2)` operam sobre uma lista de buffers (um "iovec"). O kernel representa ambos os casos de forma uniforme como uma lista de entradas `iovec`, usando uma lista de comprimento um para o caso simples.

`uio_iov` aponta para a primeira entrada dessa lista. `uio_iovcnt` é o número de entradas. Cada entrada é um `struct iovec`, declarado em `/usr/src/sys/sys/_iovec.h`:

```c
struct iovec {
        void    *iov_base;
        size_t   iov_len;
};
```

`iov_base` é um ponteiro para a memória do usuário (para um uio `UIO_USERSPACE`) ou para a memória do kernel (para um uio `UIO_SYSSPACE`). `iov_len` é o número de bytes restantes nessa entrada.

Você quase nunca tocará nesses campos diretamente. `uiomove(9)` percorre a lista iovec por você, consumindo entradas à medida que move bytes e deixando a lista consistente com a transferência restante. Se seu driver acessar `uio_iov` ou `uio_iovcnt` manualmente, você está escrevendo um driver muito incomum ou fazendo algo errado. O padrão convencional é: deixe o `uiomove` gerenciar o iovec e leia os outros campos para entender o estado da requisição.

### `uio_offset`: O Deslocamento no Destino

Para uma leitura ou escrita em um arquivo comum, `uio_offset` é a posição no arquivo onde o I/O está ocorrendo. O kernel o incrementa à medida que os bytes se movem, então um `read(2)` sequencial avança naturalmente pelo arquivo.

Para um arquivo de dispositivo, o significado de `uio_offset` é definido pelo driver. Um dispositivo que é verdadeiramente sequencial e não tem noção de posição ignorará o valor recebido e deixará o valor resultante refletir o que `uiomove` fez. Um dispositivo que se apoia em um buffer fixo tratará o deslocamento como um endereço nesse buffer e o respeitará.

`uiomove(9)` atualiza `uio_offset` em sincronia com `uio_resid`: para cada byte que move, ele decrementa `uio_resid` em um e incrementa `uio_offset` em um. Se seu driver chama `uiomove` uma vez por handler, raramente você precisará ler `uio_offset` manualmente. Se seu driver chama `uiomove` mais de uma vez, ou se usa o deslocamento para indexar seu próprio buffer, `uiomove_frombuf(9)` é o helper que você deseja.

### `uio_resid`: Os Bytes Restantes

`uio_resid` é o número de bytes que ainda precisam ser transferidos. No início de `d_read`, é o comprimento total que o usuário pediu. Ao final de uma transferência bem-sucedida, é o que não foi transferido; o kernel subtrai esse valor do comprimento original para produzir o valor de retorno de `read(2)`.

Duas armadilhas de aritmética com sinal merecem atenção. Primeira, `uio_resid` é um `ssize_t`, que é com sinal. Um valor negativo é ilegal (e `uiomove` vai acionar um `KASSERT` nele em kernels de debug), mas cuidado para não construir um acidentalmente com aritmética descuidada. Segunda, `uio_resid` pode ser zero no início da chamada. Isso acontece quando um programa de usuário chama `read(fd, buf, 0)` ou `write(fd, buf, 0)`. Seu handler não deve tratar zero como "sem intenção do usuário" e então prosseguir com I/O contra o que pode ser um buffer não inicializado. O padrão seguro é verificar o zero cedo e retornar zero (ou aceitar zero e retornar zero, para escritas). `uiomove` lida com isso de forma limpa: retorna zero imediatamente sem tocar em nada. Então, na prática, a verificação antecipada muitas vezes é redundante; o que importa é que você não *assuma* que o valor é diferente de zero.

### `uio_segflg`: Onde o Buffer Vive

Este campo indica onde os ponteiros do iovec se referem: espaço do usuário (`UIO_USERSPACE`), espaço do kernel (`UIO_SYSSPACE`) ou um mapa de objeto direto (`UIO_NOCOPY`). A enumeração está em `/usr/src/sys/sys/_uio.h`:

```c
enum uio_seg {
        UIO_USERSPACE,          /* from user data space */
        UIO_SYSSPACE,           /* from system space */
        UIO_NOCOPY              /* don't copy, already in object */
};
```

Para um `d_read` ou `d_write` chamado em nome de uma syscall de usuário, `uio_segflg` é `UIO_USERSPACE`. `uiomove(9)` lê o campo e escolhe a primitiva de transferência correta: `copyin` / `copyout` para segmentos em espaço do usuário, `bcopy` para segmentos em espaço do kernel. Seu driver não precisa fazer um desvio sobre isso; `uiomove` faz isso por você.

Você ocasionalmente verá código que constrói um uio em modo kernel manualmente, tipicamente para reutilizar uma função que recebe um uio mas servi-la a partir de um buffer do kernel. Esse código define `uio_segflg` como `UIO_SYSSPACE`. É legítimo e útil, e o encontraremos brevemente nos laboratórios. Não confunda com um uio em espaço do usuário: as propriedades de segurança são muito diferentes.

### `uio_rw`: A Direção

A direção da transferência. A enumeração está no mesmo cabeçalho:

```c
enum uio_rw {
        UIO_READ,
        UIO_WRITE
};
```

Para um handler `d_read`, `uio_rw` é `UIO_READ`. Para um handler `d_write`, `uio_rw` é `UIO_WRITE`. O campo informa ao `uiomove` se deve copiar do kernel para o usuário (leitura) ou do usuário para o kernel (escrita). Alguns handlers fazem uma asserção sobre isso como verificação de sanidade:

```c
KASSERT(uio->uio_rw == UIO_READ,
    ("Can't be in %s for write", __func__));
```

Essa asserção vem de `zero_read` em `/usr/src/sys/dev/null/null.c`. É uma forma econômica de documentar o invariante. Seu driver não precisa de asserções como essa para estar correto, mas elas podem ser uma rede de segurança útil durante o desenvolvimento.

### `uio_td`: A Thread Proprietária

O `struct thread *` do chamador. Para um uio construído em nome de uma syscall, essa é a thread que fez a syscall. Algumas APIs do kernel precisam de um ponteiro de thread; usar `uio->uio_td` em vez de `curthread` mantém a associação explícita quando o uio é repassado adiante.

Em um `d_read` ou `d_write` simples, raramente você precisará de `uio_td`. Ele se torna útil se seu driver quiser inspecionar as credenciais do chamador durante a chamada, além do que `open(2)` já validou. Isso é incomum.

### Uma Ilustração: O Que Acontece com uio Durante uma Chamada read(fd, buf, 1024)

Percorrer um único `read(2)` ajuda a consolidar como os campos se movem. Suponha que um programa de usuário chame:

```c
ssize_t n = read(fd, buf, 1024);
```

O kernel constrói um uio que se parece aproximadamente assim quando chega ao seu `d_read`:

- `uio_iov` aponta para uma lista de uma entrada.
- A única entrada tem `iov_base = buf` (o buffer do usuário) e `iov_len = 1024`.
- `uio_iovcnt = 1`.
- `uio_offset = <onde quer que o ponteiro de arquivo atual estivesse>`. Para um dispositivo buscável recém-aberto, zero.
- `uio_resid = 1024`.
- `uio_segflg = UIO_USERSPACE`.
- `uio_rw = UIO_READ`.
- `uio_td = <thread chamadora>`.

Seu handler chama, digamos, `uiomove(sc->buf, 300, uio)`. Dentro de `uiomove`, o kernel:

- Pega a primeira entrada iovec.
- Determina que 300 é menor que 1024, então vai mover 300 bytes.
- Chama `copyout(sc->buf, buf, 300)`.
- Decrementa `iov_len` em 300, para 724.
- Avança `iov_base` em 300, para `buf + 300`.
- Decrementa `uio_resid` em 300, para 724.
- Incrementa `uio_offset` em 300.

Seu handler retorna zero. O kernel calcula a contagem de bytes como `1024 - 724 = 300` e retorna 300 de `read(2)`. O usuário vê 300 bytes em `buf[0..299]` e sabe que pode chamar `read(2)` novamente para obter o restante, ou prosseguir com o que já tem.

Isso é tudo o que `uiomove` faz, em ordem. Não há nenhuma mágica.

### Como `readv(2)` Funciona de Forma Diferente

Se o usuário chamar `readv(fd, iov, 3)` com três entradas iovec, o uio no início de `d_read` terá `uio_iovcnt = 3`, `uio_iov` apontando para a lista de três entradas e `uio_resid` igual à soma de seus comprimentos. Seu handler faz uma chamada a `uiomove` (ou várias, em um loop) e `uiomove` percorre a lista por você. O código do driver é idêntico.

Esse é um dos benefícios silenciosos da abstração uio: as leituras e escritas scatter-gather são gratuitas. Seu driver foi escrito para um único buffer; ele já trata requisições com múltiplos buffers.

### Múltiplas Chamadas a `uiomove` em um Único Handler

Uma convenção que ocasionalmente confunde iniciantes: **uma única invocação de `d_read` ou `d_write` pode fazer múltiplas chamadas a `uiomove`**. Cada chamada reduz `uio_resid` e avança `uio_iov`. O uio permanece consistente entre as chamadas. Se o primeiro `uiomove` do seu handler transferir 128 bytes e o próximo transferir 256, o kernel simplesmente verá uma única chamada ao handler que transferiu 384 bytes.

O que você **não** deve fazer é salvar um ponteiro para o uio entre chamadas ao handler e tentar retomá-lo depois. Um uio é válido pela duração do dispatch que o produziu. Entre dispatches, a memória para a qual ele aponta (incluindo o array iovec) pode não ser válida. Se você precisar enfileirar uma requisição para processamento posterior, copie os dados necessários do uio (para seu próprio buffer no kernel) e use sua própria fila.

### O Que Seu Driver Precisa Ler e o Que Deve Deixar em Paz

Uma referência rápida, em ordem decrescente de frequência:

| Campo          | Ler?        | Escrever?                         |
|----------------|-------------|-----------------------------------|
| `uio_resid`    | Sim, frequentemente  | Apenas para marcar uma transferência como consumida (ex.: `uio_resid = 0`) |
| `uio_offset`   | Sim, se você o respeitar | Não, deixe `uiomove` atualizá-lo |
| `uio_rw`       | Ocasionalmente, para KASSERTs | Não |
| `uio_segflg`   | Raramente       | Não, a menos que esteja construindo um uio em modo kernel |
| `uio_td`       | Raramente       | Não |
| `uio_iov`      | Quase nunca | Nunca |
| `uio_iovcnt`   | Quase nunca | Nunca |

Se um driver iniciante escrever em `uio_iov` ou `uio_iovcnt`, algo saiu completamente dos trilhos. Se ele escrever em `uio_resid` de outra forma que não o truque `uio_resid = 0` de "consumi tudo", algo está ligeiramente errado. Se ele ler das três primeiras linhas, está no caminho normal.

### Os Campos do uio na Prática

Tudo isso se torna menos intimidador depois que você vê um handler realmente usando-o. Os estágios myfirst deste capítulo inspecionam `uio_resid` (para limitar as transferências), leem ocasionalmente `uio_offset` (para saber onde um leitor está) e delegam todo o resto a `uiomove`. Os helpers fazem o trabalho real, e o código do driver permanece pequeno.

### O Ciclo de Vida de um Único uio: Três Instantâneos

Para consolidar a discussão campo a campo, vale a pena percorrer o estado de um uio em três momentos de sua vida: o instante em que seu handler é chamado, o instante após um `uiomove` parcial e o instante logo antes de seu handler retornar. Cada instantâneo captura o mesmo uio, para que você possa ver exatamente como os campos evoluem.

O exemplo é uma chamada `read(fd, buf, 1024)` em um driver cujo handler de leitura servirá 300 bytes por chamada.

**Instantâneo 1: Na entrada de `d_read`.**

```text
uio_iov     -> [ { iov_base = buf,       iov_len = 1024 } ]
uio_iovcnt  =  1
uio_offset  =  0        (this is the first read on the descriptor)
uio_resid   =  1024
uio_segflg  =  UIO_USERSPACE
uio_rw      =  UIO_READ
uio_td      =  <calling thread>
```

O uio descreve uma requisição completa. O usuário pediu 1024 bytes, o buffer está no espaço do usuário, a direção é leitura, o offset é zero. É isso que o kernel entrega ao seu handler.

**Instantâneo 2: Após `uiomove(sc->buf, 300, uio)` retornar com sucesso.**

```text
uio_iov     -> [ { iov_base = buf + 300, iov_len =  724 } ]
uio_iovcnt  =  1
uio_offset  =  300
uio_resid   =  724
uio_segflg  =  UIO_USERSPACE    (unchanged)
uio_rw      =  UIO_READ         (unchanged)
uio_td      =  <calling thread> (unchanged)
```

Quatro campos mudaram em sincronia. `iov_base` avançou 300 bytes para que a próxima transferência coloque os bytes após os que acabaram de ser escritos. `iov_len` diminuiu 300, pois a entrada iovec agora descreve apenas os 724 bytes restantes. `uio_offset` cresceu 300, pois 300 bytes de posição no stream foram avançados. `uio_resid` diminuiu 300, pois 300 bytes de trabalho foram concluídos.

Três campos permaneceram fixos: `uio_segflg`, `uio_rw` e `uio_td` descrevem a *forma* da requisição, que não muda durante a transferência. Se seu handler precisar verificar algum deles, poderá fazê-lo antes ou depois de `uiomove` e obterá a mesma resposta.

**Instantâneo 3: Logo antes de `d_read` retornar.**

Imagine que o handler, após servir 300 bytes, decide que não tem mais dados e retorna zero sem chamar `uiomove` novamente.

```text
uio_iov     -> [ { iov_base = buf + 300, iov_len =  724 } ]
uio_iovcnt  =  1
uio_offset  =  300
uio_resid   =  724
uio_segflg  =  UIO_USERSPACE
uio_rw      =  UIO_READ
uio_td      =  <calling thread>
```

Idêntico ao Instantâneo 2. O handler não tocou em nada; simplesmente retornou. O kernel verá `uio_resid = 724` versus o `uio_resid = 1024` inicial e calculará `1024 - 724 = 300`, que retornará ao espaço do usuário como resultado de `read(2)`. O chamador verá um valor de retorno de 300 e saberá que o driver produziu 300 bytes.

Se o handler tivesse executado um loop em `uiomove` até `uio_resid` chegar a zero, o instantâneo no retorno teria `uio_resid = 0` e o kernel retornaria 1024 ao espaço do usuário (uma transferência completa). Se o handler tivesse chamado `uiomove` e obtido um erro, `uio_resid` refletiria o progresso parcial que ocorreu antes da falha, e o handler retornaria o errno.

### O Que Esse Modelo Mental Oferece a Você

Três observações emergem dos instantâneos e merecem ser nomeadas explicitamente.

**Primeiro, uio_resid é o contrato.** Qualquer valor que estiver em `uio_resid` quando seu handler retornar, o kernel irá confiar. Se for menor do que na entrada, alguns bytes foram transferidos; a diferença é a contagem de bytes. Se estiver inalterado, nada foi transferido; o valor de retorno será zero (EOF) ou um errno (dependendo do que seu handler retornou).

**Segundo, uiomove é a única coisa na qual você deve confiar para decrementar uio_resid.** Um driver que subtrai manualmente de `uio_resid` quase certamente está fazendo algo errado; o tratamento de falhas do kernel, o percurso do iovec e as atualizações de offset estão todos incorporados ao caminho de código de `uiomove`. Definir `uio_resid = 0` é a única exceção, usada por drivers como `null_write` de `null.c` para dizer "finja que todos os bytes foram consumidos".

**Terceiro, o uio é espaço temporário.** Um uio não é um objeto de longa duração. Ele é criado a cada syscall, é consumido à medida que `uiomove` o usa e é descartado quando seu handler retorna. Salvar um ponteiro para o uio para uso posterior é um bug de tempo de vida esperando para se manifestar. Se seu driver precisar de dados do uio além da chamada atual, ele deve copiar os bytes para seu próprio armazenamento (que é o que `d_write` faz: copia os bytes por meio de `uiomove` para `sc->buf`, sem deixar nada no uio para depois).

Esses três fatos são a fundação sobre a qual todo o restante do capítulo é construído. Se você os internalizar, o restante do mecanismo uio deixará de parecer misterioso.



## Transferência Segura de Dados: `uiomove`, `copyin`, `copyout`

As seções anteriores descreveram `struct uio` e nomearam `uiomove(9)` como a função que move bytes. Esta seção explica por que essa função existe, o que ela faz internamente e quando um driver deve recorrer diretamente a `copyin(9)` ou `copyout(9)`.

### Por Que o Acesso Direto à Memória É Inseguro

Um processo de usuário tem seu próprio espaço de endereçamento virtual. Quando um processo chama `read(2)` com um ponteiro de buffer, esse ponteiro é um endereço virtual no espaço de endereçamento do processo. Ele pode se referir a uma página de memória que está presente na RAM física, ou a uma página que foi paginada para o disco, ou a uma página que não está mapeada. Pode até ser um ponteiro que o programa de usuário fabricou deliberadamente para tentar derrubar o kernel.

Do ponto de vista do kernel, o espaço de endereçamento do usuário não é diretamente endereçável. O kernel tem seu próprio espaço de endereçamento; um ponteiro de usuário entregue ao kernel não tem significado como ponteiro do kernel. Mesmo que o kernel possa resolver o ponteiro do usuário por meio do mecanismo de tabela de páginas, usá-lo diretamente é perigoso: a página pode gerar uma falha, a proteção de memória pode estar errada, o endereço pode cair fora das regiões mapeadas do processo, ou o ponteiro pode ter sido construído para apontar para a memória do kernel em uma tentativa de vazá-la ou corrompê-la.

O acesso direto à memória, em outras palavras, não é uma funcionalidade que o kernel obtém de graça. É um privilégio que deve ser exercido com cuidado, com cada acesso roteado por funções que sabem como tratar falhas, verificar proteções e manter os espaços de endereçamento do usuário e do kernel distintos.

No FreeBSD, essas funções são `copyin(9)` (do usuário para o kernel), `copyout(9)` (do kernel para o usuário) e `uiomove(9)` (em qualquer direção, guiado pelo uio).

### O Que `copyin(9)` e `copyout(9)` Fazem

De `/usr/src/sys/sys/systm.h`:

```c
int copyin(const void * __restrict udaddr,
           void * __restrict kaddr, size_t len);

int copyout(const void * __restrict kaddr,
            void * __restrict udaddr, size_t len);
```

`copyin` recebe um ponteiro do espaço do usuário, um ponteiro do espaço do kernel e um comprimento. Ele copia `len` bytes do usuário para o kernel. `copyout` é o inverso: ponteiro do kernel, ponteiro do usuário, comprimento. Copia do kernel para o usuário.

Ambas as funções validam o endereço do usuário, trazem a página do usuário para a memória se necessário, executam a cópia e capturam qualquer falha que ocorra. Elas retornam zero em caso de sucesso e `EFAULT` se o endereço do usuário era inválido ou se a cópia falhou por outro motivo. Elas nunca corrompem silenciosamente a memória; sempre ou completam a cópia ou relatam o erro.

Essas duas primitivas são a fundação sobre a qual todas as transferências de memória entre usuário e kernel são construídas. São elas que `uiomove(9)` chama internamente quando o uio está no espaço do usuário. São elas que `fubyte(9)`, `subyte(9)` e um punhado de outras funções de conveniência usam. São as funções nas quais o kernel confia como fronteira de segurança.

### O Que `uiomove(9)` Faz

`uiomove(9)` é um wrapper em torno de `copyin` / `copyout` que compreende a estrutura uio. Sua implementação é curta e vale a pena ler; ela está em `/usr/src/sys/kern/subr_uio.c`.

De forma geral, o algoritmo é:

1. Verificar a sanidade do uio: a direção é válida, o resid não é negativo, a thread proprietária é a thread atual se o segmento estiver no espaço do usuário.
2. Loop: enquanto o chamador pediu mais bytes (`n > 0`) e o uio ainda tem espaço (`uio->uio_resid > 0`), consumir a próxima entrada iovec.
3. Para cada entrada iovec, calcular quantos bytes mover (o mínimo entre o comprimento da entrada, a contagem restante do chamador e o resid do uio) e chamar `copyin` ou `copyout` (para segmentos no espaço do usuário) ou `bcopy` (para segmentos no espaço do kernel), dependendo de `uio_rw` e `uio_segflg`.
4. Avançar `iov_base` e `iov_len` do iovec conforme os bytes são transferidos; decrementar `uio_resid`, incrementar `uio_offset`.
5. Se qualquer cópia falhar, sair do loop e retornar o erro.

A função retorna zero em caso de sucesso ou um código errno em caso de falha. A falha mais comum é `EFAULT` proveniente de um ponteiro de usuário inválido.

A propriedade crítica de `uiomove` é que ele é **a única função que seu driver deve usar para mover bytes por meio de um uio**. Não `bcopy`, não `memcpy`, não `copyout`. O uio carrega as informações que `uiomove` precisa para escolher a primitiva correta, e o driver não precisa adivinhar.

### Quando Usar Qual

A divisão de trabalho é direta na prática.

Use `uiomove(9)` nos handlers `d_read` e `d_write` sempre que o uio descrever a transferência. Este é o caso esmagadoramente mais comum.

Use `copyin(9)` e `copyout(9)` diretamente quando você tiver um ponteiro de usuário proveniente de algum lugar que não seja um uio. Exemplos:

- Dentro de um handler `d_ioctl`, para comandos de controle que carregam ponteiros do espaço do usuário como argumentos (Capítulo 25).
- Dentro de uma thread do kernel que aceita dados fornecidos pelo usuário por meio de um mecanismo que você mesmo construiu, e não por meio de um uio.
- Ao ler ou escrever um pequeno trecho de memória do usuário de tamanho fixo que não é o objeto da syscall.

**Não** use `copyin` ou `copyout` dentro de `d_read` ou `d_write` para buscar dados do iovec do uio. Sempre passe por `uiomove`. O iovec não tem garantia de ser um único buffer contíguo e, mesmo que seja, seu driver não tem motivo para ir além da abstração uio e tocá-lo diretamente.

### Uma Tabela para Consulta Rápida

| Situação                                                                                     | Ferramenta recomendada  |
|----------------------------------------------------------------------------------------------|-------------------------|
| Transferir bytes por meio de um uio (leitura ou escrita)                                     | `uiomove(9)`            |
| Transferir bytes por meio de um uio, com um buffer fixo no kernel e offset automático        | `uiomove_frombuf(9)`    |
| Ler um ponteiro de usuário conhecido que não é transportado por um uio                       | `copyin(9)`             |
| Escrever em um ponteiro de usuário conhecido que não é transportado por um uio               | `copyout(9)`            |
| Ler uma string terminada em null do espaço do usuário                                        | `copyinstr(9)`          |
| Ler um único byte do espaço do usuário                                                       | `fubyte(9)`             |
| Escrever um único byte no espaço do usuário                                                  | `subyte(9)`             |

`fubyte` e `subyte` são funções de nicho; a maioria dos drivers nunca as utiliza. Elas estão listadas aqui apenas para reconhecimento. `copyinstr` é ocasionalmente útil em caminhos de controle que recebem uma string do usuário; não faremos uso dela neste capítulo.

### Por Que Não um `memcpy` Direto?

Um iniciante às vezes pergunta: "posso simplesmente fazer um cast do ponteiro do usuário e copiar os bytes com `memcpy`?" A resposta é um não sem qualificações, e vale a pena entender o motivo.

`memcpy` pressupõe que ambos os ponteiros apontam para memória acessível no espaço de endereçamento atual. Um ponteiro de usuário não tem essa garantia. Em arquiteturas que separam ponteiros de usuário e de kernel no nível do hardware (SMAP no amd64, por exemplo), a CPU recusará o acesso. Em arquiteturas que compartilham o espaço de endereçamento, o ponteiro ainda pode ser inválido, pode apontar para uma página que foi paginada para disco, ou pode apontar para uma página que o kernel não tem permissão de tocar. Nenhum desses casos pode ser tratado com segurança dentro de um `memcpy` simples: a falha resultante ou causaria um panic no sistema ou vazaria informações através da fronteira de confiança.

As primitivas do kernel `copyin` e `copyout` existem exatamente para tratar esses casos de forma correta. Elas instalam um handler de falha antes do acesso, de modo que um ponteiro de usuário inválido retorna `EFAULT` em vez de causar um panic. Elas respeitam o SMAP e proteções similares. Elas conseguem aguardar a paginação de uma página de volta para a memória. Nada disso é opcional, e nada disso é algo que o seu driver deva replicar.

A regra prática: se um ponteiro veio do espaço do usuário, passe-o por `copyin` / `copyout` / `uiomove`. Não o dereferencie diretamente. Não use `memcpy` através dele. Não o passe para nenhuma função que usará `memcpy` através dele. Se você respeitar o limite de abstração, o kernel lhe oferece uma interface estável, segura e bem documentada. Se você o cruzar, todos os bugs decorrentes serão de sua responsabilidade para sempre.

### O Que Acontece Durante uma Falha

Um exemplo concreto: o que `uiomove` faz de fato quando o ponteiro de usuário é inválido?

O kernel instala um handler de falha antes da cópia, tipicamente por meio de sua tabela de trap handlers. Quando a CPU sofre uma falha no acesso ao endereço do usuário, o handler percebe que a instrução que falhou está dentro de um caminho de código de `copyin` ou `copyout`, avança até o caminho de retorno de falha e retorna `EFAULT`. Sem panic. Sem corrupção de dados. O chamador de `uiomove` recebe um valor de retorno diferente de zero, propaga o erro para o chamador de `d_read` ou `d_write`, e a syscall retorna para o userland com `errno = EFAULT`.

O driver não precisa fazer nada especial para cooperar com esse mecanismo. Basta verificar o valor de retorno de `uiomove` e propagar os erros. Faremos isso em todos os handlers deste capítulo.

### Alinhamento e Segurança de Tipos

Mais uma sutileza que merece atenção. O buffer do usuário é um fluxo de bytes. Ele não carrega nenhuma informação de tipo. Se o seu driver colocar uma `struct` no buffer e o usuário a retirar, o usuário recebe bytes; esses bytes podem ou não estar corretamente alinhados para um acesso a `struct` na arquitetura do chamador.

Para o `myfirst`, o problema não ocorre, porque os bytes são texto arbitrário do usuário. Para drivers que desejam exportar dados estruturados, a convenção é ou exigir que o usuário copie os bytes para uma estrutura local alinhada antes de interpretá-los, ou incluir negociação explícita de alinhamento e versão no formato dos dados. `ioctl(2)` contorna o problema porque o layout dos dados faz parte do número do comando `IOCTL`; `read` e `write` não têm esse luxo.

Este é um dos pontos em que é tentador, e errado, sobrepor dados estruturados sobre `read`/`write`. Se o seu driver precisa entregar dados tipados ao userland, a interface `ioctl` ou um mecanismo externo de RPC são as ferramentas corretas. `read` e `write` transportam bytes. Essa é a promessa, e é ela que os mantém portáveis.

### Um Pequeno Exemplo Trabalhado

Suponha que um programa do usuário grave quatro inteiros no seu dispositivo:

```c
int buf[4] = { 1, 2, 3, 4 };
write(fd, buf, sizeof(buf));
```

No `d_write` do driver, o uio terá a seguinte aparência:

- Uma entrada iovec com `iov_base = <endereço de usuário de buf[0]>`, `iov_len = 16`.
- `uio_resid = 16`, `uio_offset = 0`, `uio_segflg = UIO_USERSPACE`, `uio_rw = UIO_WRITE`.

Um handler ingênuo poderia chamar `uiomove(sc->intbuf, 16, uio)`, onde `sc->intbuf` é `int sc->intbuf[4];`. O `uiomove` emitiria um `copyin` que copiaria os 16 bytes. Em caso de sucesso, `sc->intbuf` conteria os quatro inteiros na ordem de bytes do programa chamador.

Observe, porém: o usuário pode ter gravado esses inteiros na ordem de bytes de uma CPU completamente diferente, caso o driver venha a ser usado entre arquiteturas distintas. O usuário pode ter usado `int32_t` onde o driver usou `int`. O usuário pode ter preenchido a estrutura de forma diferente. Para o `myfirst`, nada disso importa porque tratamos os dados como bytes opacos. Para um driver que expõe dados estruturados por `read`/`write`, esses problemas se multiplicam rapidamente, e é por isso que a maioria dos drivers reais usa `ioctl` para cargas estruturadas, ou declara um formato de transmissão explícito (ordem de bytes, larguras dos campos, alinhamento) em sua documentação.

A lição: `uiomove` move bytes. Ele não sabe nem se importa com tipos. Cabe ao seu driver decidir o que esses bytes significam.

### Um Mini Estudo de Caso: Quando o Round-Trip de uma Struct Dá Errado

Para tornar o ponto "bytes não são tipos" mais concreto, percorra uma tentativa plausível, porém equivocada, de expor um contador do kernel via `read(2)` como uma estrutura tipada.

Suponha que o seu driver mantenha um conjunto de contadores:

```c
struct myfirst_stats {
        uint64_t reads;
        uint64_t writes;
        uint64_t errors;
        uint32_t flags;
};
```

E suponha que, otimistamente, você os exponha via `d_read`:

```c
static int
stats_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_stats snap;

        mtx_lock(&sc->mtx);
        snap.reads  = sc->stat_reads;
        snap.writes = sc->stat_writes;
        snap.errors = sc->stat_errors;
        snap.flags  = sc->stat_flags;
        mtx_unlock(&sc->mtx);

        return (uiomove(&snap, sizeof(snap), uio));
}
```

À primeira vista, tudo parece correto. Os bytes chegam ao espaço do usuário. Um leitor pode fazer um cast do buffer para `struct myfirst_stats` e examinar os campos. O autor testa em amd64, vê os valores corretos e distribui o driver.

Três problemas estão ali, esperando.

**Problema 1: Padding da struct.** O layout de `struct myfirst_stats` depende do compilador e da arquitetura. No amd64 com o ABI padrão, `uint64_t` requer alinhamento de 8 bytes, de modo que a struct tem 8 bytes para `reads`, 8 para `writes`, 8 para `errors`, 4 para `flags`, mais 4 bytes de padding final para arredondar o tamanho para 32. O programa do usuário deve declarar uma struct com o *mesmo* padding para ler os campos corretamente. Um programa que redeclare a struct com `#pragma pack(1)` ou que use uma versão diferente do compilador interpretará os bytes de forma errada e verá lixo em `errors`.

**Problema 2: Ordem de bytes.** Uma máquina amd64 armazena `uint64_t` em little-endian. Um programa do usuário rodando na mesma arquitetura decodifica corretamente. Um programa rodando remotamente em uma máquina big-endian, lendo os bytes por um pipe de rede, vê os inteiros com os bytes invertidos. O driver não escolheu uma ordem de bytes para o protocolo de transmissão, então o formato é dependente da CPU por acidente.

**Problema 3: Atomicidade do snapshot.** O leitor pode extrair os bytes via `uiomove` após `mtx_unlock` liberar o mutex e antes de o kernel devolver o controle ao chamador. Entre esses dois momentos, os campos `snap.reads`, `snap.writes` etc. já estão capturados na variável local `snap` na pilha, então *essa parte* está correta. Mas o exemplo é pequeno o suficiente para o bug não aparecer; um snapshot maior poderia ser capturado ao longo de várias aquisições de mutex e exibir leituras inconsistentes.

**A solução não é "se esforçar mais" com o layout da struct.** A solução é parar de usar `read(2)` para dados estruturados. Duas opções melhores existem:

- **`sysctl`**: o capítulo tem utilizado essa abordagem ao longo de todo o texto. Contadores individuais são expostos como nós nomeados de tipo conhecido. `sysctl(3)` no lado do usuário retorna inteiros diretamente: sem layout de struct, sem padding, sem ordem de bytes.
- **`d_ioctl`**: o Capítulo 25 desenvolve o `ioctl` de forma completa. Para este caso de uso, um `ioctl` com uma estrutura de requisição bem definida seria apropriado, e as macros `_IOR` / `_IOW` documentam o tamanho e a direção.

A interface `read(2)` promete "um fluxo de bytes que o driver define"; nada mais. Se você respeitar a promessa, o seu driver será portável, testável e resistente à deriva silenciosa de layout. Se você quebrar a promessa expondo estruturas tipadas, herda todas as armadilhas de ABI que os protocolos de rede levaram décadas para aprender a contornar.

No `myfirst` deste capítulo nunca encontramos esse problema, porque sempre trabalhamos apenas com fluxos de bytes opacos. O objetivo do estudo de caso é ajudar você a reconhecer o formato desse erro antes que alguém lhe entregue um driver que já o está cometendo.

### Resumo desta Seção

- Use `uiomove(9)` dentro de `d_read` e `d_write`. Ele lê o uio, escolhe a primitiva correta e trata falhas de usuário e de kernel por você.
- Use `uiomove_frombuf(9)` quando quiser aritmética automática de deslocamento dentro do buffer.
- Use `copyin(9)` e `copyout(9)` apenas quando tiver um ponteiro de usuário fora do contexto do uio, tipicamente em `d_ioctl`.
- Nunca dereferencie ponteiros de usuário diretamente.
- Verifique os valores de retorno. Qualquer cópia pode falhar com `EFAULT`, e o seu handler deve propagar o erro.

Essas regras são curtas, mas cobrem quase todos os erros de segurança de I/O que drivers iniciantes cometem.

---

## Gerenciando Buffers Internos no Seu Driver

Os handlers de leitura e escrita são a superfície visível do caminho de I/O do seu driver. Por trás deles, o driver precisa armazenar dados em algum lugar. Esta seção trata de como esse armazenamento é projetado, alocado, protegido e liberado, no nível acessível para iniciantes que este capítulo requer. O Capítulo 10 estenderá o buffer para um ring buffer verdadeiro e o tornará seguro para uso concorrente sob carga; estamos deliberadamente aquém disso aqui.

### Por Que Você Precisa de Buffers

Um buffer é um armazenamento temporário entre chamadas de I/O. Um driver usa um buffer por pelo menos três razões:

1. **Equalização de taxa.** Produtores e consumidores não chegam ao mesmo tempo. Uma escrita pode depositar bytes que uma leitura posterior irá buscar.
2. **Reformatação de requisições.** Um usuário pode ler em unidades que não se alinham com a forma como o driver produz dados. Um buffer absorve essa incompatibilidade.
3. **Isolamento.** Os bytes dentro do buffer do driver são dados do kernel. Não são ponteiros de usuário, não são endereços de DMA, não estão em uma scatter-gather list. Tudo em um buffer do kernel é endereçável pelo driver de forma segura e uniforme.

Para o `myfirst`, o buffer é um pequeno trecho de armazenamento em RAM. `d_write` grava nele; `d_read` lê dele. O buffer é o estado do driver. O mecanismo uio é o encanamento que move os bytes para dentro e para fora.

### Alocação Estática vs. Dinâmica

Duas alternativas razoáveis existem para onde o buffer reside.

**Alocação estática** coloca o buffer dentro da estrutura softc ou como um array no nível do módulo:

```c
struct myfirst_softc {
        ...
        char buf[4096];
        size_t bufused;
        ...
};
```

Prós: a alocação nunca falha, o tamanho é explícito e o tempo de vida está trivialmente vinculado ao softc. Contras: o tamanho fica fixado em tempo de compilação; se você quiser torná-lo configurável mais tarde, precisará refatorar.

**Alocação dinâmica** usa `malloc(9)` de um bucket `M_*`:

```c
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
```

Prós: o tamanho pode ser escolhido no momento do attach a partir de um sysctl ou tunable; pode ser redimensionado com cuidado. Contras: a alocação pode falhar (menos relevante com `M_WAITOK`, mais com `M_NOWAIT`); o driver assume mais um caminho de liberação.

Para buffers pequenos, a alocação estática dentro do softc é a escolha mais simples, e a que o Capítulo 7 usou implicitamente ao depender do Newbus para alocar todo o softc. O Capítulo 9 usará alocação dinâmica porque o buffer é grande o suficiente para que colocá-lo no softc seja um pouco desperdiçador, e porque o caminho dinâmico é o padrão que você usará repetidamente mais adiante no livro.

### A Chamada `malloc(9)`

O `malloc(9)` do kernel recebe três argumentos: o tamanho, um tipo malloc (que é uma tag usada pelo kernel para contabilidade e depuração) e um conjunto de flags. Uma forma comum:

```c
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
```

`M_DEVBUF` é o tipo malloc genérico de "buffer de dispositivo", definido em toda a árvore e apropriado para dados privados do driver que não merecem uma tag dedicada. Se o seu driver crescer o suficiente para justificar uma tag própria, você pode declarar uma com `MALLOC_DECLARE(M_MYFIRST)` e `MALLOC_DEFINE(M_MYFIRST, "myfirst", "myfirst driver data")`, usando `M_MYFIRST` no lugar. Por ora, `M_DEVBUF` é suficiente.

Os bits de flag mais relevantes neste estágio:

- `M_WAITOK`: a alocação pode dormir aguardando memória. No contexto do attach, essa é quase sempre a escolha certa.
- `M_NOWAIT`: não dorme; retorna `NULL` se a memória estiver escassa. Necessário quando você está em um contexto que não pode dormir (um interrupt handler, dentro de um lock que não admite sono).
- `M_ZERO`: zera a memória antes de retorná-la. Use em conjunto com `M_WAITOK` ou `M_NOWAIT` conforme o caso.

Uma chamada com `M_WAITOK` sem `M_NOWAIT` tem a garantia de retornar um ponteiro válido no FreeBSD 14.3. O kernel dormirá e poderá acionar a recuperação de memória se necessário, mas na prática não retornará `NULL`. Ainda assim, verificar se o retorno é `NULL` é uma prática defensiva que não tem custo nenhum; faremos isso.

### A Chamada Correspondente `free(9)`

Todo `malloc(9)` tem um `free(9)` correspondente. A assinatura é:

```c
free(sc->buf, M_DEVBUF);
```

O tipo malloc passado para `free` deve ser o mesmo passado para `malloc` para o mesmo ponteiro. Passar um tipo diferente corromperia a contabilidade interna do kernel e é um dos bugs que kernels compilados com `INVARIANTS` detectam em tempo de execução.

O lugar certo para o `free` depende de onde o `malloc` foi feito: o attach aloca, o detach libera. Se o attach falhar no meio do caminho, o caminho de desfazimento do erro libera tudo que foi alocado antes da falha. Vimos esse padrão no Capítulo 7; vamos reutilizá-lo aqui.

### Dimensionamento do Buffer

Escolher o tamanho do buffer é uma decisão de projeto. Para um driver de sala de aula, qualquer tamanho pequeno funciona. Algumas diretrizes:

- **Pequeno** (algumas centenas de bytes a alguns quilobytes): adequado para demonstração. Fácil de raciocinar. Cargas de trabalho maiores que o buffer produzirão `ENOSPC` ou leituras curtas rapidamente; isso é uma característica útil para o ensino, não um defeito.
- **Tamanho de página** (4096 bytes): um padrão comum e sensato. A alocação de memória é alinhada a páginas de graça, e muitas ferramentas tratam 4 KiB como uma unidade natural.
- **Maior** (muitos quilobytes a um megabyte): adequado para drivers que esperam armazenar muitos dados em buffer. Lembre-se de que a memória do kernel não é infinita; um driver descontrolado que aloca um megabyte por abertura pode desestabilizar o sistema.

Para o `myfirst` Stage 2, usaremos um buffer de 4096 bytes. É grande o suficiente para que um teste razoável (um parágrafo de texto, alguns inteiros) caiba, e pequeno o suficiente para que o comportamento de `ENOSPC` seja fácil de provocar a partir de um shell.

### Estouros de Buffer

O bug mais comum em um driver que gerencia seu próprio buffer é escrever além do final do buffer. Esse bug é absolutamente fatal no espaço do kernel. Um programa em espaço do usuário que ultrapassa um buffer pode corromper seu próprio heap; um módulo do kernel que faz o mesmo pode corromper a memória de outro subsistema, e o crash (ou, pior, o mau funcionamento silencioso) pode aparecer longe do ponto onde o bug está.

A defesa é disciplina aritmética. Toda vez que seu código estiver prestes a escrever `N` bytes a partir do offset `O` em um buffer de tamanho `S`, verifique que `O + N <= S` antes da escrita. No handler do Stage 3 apresentado acima, a expressão `towrite = MIN((size_t)uio->uio_resid, avail)` é exatamente essa verificação: `towrite` é limitado a `avail`, onde `avail` é `sc->buflen - sc->bufused`. Não há como exceder `sc->buflen`.

Um bug relacionado é a confusão entre tipos com e sem sinal. `uio_resid` é `ssize_t`; `sc->bufused` é `size_t`. Misturá-los de forma descuidada pode produzir um valor negativo que transborda quando convertido para `size_t`, com resultados catastróficos. O macro `MIN` e um cast explícito para `(size_t)` valem o pequeno ruído que acrescentam ao código.

### Considerações sobre Locking

Se o seu driver pode ser acessado por mais de um contexto de usuário simultaneamente, o buffer precisa de um lock. Dois escritores simultâneos podem disputar o `bufused`; dois leitores simultâneos podem disputar os offsets de leitura; um escritor e um leitor podem intercalar suas atualizações de estado de um modo que corrompe ambos.

No `myfirst`, o campo `struct mtx mtx` que carregamos desde o Capítulo 7 é o lock que usaremos. É um mutex `MTX_DEF` comum, o que significa que pode ser mantido durante uma chamada a `uiomove` (que pode dormir em uma falta de página). Vamos mantê-lo durante cada atualização de `bufused` e durante o `uiomove` que transfere bytes para dentro ou fora do buffer compartilhado.

A Parte 3 aprofunda bastante a estratégia de locking. Por ora, a regra é: **proteja qualquer campo que mais de um handler possa tocar ao mesmo tempo**. No Stage 3, esses campos são `sc->buf` e `sc->bufused`. Seu `fh` por abertura é por descritor; ele não precisa do mesmo lock, porque dois handlers não podem executar para o mesmo descritor simultaneamente nos casos que exercitaremos.

### Uma Prévia dos Buffers Circulares

O Capítulo 10 constrói um ring buffer propriamente dito: um buffer de tamanho fixo onde ponteiros `head` e `tail` se perseguem em círculo. Ele difere do buffer linear que estamos usando no Capítulo 9 em dois aspectos:

1. Ele não precisa ser zerado entre os usos. Os ponteiros dão a volta; o buffer é reutilizado no lugar.
2. Ele pode suportar streaming em regime permanente. Um buffer linear enche e então recusa escritas; um ring buffer mantém uma janela deslizante dos dados recentes.

O Stage 3 deste capítulo *não* implementa um ring. Ele implementa um buffer linear no qual `d_write` acrescenta dados e `d_read` drena. Quando o buffer está cheio, `d_write` retorna `ENOSPC`; quando está vazio, `d_read` retorna zero bytes. Isso é suficiente para acertar o caminho de I/O sem o controle contábil extra de um ring. O Capítulo 10 adiciona esse controle contábil sobre as mesmas formas de handler.

### Uma Nota sobre a Thread Safety do fh por Descritor

O `struct myfirst_fh` que seu driver aloca em `d_open` é por descritor. Dois handlers para o mesmo descritor não podem executar simultaneamente nos cenários exercitados neste capítulo (o kernel serializa operações por arquivo através do mecanismo de descritores de arquivo para os casos comuns), portanto os campos dentro de `fh` não precisam de seu próprio lock. Dois handlers para *descritores diferentes* executam de forma concorrente, mas tocam estruturas `fh` diferentes.

Esse é um invariante reconfortante, mas não absoluto. Um driver que arranjar para passar o ponteiro `fh` para uma thread do kernel que execute em paralelo com a syscall precisa adicionar sua própria sincronização. Não faremos isso neste capítulo; por ora, o `fh` é seguro enquanto você o tocar somente de dentro do handler que recebeu o descritor.

### Helpers do Kernel que Você Deve Reconhecer

Antes de seguirmos em frente, vale nomear o pequeno conjunto de macros e funções auxiliares que o capítulo tem usado. Eles estão definidos em headers padrão do FreeBSD, e iniciantes às vezes copiam código que os utiliza sem saber de onde vêm ou quais restrições se aplicam.

`MIN(a, b)` e `MAX(a, b)` estão disponíveis no código do kernel através de `<sys/libkern.h>`, que é incluído transitivamente por `<sys/systm.h>`. Eles avaliam cada argumento no máximo duas vezes, portanto `MIN(count++, limit)` é um bug: `count` é incrementado duas vezes. Um driver bem escrito evita efeitos colaterais dentro dos argumentos de `MIN`/`MAX`.

```c
towrite = MIN((size_t)uio->uio_resid, avail);
```

O cast explícito para `(size_t)` faz parte do padrão, não é um floreio estilístico. `uio_resid` é `ssize_t`, que é com sinal; `avail` é `size_t`, que é sem sinal. Sem o cast, o compilador escolhe um tipo para a comparação, e compiladores modernos emitem avisos quando tipos com sinal e sem sinal se encontram no mesmo `MIN`/`MAX`. O cast torna a intenção explícita: já verificamos que `uio_resid` é não negativo (o kernel garante isso), portanto o cast é seguro.

`howmany(x, d)`, definido em `<sys/param.h>`, computa `(x + d - 1) / d`. Use-o quando precisar de divisão com arredondamento para cima. Um driver que aloca páginas para armazenar uma contagem de bytes frequentemente escreve:

```c
npages = howmany(buflen, PAGE_SIZE);
```

`rounddown(x, y)` e `roundup(x, y)` alinham `x` para baixo ou para cima ao múltiplo mais próximo de `y`. `roundup2` e `rounddown2` são variantes mais rápidas que funcionam somente quando `y` é uma potência de dois. É assim que drivers alinham buffers a páginas ou alinham offsets a blocos.

`__DECONST(type, ptr)` remove `const` sem gerar aviso do compilador. É a forma educada de dizer ao compilador "sei que este ponteiro foi declarado `const`, mas verifiquei que a função que vou chamar não modificará os dados, então pare de reclamar". Usado em torno de `zero_region` no `zero_read` do `null.c`; nós o usamos no `myfirst_read` do Stage 1. Prefira-o a um cast simples para `(void *)`, porque ele sinaliza intenção.

`curthread` é um macro específico de arquitetura (resolvido através de um registrador por CPU) que aponta para a thread atualmente em execução. `uio->uio_td` normalmente é igual a `curthread` quando o uio veio de uma syscall; os dois são intercambiáveis nesse contexto, mas o valor carregado pelo uio é mais autodocumentado.

`bootverbose` é um inteiro que é definido como não zero se o kernel foi inicializado com `-v` ou se o operador o ativou via sysctl. Proteger linhas de log verbosas com `if (bootverbose)` é o idioma FreeBSD para logging de depuração que fica visível sob demanda, mas silencioso por padrão.

Reconhecer esses helpers quando você os encontrar em outros drivers reduz o tempo necessário para ler código desconhecido. Nenhum deles é exótico; todos fazem parte do vocabulário padrão que se espera que um contribuidor do kernel leia sem precisar consultar documentação.



## Tratamento de Erros e Casos Especiais

Um driver iniciante que "funciona no caminho feliz" é um driver que eventualmente vai travar o kernel. A parte interessante do tratamento de I/O são as partes que não são o caminho feliz: leituras de comprimento zero, escritas parciais, ponteiros de usuário inválidos, sinais entregues no meio de uma chamada, buffers esgotados e algumas dezenas de variações dessas situações. Esta seção percorre os casos comuns e os valores de errno que os acompanham.

### Os Valores de Errno que Importam para I/O

O FreeBSD tem um espaço de errno amplo. Apenas alguns são frequentes nos caminhos de I/O de drivers; conhecê-los bem é mais útil do que percorrer superficialmente a lista completa.

`0`: sucesso. Retorne este valor quando a transferência for concluída corretamente. A contagem de bytes está implícita em `uio_resid`.

`ENXIO` ("Device not configured"): a operação não pode prosseguir porque o dispositivo não está em um estado utilizável. Retorne-o de `d_open`, `d_read` ou `d_write` se o softc estiver ausente, `is_attached` for false ou o driver tiver sido instruído a encerrar. É o erro idiomático de "o cdev existe, mas o dispositivo subjacente não".

`EFAULT` ("Bad address"): um ponteiro de usuário era inválido. Raramente você retorna esse erro diretamente; `uiomove(9)` o retorna em seu nome quando uma operação `copyin`/`copyout` falha. Propague-o retornando qualquer erro que `uiomove` produziu.

`EINVAL` ("Invalid argument"): algum parâmetro não faz sentido. Para uma leitura ou escrita, isso geralmente é um offset fora do intervalo (se o seu driver respeitar offsets) ou uma requisição malformada. Evite usá-lo como um catch-all genérico.

`EAGAIN` ("Resource temporarily unavailable"): a operação bloquearia, mas `O_NONBLOCK` estava definido. Para um `d_read` que não tem dados, esta é a resposta correta no modo não bloqueante. Para um `d_write` que não tem espaço, a mesma lógica se aplica. Trataremos isso no Stage 3.

`EINTR` ("Interrupted system call"): um sinal foi entregue enquanto a thread estava bloqueada dentro do driver. Seu `d_read` pode retornar `EINTR` se um sleep foi interrompido por um sinal. O kernel então ou tentará novamente a syscall de forma transparente (dependendo do flag `SA_RESTART`) ou retornará ao userland com `errno = EINTR`. Veremos o tratamento de `EINTR` no Capítulo 10; o Capítulo 9 não bloqueia e, portanto, não produz `EINTR`.

`EIO` ("Input/output error"): um catch-all para erros de hardware. Use-o quando o seu driver se comunica com hardware real e o hardware reporta uma falha. Raro no `myfirst`, que não tem hardware.

`ENOSPC` ("No space left on device"): o buffer do driver está cheio e não consegue aceitar mais dados. A resposta correta para uma escrita quando não há espaço disponível. O Stage 3 retorna esse erro.

`EPIPE` ("Broken pipe"): usado por drivers semelhantes a pipes quando a outra extremidade fechou a conexão. Não é relevante para o `myfirst`.

`ERANGE`, `EOVERFLOW`, `EMSGSIZE`: menos comuns em drivers de caracteres; aparecem quando o kernel ou o driver quer dizer "o número que você pediu está fora do intervalo". Não os usaremos neste capítulo.

### Fim de Arquivo em uma Leitura

Por convenção, uma leitura que retorna zero bytes (porque `uiomove` não moveu nada e seu handler retornou zero) é interpretada pelo chamador como fim de arquivo. Shells, `cat`, `head`, `tail`, `dd` e a maioria das outras ferramentas do sistema base dependem dessa convenção.

A implicação para o seu `d_read`: quando o driver não tem mais nada a entregar, retorne zero. Não retorne um errno. `uio_resid` deve ainda ter seu valor original, porque nenhum byte foi movido.

No Stage 1 e Stage 2 do `myfirst`, o fim de arquivo ocorre quando o offset de leitura por descritor alcança o comprimento do buffer. `uiomove_frombuf` retorna zero nesse caso de forma natural, portanto não precisamos de um caminho de código especial.

No Estágio 3, onde `d_read` esvazia um buffer no qual `d_write` acrescenta dados, o comportamento de EOF é mais sutil: "sem dados agora" não é o mesmo que "nunca mais haverá dados". Reportaremos "sem dados agora" como uma leitura de zero bytes. Um programa de usuário mais cuidadoso pode interpretar isso como EOF e parar; um menos cuidadoso vai continuar em loop e chamar `read(2)` novamente. O Capítulo 10 apresenta as estratégias corretas de leitura bloqueante ou de `poll(2)` que permitem a um programa de usuário aguardar mais dados sem ficar em espera ativa.

### Leituras e Escritas de Comprimento Zero

Uma requisição de comprimento zero (`read(fd, buf, 0)` ou `write(fd, buf, 0)`) é legal. Significa "não faça nada, mas me diga se você poderia ter feito algo". O kernel cuida da maior parte do despacho: se `uio_resid` é zero na entrada, qualquer chamada a `uiomove` é uma operação nula, e seu handler retorna zero. O chamador vê uma transferência de zero bytes e nenhum erro.

Dois pontos sutis. Primeiro, não trate `uio_resid == 0` como uma condição de erro. Não é. É uma requisição legítima. Segundo, não assuma que `uio_resid == 0` significa fim de arquivo; significa apenas que o chamador pediu zero bytes. EOF diz respeito ao driver ter ficado sem dados, não ao chamador não ter pedido nenhum.

### Transferências Parciais

Uma leitura parcial é uma leitura que retorna menos bytes do que foram solicitados. Uma escrita parcial é uma escrita que consumiu menos bytes do que foram oferecidos. Ambas são legais e esperadas na I/O do UNIX; programas de usuário bem escritos as tratam com um loop.

Seu driver é o árbitro final de quanto transferir. A família de funções `uiomove` transfere no máximo `MIN(user_request, driver_offer)` bytes por chamada. Se seu código chama `uiomove(buf, 128, uio)` e o usuário pediu 1024, o kernel transfere 128 e deixa 896 em `uio_resid`. O chamador recebe um retorno de 128 bytes do `read(2)`.

Um programa de usuário mal comportado que não faz loop sobre I/O parcial vai perder bytes. Isso não é problema do seu driver; o UNIX funciona assim desde 1971. Um driver bem comportado é aquele que retorna contagens de bytes honestas (por meio de `uio_resid`) e valores de errno previsíveis, mesmo quando transferências parciais ocorrem.

### Tratando EFAULT de uiomove

Quando `uiomove(9)` retorna um erro diferente de zero, o valor mais comum é `EFAULT`. Quando você o recebe, o kernel já:

- Instalou um handler de falha ao redor da cópia.
- Observou a falha.
- Desfez a cópia parcial.
- Retornou `EFAULT` ao chamador de `uiomove`.

Seu handler tem duas opções de resposta:

1. **Propagar o erro**. Retorne `EFAULT` (ou qualquer errno que foi retornado) de `d_read` / `d_write`. Esta é a opção mais simples e quase sempre correta.
2. **Ajustar o estado do driver e retornar sucesso**. Se alguns bytes foram movidos antes da falha, `uio_resid` pode já ter diminuído. O kernel reportará esse sucesso parcial ao espaço do usuário. Pode ser que você queira atualizar algum contador interno do driver que reflita até onde a transferência chegou.

Na prática, a opção 1 é a resposta universal, a menos que você tenha um motivo específico para fazer mais. A opção 2 adiciona complexidade que raramente vale a pena.

### Programação Defensiva para Entrada do Usuário

Cada byte que um usuário escreve no seu dispositivo não é confiável. Isso parece dramático; também é literalmente verdade. Um módulo do kernel que interpreta uma escrita do usuário como uma estrutura e desreferencia um ponteiro dessa estrutura é um módulo do kernel com uma vulnerabilidade trivial de escrita arbitrária na memória.

A regra geral: **trate os bytes no seu buffer como dados arbitrários, não como uma estrutura tipada, a menos que você tenha escolhido deliberadamente um formato de transferência que você valida em cada fronteira**. Para o `myfirst` isso é fácil, porque nunca interpretamos os bytes; eles são payload. Para drivers que expõem uma interface de escrita estruturada (por exemplo, um driver que permite aos usuários configurar o comportamento por meio de escritas), o caminho defensivo é:

- Validar o tamanho da escrita em relação ao tamanho esperado da mensagem.
- Copiar os bytes para uma estrutura no espaço do kernel (não para um ponteiro do usuário).
- Validar cada campo dessa estrutura antes de agir sobre ele.
- Nunca armazenar um ponteiro do usuário dentro do seu driver para uso posterior.

Essas regras são mais fáceis de seguir do que parecem, mas também são fáceis de violar sem perceber. O Capítulo 25 as revisita quando analisamos o design de `ioctl`. Por ora, a exigência é baixa: seu driver `myfirst` deve copiar bytes por meio de `uiomove`, não interpretá-los.

### Registrando Erros versus Falhas Silenciosas

Quando um handler retorna um errno, o erro se propaga ao espaço do usuário como o valor de `errno` da syscall que falhou. A maioria dos programas de usuário o verá ali e o reportará. Alguns vão ignorá-lo.

Durante o desenvolvimento de drivers, ajuda também registrar erros significativos no `dmesg`. `device_printf(9)` é a ferramenta certa, porque ela prefixo cada linha com o nome do dispositivo Newbus, permitindo identificar qual instância produziu a mensagem. Um exemplo do Stage 3:

```c
if (avail == 0) {
        mtx_unlock(&sc->mtx);
        if (bootverbose)
                device_printf(sc->dev, "write rejected: buffer full\n");
        return (ENOSPC);
}
```

A guarda `if (bootverbose)` é um idioma comum do FreeBSD para logging detalhado: ela só imprime se o kernel foi inicializado com a flag `-v` ou se o sysctl `bootverbose` foi ativado, o que mantém os logs de produção silenciosos enquanto ainda oferece aos desenvolvedores uma forma de ver os detalhes.

Não registre cada erro em cada chamada; isso produz spam no log, o que torna problemas reais mais difíceis de encontrar. Registre a primeira ocorrência de uma condição, ou registre periodicamente, ou registre apenas sob `bootverbose`. A escolha depende do driver. Para o `myfirst`, um único log por transição (buffer vazio, buffer cheio) é suficiente.

### Previsibilidade e Amigabilidade ao Usuário

Um iniciante escrevendo um driver geralmente se concentra em tornar o caminho feliz rápido. Um autor de driver mais experiente se concentra em tornar os caminhos de erro previsíveis. A diferença é esta: quando um operador usa seu driver e algo quebra, o valor de errno, a mensagem de log e a reação no espaço do usuário precisam formar uma história clara. Se `read(2)` retorna `-1` com `errno = EIO` e os logs estão silenciosos, o operador não tem por onde começar. Se os logs dizem "myfirst0: read failed, device detached" e o usuário recebe `ENXIO`, a história se conta sozinha.

Busque isso. Retorne o errno correto. Registre a causa subjacente uma vez. Seja honesto com transferências parciais. Nunca descarte dados silenciosamente.

### Uma Tabela Resumida de Convenções

| Situação em d_read                                            | Retorno       |
|-------------------------------------------------|---------------|
| Sem dados para entregar, mais podem chegar depois             | `0` com `uio_resid` inalterado |
| Sem dados, nunca haverá mais (EOF)                            | `0` com `uio_resid` inalterado |
| Alguns dados entregues, outros não                            | `0` com `uio_resid` refletindo o restante |
| Entrega completa                                              | `0` com `uio_resid = 0` |
| Ponteiro do usuário inválido                                  | `EFAULT` (de `uiomove`) |
| Dispositivo não pronto / sendo desconectado                   | `ENXIO` |
| Não bloqueante, bloquearia                                    | `EAGAIN` |
| Erro de hardware                                              | `EIO` |

| Situação em d_write                                           | Retorno       |
|-------------------------------------------------|---------------|
| Aceitação completa                                            | `0` com `uio_resid = 0` |
| Aceitação parcial                                             | `0` com `uio_resid` refletindo o restante |
| Sem espaço, bloquearia                                        | `EAGAIN` (não bloqueante) ou sleep (bloqueante) |
| Sem espaço, permanente                                        | `ENOSPC` |
| Ponteiro inválido                                             | `EFAULT` (de `uiomove`) |
| Dispositivo não pronto                                        | `ENXIO` |
| Erro de hardware                                              | `EIO` |

Ambas as tabelas são curtas de propósito. A maioria dos drivers usa apenas quatro ou cinco valores de errno no total. Quanto mais clara for sua história de erros, melhor será o seu driver para se trabalhar.



## Evoluindo Seu Driver: Os Três Estágios

Com a teoria estabelecida, passamos ao código. Esta seção percorre três estágios do `myfirst`, cada um pequeno, cada um um driver completo que carrega, executa e exercita um padrão específico de I/O.

Os estágios são projetados para se apoiarem mutuamente:

- **Stage 1** adiciona um caminho de leitura que serve uma mensagem fixa no espaço do kernel. Este é o `myfirst_read` em sua forma mais simples.
- **Stage 2** adiciona um caminho de escrita que deposita dados do usuário em um buffer do kernel, e um caminho de leitura que lê desse mesmo buffer. O buffer é dimensionado no momento do attach e não faz wraparound.
- **Stage 3** transforma o Stage 2 em um buffer FIFO (primeiro a entrar, primeiro a sair), de modo que escritas adicionam ao final e leituras drenam do início, e o driver pode servir um fluxo contínuo (embora finito).

Os três estágios partem do código do Stage 2 do Capítulo 8. O sistema de build (`Makefile`) não muda. Os handlers `attach` e `detach` crescem levemente a cada estágio. A forma do `cdevsw`, os métodos Newbus, o encanamento `fh` por abertura e a árvore de sysctl permanecem os mesmos. Você passará a maior parte do tempo observando `d_read` e `d_write`.

### Stage 1: Um Leitor de Mensagem Estática

O driver do Stage 1 mantém uma mensagem fixa na memória do kernel e a serve aos leitores. `d_read` usa `uiomove_frombuf(9)` para entregar a mensagem. `d_write` permanece um stub: retorna sucesso sem consumir nenhum byte. Este estágio é a ponte do stub do Capítulo 8 para um leitor real; ele apresenta `uiomove_frombuf` no contexto mais simples possível.

Adicione um par de campos ao softc para guardar a mensagem e seu comprimento, e um offset por descritor ao `fh`:

```c
struct myfirst_softc {
        /* ...existing Chapter 8 fields... */

        const char *message;
        size_t      message_len;
};

struct myfirst_fh {
        struct myfirst_softc *sc;
        uint64_t              reads;
        uint64_t              writes;
        off_t                 read_off;
};
```

Em `myfirst_attach`, inicialize a mensagem:

```c
static const char myfirst_message[] =
    "Hello from myfirst.\n"
    "This is your first real read path.\n"
    "Chapter 9, Stage 1.\n";

sc->message = myfirst_message;
sc->message_len = sizeof(myfirst_message) - 1;
```

Observe o `- 1`: não queremos servir o byte NUL terminador ao espaço do usuário. Arquivos de texto não carregam um NUL no final, e um dispositivo que se comporta como um também não deveria.

O novo `myfirst_read`:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        before = uio->uio_offset;
        error = uiomove_frombuf(__DECONST(void *, sc->message),
            sc->message_len, uio);
        if (error == 0)
                fh->reads += (uio->uio_offset - before);
        fh->read_off = uio->uio_offset;
        return (error);
}
```

Dois detalhes merecem atenção.

Primeiro, `uio->uio_offset` é a posição por descritor no fluxo. O kernel a mantém entre chamadas, avançando-a conforme `uiomove_frombuf` move bytes. O primeiro `read(2)` em um descritor recém-aberto começa no offset zero; cada `read(2)` subsequente começa onde o anterior terminou. Quando o offset atinge `sc->message_len`, `uiomove_frombuf` retorna zero sem mover nenhum byte, e o chamador vê EOF.

Segundo, `before` captura `uio->uio_offset` na entrada para que possamos calcular quantos bytes foram movidos. Após `uiomove_frombuf` retornar, a diferença é o tamanho da transferência, e a adicionamos ao contador `reads` por descritor. É aqui que o campo `fh->reads` do Capítulo 8 finalmente cumpre sua função.

O cast `__DECONST` é um idioma do FreeBSD para remover `const`. `uiomove_frombuf` recebe um `void *` não-`const` porque está preparado para mover em qualquer direção, mas neste contexto sabemos que a direção é kernel-para-usuário (uma leitura), portanto sabemos que o buffer do kernel não será modificado. Remover o `const` aqui é seguro; usar um cast simples `(void *)` funcionaria igualmente, mas é menos autodocumentado.

`myfirst_write` permanece como o Capítulo 8 o deixou para o Stage 1:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_fh *fh;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);

        (void)fh;
        uio->uio_resid = 0;
        return (0);
}
```

As escritas são aceitas e descartadas, no estilo do `/dev/null`. O Stage 2 mudará isso.

Compile e carregue. Um teste rápido a partir do userland:

```sh
% cat /dev/myfirst/0
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
%
```

Uma segunda leitura do mesmo descritor retorna EOF, porque o offset já passou do final da mensagem:

```sh
% cat /dev/myfirst/0 /dev/myfirst/0
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
Hello from myfirst.
This is your first real read path.
Chapter 9, Stage 1.
```

Atenção: `cat` lê a mensagem duas vezes. Isso ocorre porque `cat` abre o arquivo duas vezes (uma por argumento) e cada abertura recebe um descritor novo com seu próprio `uio_offset`. Se quiser verificar que duas aberturas realmente enxergam offsets independentes, abra o dispositivo a partir de um pequeno programa C e leia mais de uma vez pelo mesmo descritor:

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int
main(void)
{
        int fd = open("/dev/myfirst/0", O_RDONLY);
        if (fd < 0) { perror("open"); return 1; }
        char buf[64];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
                fwrite(buf, 1, n, stdout);
        }
        close(fd);
        return 0;
}
```

O primeiro `read(2)` retorna a mensagem; o segundo retorna zero (EOF); o programa termina. Isso confirma que `uio_offset` está sendo mantido por descritor.

O Stage 1 é intencionalmente curto. Ele apresenta três ideias (o helper `uiomove_frombuf`, os offsets por descritor, o idioma `__DECONST`) sem sobrecarregar o leitor. O restante do capítulo constrói sobre isso.

### Stage 2: Um Buffer de Escrita-Única / Leitura-Múltipla

O Stage 2 estende o driver para aceitar escritas. O driver aloca um buffer no kernel no momento do attach, escritas depositam nele, leituras entregam a partir dele. Não há wraparound: uma vez que o buffer está cheio, novas escritas retornam `ENOSPC`. As leituras enxergam o que foi escrito até o momento, a partir do seu próprio offset por descritor.

A forma do `myfirst_softc` cresce com alguns campos:

```c
struct myfirst_softc {
        /* ...existing Chapter 8 fields... */

        char    *buf;
        size_t   buflen;
        size_t   bufused;

        uint64_t bytes_read;
        uint64_t bytes_written;
};
```

`buf` é o ponteiro retornado por `malloc(9)`. `buflen` é seu tamanho, uma constante de compilação por simplicidade; você pode torná-lo configurável depois. `bufused` é a marca d'água máxima: o número de bytes que foram escritos até o momento.

Dois novos nós sysctl para observabilidade:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bytes_written", CTLFLAG_RD,
    &sc->bytes_written, 0, "Total bytes written into the buffer");

SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
    OID_AUTO, "bufused", CTLFLAG_RD,
    &sc->bufused, 0, "Current byte count in the buffer");
```

`bufused` é um `size_t`, e a macro sysctl para inteiro sem sinal é `SYSCTL_ADD_UINT` em plataformas de 32 bits ou `SYSCTL_ADD_U64` em plataformas de 64 bits. Como este driver tem como alvo o FreeBSD 14.3 em amd64 no laboratório típico, `SYSCTL_ADD_UINT` é suficiente; o campo será apresentado como `unsigned int` mesmo que o tipo interno seja `size_t`. Se você tiver como alvo arm64 ou outra plataforma de 64 bits, use `SYSCTL_ADD_U64` e faça o cast adequado.

Aloque o buffer no `attach`:

```c
#define MYFIRST_BUFSIZE 4096

sc->buflen = MYFIRST_BUFSIZE;
sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
if (sc->buf == NULL) {
        error = ENOMEM;
        goto fail_mtx;
}
sc->bufused = 0;
```

Libere-o no `detach`:

```c
if (sc->buf != NULL) {
        free(sc->buf, M_DEVBUF);
        sc->buf = NULL;
}
```

Ajuste o tratamento de erro no `attach` para incluir a liberação do buffer:

```c
fail_dev:
        if (sc->cdev_alias != NULL) {
                destroy_dev(sc->cdev_alias);
                sc->cdev_alias = NULL;
        }
        destroy_dev(sc->cdev);
        sysctl_ctx_free(&sc->sysctl_ctx);
        free(sc->buf, M_DEVBUF);
        sc->buf = NULL;
fail_mtx:
        mtx_destroy(&sc->mtx);
        sc->is_attached = 0;
        return (error);
```

Agora o handler de leitura:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        off_t before;
        size_t have;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        have = sc->bufused;
        before = uio->uio_offset;
        error = uiomove_frombuf(sc->buf, have, uio);
        if (error == 0) {
                sc->bytes_read += (uio->uio_offset - before);
                fh->reads += (uio->uio_offset - before);
        }
        fh->read_off = uio->uio_offset;
        mtx_unlock(&sc->mtx);
        return (error);
}
```

O handler de leitura adquire o mutex para ler `bufused` de forma consistente e, em seguida, chama `uiomove_frombuf` com o valor atual do high-water mark como tamanho efetivo do buffer. Um leitor que seja executado antes de qualquer escrita verá `have = 0` e `uiomove_frombuf` retornará zero, o que o chamador interpreta como EOF. Um leitor que seja executado após algumas escritas verá o `bufused` atual e receberá até aquela quantidade de bytes.

O handler de escrita:

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        avail = sc->buflen - sc->bufused;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + sc->bufused, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                sc->bytes_written += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

Observe o limitador: `towrite = MIN(uio->uio_resid, avail)`. Se o usuário solicitou escrever 8 KiB e temos 512 bytes disponíveis, aceitamos 512 bytes e deixamos o kernel reportar uma escrita parcial de 512 de volta ao espaço do usuário. Um chamador bem comportado fará um loop com os bytes restantes; um chamador menos disciplinado perderá o excedente. Essa é responsabilidade do chamador; o driver cumpriu sua parte honestamente.

Teste rápido a partir do espaço do usuário:

```sh
% sudo kldload ./myfirst.ko
% echo "hello" | sudo tee /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
hello
% echo "more" | sudo tee -a /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
hello
more
% sysctl dev.myfirst.0.stats.bufused
dev.myfirst.0.stats.bufused: 11
%
```

O buffer cresceu 6 bytes com `"hello\n"`, depois mais 5 com `"more\n"`, totalizando 11 bytes. `cat` lê todos os 11 bytes de volta. Um segundo `cat` a partir de uma nova abertura começa no offset zero e os lê novamente.

O que acontece se escrevermos mais do que o buffer pode comportar?

```sh
% dd if=/dev/zero bs=1024 count=8 | sudo tee /dev/myfirst/0 > /dev/null
dd: stdout: No space left on device
tee: /dev/myfirst/0: No space left on device
8+0 records in
7+0 records out
```

`dd` escreveu 7 blocos de 1024 bytes antes que o 8º falhasse. `tee` reporta o erro. O driver aceitou dados até o seu limite e então retornou `ENOSPC` de forma limpa. O kernel propagou o valor de errno de volta ao espaço do usuário.

### Estágio 3: Um Driver Echo FIFO

O Estágio 3 transforma o buffer em um FIFO. Escritas adicionam dados ao final. Leituras drenam a partir do início. Quando o buffer está vazio, leituras retornam zero bytes (EOF em buffer vazio). Quando o buffer está cheio, escritas retornam `ENOSPC`.

O buffer permanece linear: sem wrap-around. Após uma leitura que esvazia todos os dados, `bufused` é zero, e a próxima escrita começa novamente no offset zero em `sc->buf`. Isso mantém a contabilidade mínima e concentra a etapa na mudança de direção de I/O, em vez de na mecânica de ring buffer.

O softc ganha mais um campo:

```c
struct myfirst_softc {
        /* ...existing fields... */

        size_t  bufhead;   /* index of next byte to read */
        size_t  bufused;   /* bytes in the buffer, from bufhead onward */

        /* ...remaining fields... */
};
```

`bufhead` é o offset do primeiro byte ainda a ser lido. `bufused` é o número de bytes válidos a partir de `bufhead`. O invariante `bufhead + bufused <= buflen` sempre se mantém.

Reinicialize ambos em `attach`:

```c
sc->bufhead = 0;
sc->bufused = 0;
```

Novo handler de leitura:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t toread;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        if (sc->bufused == 0) {
                mtx_unlock(&sc->mtx);
                return (0); /* EOF-on-empty */
        }
        toread = MIN((size_t)uio->uio_resid, sc->bufused);
        error = uiomove(sc->buf + sc->bufhead, toread, uio);
        if (error == 0) {
                sc->bufhead += toread;
                sc->bufused -= toread;
                sc->bytes_read += toread;
                fh->reads += toread;
                if (sc->bufused == 0)
                        sc->bufhead = 0;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

Alguns detalhes diferem do Estágio 2. A leitura não respeita mais `uio->uio_offset`; o offset por descritor não tem significado em um FIFO onde cada descritor enxerga o mesmo stream e o stream desaparece conforme é consumido. Quando `bufused` chega a zero, redefinimos `bufhead` para zero, o que mantém a próxima escrita alinhada ao início do buffer e evita empurrar dados em direção ao final.

Esse truque de "colapso no esvaziamento" não é um ring buffer, mas é suficientemente próximo para um FIFO pedagógico. O passo extra de realinhamento é `O(1)`; custa praticamente nada.

Novo handler de escrita (basicamente igual ao Estágio 2, mas observe onde ocorre o append):

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        size_t avail, tail, towrite;
        int error;

        error = devfs_get_cdevpriv((void **)&fh);
        if (error != 0)
                return (error);
        if (sc == NULL || !sc->is_attached)
                return (ENXIO);

        mtx_lock(&sc->mtx);
        tail = sc->bufhead + sc->bufused;
        avail = sc->buflen - tail;
        if (avail == 0) {
                mtx_unlock(&sc->mtx);
                return (ENOSPC);
        }
        towrite = MIN((size_t)uio->uio_resid, avail);
        error = uiomove(sc->buf + tail, towrite, uio);
        if (error == 0) {
                sc->bufused += towrite;
                sc->bytes_written += towrite;
                fh->writes += towrite;
        }
        mtx_unlock(&sc->mtx);
        return (error);
}
```

A escrita faz append em `sc->bufhead + sc->bufused`, não em `sc->bufused` sozinho, porque a fatia de dados válidos se deslocou conforme as leituras a esvaziaram.

Teste de fumaça:

```sh
% echo "one" | sudo tee /dev/myfirst/0 > /dev/null
% echo "two" | sudo tee -a /dev/myfirst/0 > /dev/null
% cat /dev/myfirst/0
one
two
% cat /dev/myfirst/0
%
```

Após o primeiro `cat`, o buffer está vazio. O segundo `cat` não encontra dados e encerra imediatamente.

Esse é o formato do Estágio 3. O driver é um FIFO em memória simples e honesto. Os usuários podem inserir bytes, retirá-los e observar os contadores via sysctl. Isso é I/O real, e é o ponto de partida a partir do qual o Capítulo 10 se desenvolve.



## Rastreando um `read(2)` do Espaço do Usuário até Seu Handler

Antes de começar a trabalhar nos laboratórios, veja passo a passo o que acontece quando um programa de usuário chama `read(2)` em um de seus nós. Compreender esse caminho é uma daquelas coisas que mudam a forma como você lê código de driver. Todo handler que você vê na árvore está posicionado no fundo da cadeia de chamadas descrita abaixo; uma vez que você reconhece a cadeia, cada handler começa a parecer familiar.

### Passo 1: O Programa de Usuário Chama `read(2)`

O wrapper `read` da biblioteca C é uma fina tradução da chamada em um trap de syscall: ele coloca o descritor de arquivo, o ponteiro para o buffer e o contador nos registradores apropriados e executa a instrução de trap para a arquitetura atual. O controle é transferido ao kernel.

Essa parte não tem nada a ver com drivers. É a mesma para toda syscall. O que importa é que o kernel está agora executando em nome do processo de usuário, no espaço de endereçamento do kernel, com os registradores do usuário salvos e as credenciais do processo visíveis por meio de `curthread->td_ucred`.

### Passo 2: O Kernel Busca o Descritor de Arquivo

O kernel chama `sys_read(2)` (em `/usr/src/sys/kern/sys_generic.c`), que valida os argumentos, busca o descritor de arquivo na tabela de arquivos do processo chamante e adquire uma referência no `struct file` resultante.

Se o descritor não estiver aberto, a chamada falha aqui com `EBADF`. Se o descritor estiver aberto, mas não for legível (por exemplo, o usuário abriu o dispositivo com `O_WRONLY`), a chamada também falha com `EBADF`. O driver não está envolvido; `sys_read` impõe o modo de acesso.

### Passo 3: O Vetor de Operações de Arquivo Despacha

O `struct file` tem uma tag de tipo de arquivo (`f_type`) e um vetor de operações de arquivo (`f_ops`). Para um arquivo comum, o vetor despacha para a camada VFS; para um socket, despacha para a camada de sockets; para um dispositivo aberto por meio do devfs, despacha para `vn_read`, que por sua vez chama a operação de vnode `VOP_READ` no vnode por trás do arquivo.

Isso pode parecer indireção pela indireção. Na realidade, é assim que o kernel mantém o restante do caminho da syscall idêntico para todo tipo de arquivo. Os drivers não precisam saber sobre essa camada; devfs e VFS entregam a chamada ao seu handler eventualmente.

### Passo 4: O VFS Invoca o devfs

As operações de sistema de arquivos do vnode apontam para a implementação do devfs da interface de vnode (`devfs_vnops`). `VOP_READ` em um vnode do devfs chama `devfs_read_f`, que inspeciona o cdev por trás do vnode, adquire uma referência de contagem de thread nele (incrementando `si_threadcount`) e chama `cdevsw->d_read`. Essa é a sua função.

Dois detalhes desse passo têm implicações para o seu driver.

Primeiro, **o incremento de `si_threadcount` é o que `destroy_dev(9)` usa para saber que seu handler está ativo**. Quando um módulo é descarregado e `destroy_dev` é executado, ele aguarda até que toda invocação atual de cada handler retorne. A referência é incrementada antes de seu `d_read` ser chamado e liberada após seu retorno. Esse mecanismo é o motivo pelo qual seu driver pode ser descarregado com segurança enquanto um usuário está no meio de um `read(2)`.

Segundo, **a chamada é síncrona do ponto de vista da camada VFS**. O VFS chama seu handler, aguarda seu retorno e então propaga o resultado. Você não precisa fazer nada especial para participar dessa sincronização; basta retornar do seu handler quando terminar.

### Passo 5: Seu Handler `d_read` É Executado

É aqui que estivemos durante todo o capítulo. O handler:

- Recebe um `struct cdev *dev` (o nó sendo lido), um `struct uio *uio` (a descrição de I/O) e um `int ioflag` (flags da entrada da tabela de arquivos).
- Recupera o estado por-open via `devfs_get_cdevpriv(9)`.
- Verifica se o dispositivo está ativo.
- Transfere bytes por meio de `uiomove(9)`.
- Retorna zero ou um código de erro (errno).

Nada sobre esse passo deve ser misterioso a esta altura.

### Passo 6: O Kernel Retorna pela Cadeia e Reporta

`devfs_read_f` observa o valor de retorno do seu handler. Se for zero, calcula a contagem de bytes a partir da diminuição em `uio->uio_resid` e retorna essa contagem. Se não for zero, converte o errno no retorno de erro da syscall. `vn_read` do VFS passa o resultado para `sys_read`. `sys_read` grava o resultado no registrador de valor de retorno.

O controle retorna ao espaço do usuário. O wrapper `read` da biblioteca C examina o resultado: um valor positivo é retornado como o valor de retorno de `read(2)`; um valor negativo define `errno` e retorna `-1`.

O programa de usuário vê o inteiro que esperava, e seu fluxo de controle continua.

### Passo 7: Os Contadores de Referência São Decrementados

Na saída, `devfs_read_f` libera a referência de contagem de thread no cdev. Se `destroy_dev(9)` estava aguardando `si_threadcount` chegar a zero, ele pode agora prosseguir com a desmontagem.

É por isso que toda a cadeia é estruturada com tanto cuidado. Cada referência é pareada; cada incremento tem um decremento correspondente; cada pedaço de estado que o handler toca é de propriedade do handler, do softc ou do `fh` por-open. Se qualquer um desses invariantes for violado, o descarregamento se torna inseguro.

### Por Que Esse Rastreamento É Importante para Você

Três conclusões.

**A primeira**: o mecanismo acima é o motivo pelo qual seu handler não precisa fazer nada especial para coexistir com o descarregamento do módulo. Desde que você retorne de `d_read` em tempo finito, o kernel permitirá que seu driver seja descarregado limpo. Isso é parte do motivo pelo qual o Capítulo 9 mantém todas as leituras não bloqueantes no nível do driver.

**A segunda**: cada camada entre `read(2)` e seu handler é configurada pelo kernel antes que seu código execute. O buffer do usuário é válido (ou `uiomove` reportará `EFAULT`), o cdev está ativo (ou devfs teria recusado a chamada), o modo de acesso é compatível com o descritor (ou `sys_read` teria recusado) e as credenciais do processo são as da thread atual. Você pode se concentrar no trabalho do seu driver e confiar nas camadas.

**A terceira**: quando você lê um driver desconhecido na árvore e seu `d_read` parece estranho, você pode percorrer a cadeia em sentido inverso. Quem chamou esse handler? Que estado eles prepararam? Quais invariantes meu handler promete ao retornar? A cadeia informa. As respostas são geralmente as mesmas que para `myfirst`.

### O Espelho: Rastreando um `write(2)`

Uma escrita segue o mesmo tipo de cadeia, espelhada. Um detalhamento completo de sete passos seria basicamente uma reafirmação do rastreamento de leitura com palavras substituídas, portanto o parágrafo abaixo é deliberadamente comprimido.

O usuário chama `write(fd, buf, 1024)`. A biblioteca C faz trap no kernel. `sys_write(2)` em `/usr/src/sys/kern/sys_generic.c` valida os argumentos, busca o descritor e adquire uma referência em seu `struct file`. O vetor de operações de arquivo despacha para `vn_write`, que chama `VOP_WRITE` no vnode do devfs. `devfs_write_f` em `/usr/src/sys/fs/devfs/devfs_vnops.c` adquire a referência de contagem de thread no cdev, compõe o `ioflag` a partir de `fp->f_flag` e chama `cdevsw->d_write` com o uio descrevendo o buffer do chamante.

Seu handler `d_write` é executado. Ele recupera o estado por-open via `devfs_get_cdevpriv(9)`, verifica se o dispositivo está ativo, adquire o lock que o driver precisa em torno do buffer, limita o comprimento da transferência ao espaço disponível e chama `uiomove(9)` para copiar bytes do espaço do usuário para o buffer do kernel. Em caso de sucesso, o handler atualiza sua contabilidade e retorna zero. `devfs_write_f` libera a referência de contagem de thread. `vn_write` retorna por meio de `sys_write`, que calcula a contagem de bytes a partir da diminuição em `uio_resid` e a retorna. O usuário vê o valor de retorno de `write(2)`.

Três coisas diferem da cadeia de leitura de forma substancial.

**Primeiro, o kernel executa `copyin` dentro de `uiomove` em vez de `copyout`.** Mesmo mecanismo, direção oposta. O tratamento de falhas é idêntico: um ponteiro de usuário inválido retorna `EFAULT`, uma cópia incompleta deixa `uio_resid` consistente com o que foi transferido, e o handler simplesmente propaga o código de erro.

**Segundo, `ioflag` carrega `IO_NDELAY` da mesma forma, mas a interpretação do driver é diferente.** Em uma leitura, não bloqueante significa "retornar `EAGAIN` se não há dados". Em uma escrita, não bloqueante significa "retornar `EAGAIN` se não há espaço". Condições simétricas, valores de errno simétricos.

**Terceiro, as atualizações de `atime` / `mtime` são específicas para cada direção.** `devfs_read_f` atualiza `si_atime` se bytes foram transferidos; `devfs_write_f` atualiza `si_mtime` (e `si_ctime` em alguns caminhos) se bytes foram transferidos. São esses os valores que `stat(2)` no nó reporta, e o motivo pelo qual `ls -lu /dev/myfirst/0` mostra timestamps diferentes para leituras versus escritas. Seu driver não gerencia esses campos; o devfs gerencia.

Uma vez que você reconhece os rastreamentos de leitura e escrita como imagens espelhadas, você internalizou a maior parte do caminho de despacho de dispositivos de caracteres. Cada capítulo a partir daqui adicionará hooks (um `d_poll`, um `d_kqfilter`, um `d_ioctl`, um caminho `mmap`) que se situam na mesma cadeia em posições ligeiramente diferentes. A cadeia em si permanece constante.



## Fluxo de Trabalho Prático: Testando Seu Driver a Partir do Shell

As ferramentas do sistema base são seu primeiro e melhor ambiente de testes. Esta seção é um guia prático de como usá-las bem em um driver que você está desenvolvendo. Nenhum dos comandos abaixo é novo para você, mas usá-los para trabalhar com drivers tem um ritmo que vale a pena aprender explicitamente.

### `cat(1)`: a primeira verificação

`cat` lê a partir de seus argumentos e escreve na saída padrão. Para um driver que serve uma mensagem estática ou um buffer esvaziado, `cat` é a forma mais rápida de ver o que o caminho de leitura produz:

```sh
% cat /dev/myfirst/0
```

Se a saída for o que você espera, o caminho de leitura está funcionando. Se estiver vazia, ou seu driver não tem nada a entregar (verifique `sysctl dev.myfirst.0.stats.bufused`) ou seu handler está retornando EOF na primeira chamada. Se a saída estiver corrompida, ou seu buffer não está inicializado ou você está entregando bytes além de `bufused`.

`cat` abre seu argumento uma vez e lê dele até EOF. Cada `read(2)` é uma chamada separada ao seu `d_read`. Use `truss(1)` para ver quantas chamadas `cat` faz:

```sh
% truss cat /dev/myfirst/0 2>&1 | grep read
```

A saída mostra cada `read(2)` com seus argumentos e valor de retorno. Se você esperava uma leitura e vê três, isso indica algo sobre o dimensionamento do seu buffer; se esperava três leituras e vê apenas uma, o seu handler entregou todos os dados em uma única chamada.

### `echo(1)` e `printf(1)`: escrita simples

`echo` é a forma mais rápida de enviar uma string conhecida para o caminho de escrita do seu driver:

```sh
% echo "hello" | sudo tee /dev/myfirst/0 > /dev/null
```

Dois pontos merecem atenção. Primeiro, `echo` acrescenta uma quebra de linha por padrão; a string enviada tem seis bytes, não cinco. Use `echo -n` para suprimir a quebra de linha quando isso importar. Segundo, a invocação com `tee` existe para resolver um problema de permissão: o redirecionamento do shell (`>`) é executado com os privilégios do usuário, portanto `sudo echo > /dev/myfirst/0` falha ao abrir o nó. Redirecionar via `tee`, que roda sob `sudo`, contorna isso.

`printf` oferece mais controle:

```sh
% printf 'abc' | sudo tee /dev/myfirst/0 > /dev/null
```

Três bytes, sem quebra de linha. Use `printf '\x41\x42\x43'` para padrões binários.

### `dd(1)`: a ferramenta de precisão

Para qualquer teste que exija uma contagem específica de bytes ou um tamanho de bloco específico, `dd` é a ferramenta certa. `dd` também é uma das poucas ferramentas do sistema base que reporta leituras e escritas curtas em seu resumo, o que o torna especialmente útil para testar o comportamento do driver:

```sh
% sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=128 count=4
4+0 records in
4+0 records out
512 bytes transferred in 0.001234 secs (415000 bytes/sec)
```

Os contadores `X+Y records in` / `X+Y records out` têm um significado preciso: `X` é o número de transferências completas de bloco, `Y` é o número de transferências curtas. Uma linha mostrando `0+4 records out` significa que cada bloco foi aceito apenas parcialmente. É o driver dizendo alguma coisa.

`dd` também permite ler com um tamanho de bloco conhecido:

```sh
% sudo dd if=/dev/myfirst/0 of=/tmp/dump bs=64 count=1
```

Isso emite exatamente um `read(2)` de 64 bytes. Seu handler vê `uio_resid = 64`; você responde com o que tiver; o resultado é o que `dd` grava em `/tmp/dump`.

O flag `iflag=fullblock` instrui o `dd` a fazer loop em leituras curtas até preencher o bloco solicitado. Útil quando você quer absorver toda a saída do driver sem perder bytes pelo comportamento padrão de leitura curta.

### `od(1)` e `hexdump(1)`: inspeção byte a byte

Para testes de driver, `od` e `hexdump` permitem ver exatamente os bytes que o driver emitiu:

```sh
% sudo dd if=/dev/myfirst/0 bs=32 count=1 | od -An -tx1z
  68 65 6c 6c 6f 0a                                 >hello.<
```

O flag `-An` suprime a impressão de endereços. `-tx1z` exibe os bytes em hex e ASCII. Se a saída esperada for texto, você a vê à direita; se for binária, você vê o hex à esquerda.

Essas ferramentas se tornam essenciais quando uma leitura produz bytes inesperados. "Parece estranho" e "consigo ver cada byte em hex" são estados de depuração muito diferentes.

### `sysctl(8)` e `dmesg(8)`: a voz do kernel

Seu driver publica contadores via `sysctl` e eventos de ciclo de vida via `dmesg`. Ambos valem ser consultados durante cada teste:

```sh
% sysctl dev.myfirst.0
% dmesg | tail -20
```

A saída do sysctl é a sua janela para o estado atual do driver. `dmesg` é a sua janela para o histórico do driver desde o boot (ou desde que o ring buffer foi sobrescrito).

Um hábito útil: após cada teste, execute os dois. Se os números não corresponderem ao esperado, você terá localizado o bug rapidamente.

### `fstat(1)`: quem tem o descritor aberto?

Quando o driver se recusa a descarregar ("module busy"), a pergunta é: "quem tem `/dev/myfirst/0` aberto agora?". `fstat(1)` responde:

```sh
% fstat -p $(pgrep cat) /dev/myfirst/0
USER     CMD          PID   FD MOUNT      INUM MODE         SZ|DV R/W NAME
ebrandi  cat          1234    3 /dev         0 crw-rw----  myfirst/0  r /dev/myfirst/0
```

Alternativamente, `fuser(8)`:

```sh
% sudo fuser /dev/myfirst/0
/dev/myfirst/0:         1234
```

Qualquer uma das ferramentas nomeia os processos que mantêm o descritor. Encerre o responsável (com cuidado; não encerre nada que você não tenha iniciado) e o módulo será descarregado.

### `truss(1)` e `ktrace(1)`: observando as syscalls

Para inspecionar a interação de um programa de usuário com o seu driver, `truss` exibe cada syscall e seu valor de retorno:

```sh
% truss ./rw_myfirst
open("/dev/myfirst/0",O_WRONLY,0666)             = 3 (0x3)
write(3,"round-trip test payload\n",24)          = 24 (0x18)
close(3)                                         = 0 (0x0)
...
```

`ktrace` grava em um arquivo que `kdump` formata posteriormente; é a ferramenta certa quando você quer capturar um trace de um programa de longa execução.

Essas duas ferramentas não são específicas para drivers, mas são a forma de confirmar externamente que o seu driver está produzindo os resultados que um programa de usuário receberá.

### Um Ritmo de Teste Sugerido

Para cada estágio do capítulo, experimente este ciclo:

1. Construa e carregue.
2. Use `cat` para produzir a saída inicial e confirme visualmente.
3. Execute `sysctl dev.myfirst.0` para verificar se os contadores batem.
4. Execute `dmesg | tail` para ver os eventos de ciclo de vida.
5. Escreva algo com `echo` ou `dd`.
6. Leia de volta.
7. Repita com um tamanho maior, um tamanho de fronteira e um tamanho patológico.
8. Descarregue.

Depois de algumas iterações, isso se torna automático e rápido. É esse tipo de ritmo que transforma o desenvolvimento de drivers de um fardo em uma rotina.

### Um Passeio Concreto com `truss`

Executar um programa de userland sob `truss(1)` é uma das formas mais rápidas de ver exatamente quais syscalls ele faz ao seu driver e quais valores de retorno o kernel produz. Aqui está uma sessão típica com o driver do Estágio 3 carregado e vazio:

```sh
% truss ./rw_myfirst rt 2>&1
open("/dev/myfirst/0",O_WRONLY,00)               = 3 (0x3)
write(3,"round-trip test payload, 24b\n",29)     = 29 (0x1d)
close(3)                                         = 0 (0x0)
open("/dev/myfirst/0",O_RDONLY,00)               = 3 (0x3)
read(3,"round-trip test payload, 24b\n",255) = 29 (0x1d)
close(3)                                         = 0 (0x0)
exit(0x0)
```

Alguns pontos merecem atenção. Cada linha mostra uma syscall, seus argumentos e seu valor de retorno em decimal e hex. A chamada `write` recebeu 29 bytes e o driver aceitou todos os 29 (o valor de retorno coincide com o tamanho solicitado). A chamada `read` recebeu um buffer com espaço para 255 bytes e o driver produziu 29 bytes de conteúdo: uma leitura curta, que o programa de usuário aceita explicitamente. Ambas as chamadas `open` retornaram 3, porque os descritores 0, 1 e 2 são os fluxos padrão e o primeiro descritor livre é o 3.

Se você forçar uma escrita curta limitando o driver, `truss` mostrará isso claramente:

```sh
% truss ./write_big 2>&1 | head
open("/dev/myfirst/0",O_WRONLY,00)               = 3 (0x3)
write(3,"<8192 bytes of data>",8192)             = 4096 (0x1000)
write(3,"<4096 bytes of data>",4096)             ERR#28 'No space left on device'
close(3)                                         = 0 (0x0)
```

A primeira escrita solicitou 8192 bytes e foi aceita com 4096. A segunda escrita não teve nada a acrescentar porque o buffer estava cheio; o driver retornou `ENOSPC`, que `truss` apresentou como `ERR#28 'No space left on device'`. Essa é a visão do lado do usuário; do lado do driver, você retornou zero (com `uio_resid` decrementado para 4096) na primeira chamada e `ENOSPC` na segunda. Comparar o que `truss` vê com o que seu `device_printf` diz é uma excelente forma de detectar incompatibilidades entre a intenção do driver e o que o kernel reporta.

`truss -f` acompanha forks, o que é útil quando seu conjunto de testes cria processos filhos. `truss -d` prefixa cada linha com um timestamp relativo, útil para raciocinar sobre a latência entre chamadas. Ambos os flags são pequenos investimentos; os benefícios se acumulam rapidamente quando você começa a executar testes de estresse com múltiplos processos.

### Uma Nota Rápida sobre `ktrace`

`ktrace(1)` é o irmão mais robusto do `truss`. Ele grava um trace binário em um arquivo (`ktrace.out` por padrão), que você depois formata com `kdump(1)`. É a ferramenta certa quando:

- A execução do teste é longa e você não quer acompanhar a saída em tempo real.
- Você quer capturar detalhes mais finos que o `truss` não oferece (temporização de syscalls, entrega de sinais, lookups no namei).
- Você quer reproduzir um trace depois, talvez em outra máquina.

Uma sessão típica:

```sh
% sudo ktrace -i ./stress_rw -s 5
% sudo kdump | head -40
  2345 stress_rw CALL  open(0x800123456,0x1<O_WRONLY>)
  2345 stress_rw NAMI  "/dev/myfirst/0"
  2345 stress_rw RET   open 3
  2345 stress_rw CALL  write(0x3,0x800123500,0x40)
  2345 stress_rw RET   write 64
  2345 stress_rw CALL  write(0x3,0x800123500,0x40)
  2345 stress_rw RET   write 64
...
```

Para o Capítulo 9, a diferença entre `truss` e `ktrace` é pequena. Use `truss` como padrão; recorra ao `ktrace` quando precisar de mais detalhes ou de um trace gravado.

### Monitorando a Memória do Kernel com `vmstat -m`

Seu driver aloca memória do kernel via `malloc(9)` com o tipo `M_DEVBUF`. O comando `vmstat -m` do FreeBSD revela quantas alocações estão ativas em cada bucket de tipo. Execute-o com o driver carregado e ocioso, depois novamente com um buffer alocado, e o aumento será visível na linha `devbuf`:

```sh
% vmstat -m | head -1
         Type InUse MemUse HighUse Requests  Size(s)
% vmstat -m | grep devbuf
       devbuf   415   4120K       -    39852  16,32,64,128,256,512,1024,2048,...
```

A coluna `InUse` é a contagem atual de alocações vivas deste tipo. `MemUse` é o tamanho total atualmente em uso. `HighUse` é o valor máximo histórico desde o boot. `Requests` é a contagem acumulada de chamadas a `malloc` que selecionaram este tipo.

Carregue o driver do Estágio 2. `InUse` sobe em um (o buffer de 4096 bytes), `MemUse` sobe em aproximadamente 4 KiB, e `Requests` é incrementado. Descarregue. `InUse` cai em um; `MemUse` cai pelos 4 KiB. Se não cair, há um vazamento de memória, e o `vmstat -m` acabou de indicar isso.

Este é o segundo canal de observabilidade que vale acrescentar ao seu ritmo de testes. `sysctl` exibe contadores de propriedade do driver. `dmesg` exibe linhas de log de propriedade do driver. `vmstat -m` exibe contagens de alocação de propriedade do kernel, e detecta uma classe de bug (esqueceu de liberar) que os dois primeiros não conseguem ver.

Para um driver que declara seu próprio tipo de malloc via `MALLOC_DEFINE(M_MYFIRST, "myfirst", ...)`, o comando `vmstat -m | grep myfirst` é ainda melhor: ele isola as alocações do seu driver do pool genérico `devbuf`. O `myfirst` permanece com `M_DEVBUF` ao longo deste capítulo por simplicidade, mas migrar para um tipo dedicado é uma pequena mudança que você pode querer fazer antes de distribuir um driver fora do ambiente de laboratório do livro.



## Observabilidade: Tornando Seu Driver Legível

Um driver que faz a coisa certa vale mais quando você consegue confirmar, de fora do kernel, que ele está fazendo a coisa certa. Esta seção é uma breve reflexão sobre as escolhas de observabilidade que este capítulo vem fazendo, e por quê.

### Três Superfícies: sysctl, dmesg, userland

Seu driver apresenta três superfícies ao operador:

- **sysctl** para contadores em tempo real: valores pontuais que o operador pode consultar.
- **dmesg (device_printf)** para eventos de ciclo de vida: abertura, fechamento, erros, transições.
- **nós `/dev`** para o caminho de dados: os bytes propriamente ditos.

Cada uma tem um papel distinto. sysctl diz ao operador *o que é verdade agora*. dmesg diz ao operador *o que mudou recentemente*. `/dev` é o que o operador está de fato utilizando.

Um driver bem observado usa as três superfícies de forma deliberada. Um driver minimamente observado usa apenas a terceira, e depurá-lo exige um depurador ou muita tentativa e erro.

### Sysctl: Contadores vs Estado

`myfirst` expõe contadores pela árvore sysctl em `dev.myfirst.0.stats`:

- `attach_ticks`: um valor pontual (quando o driver foi anexado).
- `open_count`: um contador monotonicamente crescente (aberturas ao longo da vida).
- `active_fhs`: uma contagem ao vivo (descritores ativos no momento).
- `bytes_read`, `bytes_written`: contadores monotonicamente crescentes.
- `bufused`: um valor ao vivo (ocupação atual do buffer).

Contadores monotonicamente crescentes são mais fáceis de raciocinar do que valores ao vivo, porque sua taxa de variação é informativa mesmo quando o valor absoluto não é. Um operador que vê `bytes_read` crescendo a 1 MB/s aprendeu algo, mesmo que 1 MB/s não signifique nada fora de contexto.

Valores ao vivo são essenciais quando o estado importa para decisões (`active_fhs > 0` significa que o descarregamento vai falhar). Prefira contadores monotonicamente crescentes; use valores ao vivo quando precisar deles.

### dmesg: Eventos que Valem Ver

`device_printf(9)` escreve no buffer de mensagens do kernel, que `dmesg` exibe. Cada linha vale ser vista exatamente uma vez: use dmesg para eventos, não para status contínuo.

Os eventos que `myfirst` registra:

- Attach (uma vez por instância).
- Open (uma vez por abertura).
- Destruidor (uma vez por fechamento de descritor).
- Detach (uma vez por instância).

São quatro linhas por instância por ciclo de carga/descarga, mais duas linhas por par de abertura/fechamento. Confortável.

O que não registramos:

- Cada chamada a `read` ou `write`. Isso inundaria o dmesg em qualquer carga de trabalho real.
- Cada leitura de sysctl. Essas são passivas.
- Cada transferência bem-sucedida. Os contadores sysctl carregam essa informação de forma mais compacta.

Se um driver precisar registrar algo que acontece muitas vezes por segundo, a resposta usual é proteger o registro com `if (bootverbose)`, para que fique silencioso em sistemas de produção mas disponível para desenvolvedores que iniciam com `boot -v`. Para o `myfirst`, nem isso é necessário.

### A Armadilha do Excesso de Logs

Um driver que registra cada operação é um driver que esconde seus eventos importantes num mar de ruído. Se o seu dmesg mostrar dez mil linhas de `read returned 0 bytes`, a linha que diz `buffer full, returning ENOSPC` ficará invisível.

Mantenha os logs esparsos. Registre transições, não estados. Registre uma vez por instância, não uma vez por chamada. Na dúvida, silêncio.

### Contadores que Você Adicionará Depois

Os Capítulos 10 em diante estenderão a árvore de contadores com:

- `reads_blocked`, `writes_blocked`: contagem de chamadas que precisaram dormir (Capítulo 10).
- `poll_waiters`: contagem de assinantes ativos de `poll(2)` (Capítulo 10).
- `drain_waits`, `overrun_events`: diagnósticos do ring buffer (Capítulo 10).

Cada um é mais um item que o operador pode observar para entender o que o driver está fazendo. O padrão é o mesmo: exponha os contadores, mantenha o mecanismo silencioso, deixe o operador decidir quando inspecionar.

### Como Seu Driver Se Apresenta sob Carga Leve

Um exemplo concreto vale mais do que conselhos abstratos. Carregue o Stage 3, execute o programa `stress_rw` que acompanha o capítulo por alguns segundos com `sysctl dev.myfirst.0.stats` observando em outro terminal, e você verá algo assim:

**Antes de `stress_rw` iniciar:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 0
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 0
dev.myfirst.0.stats.bytes_written: 0
dev.myfirst.0.stats.bufused: 0
```

Zero de atividade, um attach, buffer vazio.

**Durante `stress_rw`, com `watch -n 0.5 sysctl dev.myfirst.0.stats`:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 2
dev.myfirst.0.stats.active_fhs: 2
dev.myfirst.0.stats.bytes_read: 1358976
dev.myfirst.0.stats.bytes_written: 1359040
dev.myfirst.0.stats.bufused: 64
```

Dois descritores ativos (escritor + leitor), contadores subindo, buffer com 64 bytes de dados em trânsito. `bytes_written` está ligeiramente à frente de `bytes_read`, o que é exatamente o esperado: o escritor produziu um bloco que o leitor ainda não consumiu por completo. A diferença é igual a `bufused`.

**Após `stress_rw` encerrar:**

```text
dev.myfirst.0.stats.attach_ticks: 12345678
dev.myfirst.0.stats.open_count: 2
dev.myfirst.0.stats.active_fhs: 0
dev.myfirst.0.stats.bytes_read: 4800000
dev.myfirst.0.stats.bytes_written: 4800000
dev.myfirst.0.stats.bufused: 0
```

Ambos os descritores fechados. Total de aberturas é 2 (acumulado). Ativo é 0. `bytes_read` é igual a `bytes_written`; o leitor alcançou totalmente o escritor. Buffer vazio.

Três características a observar. Primeiro, `active_fhs` sempre rastreia descritores ativos; é um valor em tempo real, não um contador acumulado. Segundo, `bytes_read == bytes_written` em estado estável quando o leitor acompanha o ritmo, mais o que estiver armazenado em `bufused`. Terceiro, `open_count` é um valor de tempo de vida que nunca diminui; uma forma rápida de detectar rotatividade é observar `open_count` crescer enquanto `active_fhs` permanece estável.

Um driver que se comporta de forma previsível sob carga é um driver que você pode operar com confiança. Quando os contadores se alinham da forma descrita neste parágrafo, você tem o seu primeiro driver de verdade, não um brinquedo.

## Tipos com Sinal, sem Sinal e os Perigos do Erro por Um

Uma breve seção sobre uma classe de bug que causou mais kernel panics do que quase qualquer outra. Ela aparece com frequência especial em handlers de I/O.

### `ssize_t` vs `size_t`

Dois tipos dominam o código de I/O:

- `size_t`: sem sinal, usado para tamanhos e contagens. `sizeof(x)` retorna `size_t`. `malloc(9)` recebe `size_t`. `memcpy` recebe `size_t`.
- `ssize_t`: com sinal, usado quando um valor pode ser negativo (geralmente -1 para indicar erro). `read(2)` e `write(2)` retornam `ssize_t`. `uio_resid` é `ssize_t`.

Os dois tipos têm a mesma largura em todas as plataformas suportadas pelo FreeBSD, mas não se convertem silenciosamente um no outro sem avisos do compilador, e se comportam de forma muito diferente quando ocorre um underflow aritmético.

Uma subtração de valores `size_t` que produziria um resultado negativo sofre wrap around e se torna um valor positivo enorme, porque `size_t` é sem sinal. Por exemplo:

```c
size_t avail = sc->buflen - sc->bufused;
```

Se `sc->bufused` for maior que `sc->buflen`, `avail` é um número enorme, e o próximo `uiomove` tentará uma transferência que ultrapassa o fim do buffer.

A defesa é o invariante. Em todas as seções de gerenciamento de buffer do capítulo, mantemos `sc->bufhead + sc->bufused <= sc->buflen`. Enquanto esse invariante se mantiver, `sc->buflen - (sc->bufhead + sc->bufused)` não pode sofrer underflow.

O risco está nos caminhos de código que violam o invariante acidentalmente. Um double-free que restaura um valor já consumido, uma escrita que atualiza `bufused` duas vezes, uma condição de corrida entre escritores. Esses são os bugs a rastrear quando `avail` parecer errado.

### `uio_resid` Pode Ser Comparado com Valores sem Sinal

`uio_resid` é `ssize_t`. Os tamanhos do seu buffer são `size_t`. Um código como este:

```c
if (uio->uio_resid > sc->buflen) ...
```

Será compilado com uma comparação entre tipos com e sem sinal. Compiladores modernos avisam sobre isso; o aviso deve ser levado a sério.

O padrão mais seguro é fazer o cast explicitamente:

```c
if ((size_t)uio->uio_resid > sc->buflen) ...
```

Ou usar `MIN`, que temos usado:

```c
towrite = MIN((size_t)uio->uio_resid, avail);
```

O cast é defensável porque `uio_resid` está documentado como não negativo em uios válidos (e `uiomove` faz `KASSERT` sobre isso). O cast deixa o compilador satisfeito e torna a intenção explícita.

### Erros por Um em Contadores

Um contador atualizado no lado errado de uma verificação de erro é um bug clássico:

```c
sc->bytes_read += towrite;          /* BAD: happens even on error */
error = uiomove(sc->buf, towrite, uio);
```

O formato correto é incrementar após o sucesso:

```c
error = uiomove(sc->buf, towrite, uio);
if (error == 0)
        sc->bytes_read += towrite;
```

É por isso que temos `if (error == 0)` protegendo cada atualização de contador no capítulo. O custo é uma linha de código. O benefício é que seus contadores correspondem à realidade.

### O Idioma `uio_offset - before`

Quando você quiser saber "quantos bytes `uiomove` realmente moveu?", a forma mais limpa é comparar `uio_offset` antes e depois:

```c
off_t before = uio->uio_offset;
error = uiomove_frombuf(sc->buf, sc->buflen, uio);
size_t moved = uio->uio_offset - before;
```

Isso funciona tanto para transferências completas quanto para transferências parciais. `moved` é a contagem real de bytes, independentemente do que o chamador solicitou ou de quanto estava disponível.

Esse idioma não tem custo em tempo de execução (duas subtrações) e é inequívoco no código. Use-o quando seu driver precisar contar bytes. A alternativa, inferir a contagem a partir de `uio_resid`, exige conhecer o tamanho original da requisição, o que representa mais trabalho de contabilidade.



## Solução de Problemas Adicionais: Os Casos de Borda

Expandindo a seção de solução de problemas anterior, aqui estão mais alguns cenários que você provavelmente encontrará na primeira vez que escrever um driver real.

### "A segunda leitura no mesmo descritor retorna zero"

Esperado para um driver de mensagem estática (Estágio 1): assim que `uio_offset` alcança o fim da mensagem, `uiomove_frombuf` retorna zero.

Inesperado para um driver FIFO (Estágio 3): a primeira leitura drenou o buffer e nenhum escritor o reabasteceu. O chamador não deveria emitir uma segunda leitura imediatamente sem que uma escrita tenha ocorrido no intervalo.

Para distinguir os dois casos, verifique `sysctl dev.myfirst.0.stats.bufused`. Se for zero, o buffer está vazio. Se for diferente de zero e você ainda vir zero bytes, há um bug.

### "O driver retorna zero bytes imediatamente quando o buffer tem dados"

O handler de leitura está tomando o caminho errado. Causas comuns:

- Uma verificação `bufused == 0` colocada no lugar errado. Se a verificação for executada antes da recuperação do estado por abertura, ela pode curto-circuitar a leitura antes do trabalho real.
- Um `return 0;` acidental anteriormente no handler (por exemplo, em um ramo de debug deixado de um experimento anterior).
- Um `mtx_unlock` ausente em um caminho de erro, fazendo com que todas as chamadas subsequentes bloqueiem no mutex indefinidamente. Sintoma: a segunda chamada trava, e não um retorno de zero bytes; mas vale a pena verificar.

### "Meu `uiomove_frombuf` sempre retorna zero independentemente do buffer"

Duas causas comuns:

- O argumento `buflen` é zero. `uiomove_frombuf` retorna zero imediatamente se `buflen <= 0`.
- `uio_offset` já está em ou além de `buflen`. `uiomove_frombuf` retorna zero para sinalizar EOF nesse caso.

Adicione um `device_printf` registrando os argumentos na entrada para confirmar em qual caso você se encontra.

### "O buffer transborda para a memória adjacente"

Sua aritmética está errada. Em algum lugar você está chamando `uiomove(sc->buf + X, N, uio)` onde `X + N > sc->buflen`. A escrita prossegue silenciosamente e corrompe a memória do kernel.

Seu kernel geralmente entrará em panic logo depois, possivelmente em um subsistema completamente não relacionado. A mensagem de panic não mencionará seu driver; ela mencionará o vizinho no heap que foi corrompido.

Se você suspeitar disso, recompile com `INVARIANTS` e `WITNESS` (e em muitas plataformas, KASAN no amd64). Esses recursos do kernel detectam overruns de buffer muito antes do que o kernel padrão faz.

### "Um processo lendo do dispositivo trava para sempre"

Como o Capítulo 9 não implementa I/O bloqueante, isso não deveria acontecer com o `myfirst` Estágio 3. Se acontecer, a causa mais provável é o processo mantendo um descritor de arquivo enquanto você tentou descarregar o driver; `destroy_dev(9)` está aguardando `si_threadcount` chegar a zero, e o processo está dentro do seu handler por alguma razão.

Para diagnosticar: `ps auxH | grep <your-test>`; `gdb -p <pid>` e `bt`. A stack deve revelar onde a thread está parada.

Se seu handler do Estágio 3 dormir acidentalmente (por exemplo, porque você adicionou um `tsleep` enquanto experimentava o material do Capítulo 10 antecipadamente), a correção é remover o sleep. O driver do Capítulo 9 não bloqueia.

### "`kldunload` retorna `kldunload: can't unload file: Device busy`"

Sintoma clássico de um descritor ainda aberto. Use `fuser /dev/myfirst/0` para encontrar o processo responsável, feche o descritor ou encerre o processo, e tente novamente.

### "Modifiquei o driver e `make` compila, mas `kldload` falha com incompatibilidade de versão"

Seu ambiente de build não corresponde ao kernel em execução. Verifique:

```sh
% freebsd-version -k
14.3-RELEASE
% ls /usr/obj/usr/src/amd64.amd64/sys/GENERIC
```

Se `/usr/src` for de uma versão diferente, seus headers produzem um módulo que o kernel recusa. Recompile com os fontes correspondentes. Em uma VM de laboratório, isso geralmente significa sincronizar `/usr/src` com a versão em execução via `fetch` ou `freebsd-update src-install`.

### "Vejo cada byte escrito pelo dispositivo impresso duas vezes no dmesg"

Você tem um `device_printf` dentro do caminho crítico que imprime cada transferência. Remova-o ou proteja-o com `if (bootverbose)`.

Uma versão mais branda do mesmo bug: um log de linha única que imprime o comprimento de cada transferência. Para cargas de trabalho de teste pequenas, isso parece bem; para uma carga de trabalho real do usuário, isso vai enterrar o dmesg e causar compressão de timestamps no buffer do kernel.

### "Meu `d_read` é chamado, mas meu `d_write` não é"

Ou o programa do usuário nunca chama `write(2)` no dispositivo, ou ele chama `write(2)` com o descritor não aberto para escrita (`O_RDONLY`). Verifique os dois.

Além disso: confirme que `cdevsw.d_write` está atribuído a `myfirst_write`. Um bug de copiar-colar que o atribui a `myfirst_read` resulta em ambas as direções atingindo o handler de leitura, com resultados previsivelmente confusos.



## Notas de Design: Por Que Cada Estágio Para Onde Para

Uma breve meta-seção sobre por que os três estágios do Capítulo 9 têm os limites que têm. Esse é o tipo de raciocínio de design que vale a pena tornar explícito, porque é o raciocínio que você aplicará ao projetar seus próprios drivers.

### Por Que o Estágio 1 Existe

O Estágio 1 é o menor `d_read` possível que não é `/dev/null`. Ele introduz:

- O helper `uiomove_frombuf(9)`, a maneira mais fácil de transferir um buffer fixo para o espaço do usuário.
- Tratamento de offset por descritor.
- O padrão de usar `uio_offset` como portador de estado.

O Estágio 1 não faz nada com escritas; o stub do Capítulo 8 está bem.

Sem o Estágio 1, o salto de stubs para um driver de leitura/escrita com buffer é grande demais. O Estágio 1 permite confirmar, com código mínimo, que o handler de leitura está corretamente conectado. Todo o resto se baseia nessa confirmação.

### Por Que o Estágio 2 Existe

O Estágio 2 introduz:

- Um buffer do kernel alocado dinamicamente.
- Um caminho de escrita que aceita dados do usuário.
- Um caminho de leitura que respeita o offset do chamador ao longo do buffer acumulado.
- O primeiro uso realista do mutex do softc em um handler de I/O.

O Estágio 2 deliberadamente não drena leituras. O buffer cresce até ficar cheio; escritas subsequentes retornam `ENOSPC`. Isso permite que dois leitores concorrentes confirmem que cada um tem seu próprio `uio_offset`, que é a propriedade que o Estágio 1 não conseguia demonstrar (porque o Estágio 1 não tinha nada a escrever).

### Por Que o Estágio 3 Existe

O Estágio 3 introduz:

- Leituras que drenam o buffer.
- A coordenação entre um ponteiro de cabeça e um contador de uso.
- A semântica FIFO que a maioria dos drivers reais aproxima.

O Estágio 3 não faz wrap around. Os ponteiros de cabeça e de uso avançam pelo buffer, e o buffer colapsa para o início quando fica vazio. Um ring buffer adequado (com cabeça e cauda girando em torno de um array de tamanho fixo) pertence ao Capítulo 10, porque se combina naturalmente com leituras bloqueantes e `poll(2)`: um ring torna a operação em estado estacionário eficiente, e a operação eficiente em estado estacionário é exatamente o que um leitor bloqueante precisa.

### Por Que Não Há Ring Buffer Aqui

Um ring buffer representa de cinco a quinze linhas de contabilidade adicional além do que o Estágio 3 faz. Adicioná-lo agora não seria uma grande quantidade de código. A razão para adiá-lo é pedagógica: os dois conceitos ("semântica do caminho de I/O" e "mecânica do ring buffer") são independentemente confusos para um iniciante, e dividi-los em dois capítulos permite que cada capítulo aborde uma fonte de confusão por vez.

Quando o Capítulo 10 introduz o ring, o leitor já domina o caminho de I/O. O material novo é apenas a contabilidade do ring.

### Por Que Não Há Bloqueio

O bloqueio é útil, mas introduz `msleep(9)`, variáveis de condição, o hook de encerramento `d_purge`, e um emaranhado de questões de correção sobre o que despertar e quando. Cada um desses é um tópico substancial. Misturá-los no Capítulo 9 dobraria seu tamanho e reduziria sua clareza pela metade.

A primeira seção do Capítulo 10 é "quando seu driver precisa esperar". É uma continuação natural.

### O Que os Estágios **Não** Pretendem Ser

Os estágios não são uma simulação de um driver de hardware. Eles não imitam DMA. Eles não simulam interrupções. Eles não fingem ser nada além do que são: drivers em memória que exercitam o caminho de I/O do UNIX.

Isso importa porque, mais adiante no livro, quando escrevermos drivers de hardware reais, o caminho de I/O terá a mesma aparência. Os detalhes de hardware (de onde vêm os bytes, para onde vão os bytes) mudarão, mas a forma do handler, o uso de uiomove, as convenções de errno, os padrões de contador: tudo isso será reconhecível a partir do Capítulo 9.

Um driver que move bytes corretamente pela fronteira de confiança usuário/kernel representa 80% de qualquer driver real. O Capítulo 9 ensina esses 80%.



## Laboratórios Práticos

Os laboratórios abaixo acompanham os três estágios acima. Cada laboratório é um checkpoint que prova que seu driver está fazendo o que o texto acabou de descrever. Leia o laboratório completamente antes de começar, e faça-os em ordem.

### Laboratório 9.1: Compilar e Carregar o Estágio 1

**Objetivo:** Compilar o driver do Estágio 1, carregá-lo, ler a mensagem estática e confirmar o tratamento de offset por descritor.

**Passos:**

1. Comece pela árvore de acompanhamento: `cp -r examples/part-02/ch09-reading-and-writing/stage1-static-message ~/drivers/ch09-stage1`. Como alternativa, modifique seu driver do estágio 2 do Capítulo 8 conforme o passo a passo do Estágio 1 acima.
2. Entre no diretório e compile:
   ```sh
   % cd ~/drivers/ch09-stage1
   % make
   ```
3. Carregue o módulo:
   ```sh
   % sudo kldload ./myfirst.ko
   ```
4. Confirme que o dispositivo está presente:
   ```sh
   % ls -l /dev/myfirst/0
   crw-rw----  1 root  operator ... /dev/myfirst/0
   ```
5. Leia a mensagem:
   ```sh
   % cat /dev/myfirst/0
   Hello from myfirst.
   This is your first real read path.
   Chapter 9, Stage 1.
   ```
6. Compile a ferramenta de userland `rw_myfirst.c` da árvore de acompanhamento e execute-a no modo "read twice":
   ```sh
   % cc -o rw_myfirst rw_myfirst.c
   % ./rw_myfirst read
   [read 1] 75 bytes:
   Hello from myfirst.
   This is your first real read path.
   Chapter 9, Stage 1.
   [read 2] 0 bytes (EOF)
   ```
7. Confirme o contador por descritor:
   ```sh
   % dmesg | tail -5
   ```
   Você deve ver as linhas `open via /dev/myfirst/0 fh=...` e `per-open dtor fh=...` do Capítulo 8, além da confirmação de que o corpo da mensagem foi lido.
8. Descarregue o módulo:
   ```sh
   % sudo kldunload myfirst
   ```

**Critérios de sucesso:**

- `cat` imprime a mensagem.
- A ferramenta de userland mostra 75 bytes na primeira leitura e 0 bytes na segunda.
- `dmesg` mostra um open e um destructor por invocação de `./rw_myfirst read`.

**Erros comuns:**

- Esquecer o `-1` em `sizeof(myfirst_message) - 1`. A mensagem passará a incluir um byte NUL no final, que aparece como um caractere estranho na saída do usuário.
- Não chamar `devfs_get_cdevpriv` antes da verificação `sc == NULL`. O restante do capítulo depende dessa ordem; execute o código para entender por que ela é a correta.
- Usar `(void *)sc->message` em vez de `__DECONST(void *, sc->message)`. Ambas as formas funcionam na maioria dos compiladores; a forma `__DECONST` é a convenção e elimina um aviso em algumas configurações de compilador.

### Lab 9.2: Exercitando o Stage 2 com Escritas e Leituras

**Objetivo:** Construir o Stage 2, enviar dados do espaço do usuário para o driver, recuperá-los de volta e observar os contadores sysctl.

**Passos:**

1. A partir da árvore de acompanhamento: `cp -r examples/part-02/ch09-reading-and-writing/stage2-readwrite ~/drivers/ch09-stage2`.
2. Compile e carregue:
   ```sh
   % cd ~/drivers/ch09-stage2
   % make
   % sudo kldload ./myfirst.ko
   ```
3. Verifique o estado inicial:
   ```sh
   % sysctl dev.myfirst.0.stats
   dev.myfirst.0.stats.attach_ticks: ...
   dev.myfirst.0.stats.open_count: 0
   dev.myfirst.0.stats.active_fhs: 0
   dev.myfirst.0.stats.bytes_read: 0
   dev.myfirst.0.stats.bytes_written: 0
   dev.myfirst.0.stats.bufused: 0
   ```
4. Escreva uma linha de texto:
   ```sh
   % echo "the quick brown fox" | sudo tee /dev/myfirst/0 > /dev/null
   ```
5. Leia de volta:
   ```sh
   % cat /dev/myfirst/0
   the quick brown fox
   ```
6. Observe os contadores:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 20
   % sysctl dev.myfirst.0.stats.bytes_written
   dev.myfirst.0.stats.bytes_written: 20
   % sysctl dev.myfirst.0.stats.bytes_read
   dev.myfirst.0.stats.bytes_read: 20
   ```
7. Provoque `ENOSPC`:
   ```sh
   % dd if=/dev/zero bs=1024 count=8 | sudo tee /dev/myfirst/0 > /dev/null
   ```
   Espere um erro de escrita parcial. Inspecione `sysctl dev.myfirst.0.stats.bufused`; o valor deve ser 4096 (o tamanho do buffer).
8. Confirme que as leituras ainda entregam o conteúdo:
   ```sh
   % sudo cat /dev/myfirst/0 | od -An -c | head -3
   ```
9. Descarregue:
   ```sh
   % sudo kldunload myfirst
   ```

**Critérios de sucesso:**

- As escritas depositam bytes; as leituras os devolvem.
- `bufused` corresponde ao número de bytes escritos desde o último reset.
- O `dd` exibe uma escrita parcial quando o buffer fica cheio; o driver retorna `ENOSPC`.
- O `dmesg` exibe linhas de abertura e de destrutor para cada processo que abriu o dispositivo.

**Erros comuns:**

- Esquecer de liberar `sc->buf` no `detach`. O driver vai descarregar sem reclamações, mas uma verificação posterior de vazamento de memória do kernel (`vmstat -m | grep devbuf`) vai mostrar drift.
- Manter o mutex do softc durante a chamada a `uiomove`, sem ter certeza de que o mutex é `MTX_DEF` e não um spin lock. O `mtx_init(..., MTX_DEF)` do Capítulo 7 é a escolha correta; não o altere.
- Omitir o reset de `sc->bufused = 0` no `attach`. O Newbus inicializa o softc com zero para você, mas tornar a inicialização explícita é a convenção adotada; isso também torna uma refatoração futura menos sujeita a erros.

### Lab 9.3: Comportamento FIFO do Stage 3

**Objetivo:** Construir o Stage 3, exercitar o comportamento FIFO a partir de dois terminais e confirmar que as leituras drenam o buffer.

**Passos:**

1. A partir da árvore de acompanhamento: `cp -r examples/part-02/ch09-reading-and-writing/stage3-echo ~/drivers/ch09-stage3`.
2. Compile e carregue:
   ```sh
   % cd ~/drivers/ch09-stage3
   % make
   % sudo kldload ./myfirst.ko
   ```
3. No terminal A, escreva alguns bytes:
   ```sh
   % echo "message A" | sudo tee /dev/myfirst/0 > /dev/null
   ```
4. No terminal B, leia-os:
   ```sh
   % cat /dev/myfirst/0
   message A
   ```
5. Leia novamente no terminal B:
   ```sh
   % cat /dev/myfirst/0
   ```
   Não espere nenhuma saída. O buffer está vazio.
6. No terminal A, escreva duas linhas em rápida sucessão:
   ```sh
   % echo "first" | sudo tee /dev/myfirst/0 > /dev/null
   % echo "second" | sudo tee /dev/myfirst/0 > /dev/null
   ```
7. No terminal B, leia:
   ```sh
   % cat /dev/myfirst/0
   first
   second
   ```
   Espere as duas linhas concatenadas. Ambas as escritas foram adicionadas ao mesmo buffer antes que qualquer leitura ocorresse.
8. Inspecione os contadores:
   ```sh
   % sysctl dev.myfirst.0.stats
   ```
   `bufused` deve ter voltado a zero. `bytes_read` e `bytes_written` devem coincidir.
9. Descarregue:
   ```sh
   % sudo kldunload myfirst
   ```

**Critérios de sucesso:**

- As escritas adicionam ao buffer; as leituras o drenam.
- Uma leitura após o buffer ser drenado retorna imediatamente (EOF quando vazio).
- `bytes_read` sempre é igual a `bytes_written` assim que o leitor alcança as escritas.

**Erros comuns:**

- Não resetar `bufhead = 0` quando `bufused` chega a zero. O buffer vai "derivar" em direção ao final de `sc->buf` e recusar escritas muito antes de estar cheio.
- Esquecer de atualizar `bufhead` à medida que as leituras drenam o buffer. O driver vai ler os mesmos bytes repetidamente.
- Usar `uio->uio_offset` como offset por descritor. Em um FIFO, os offsets são compartilhados; um offset por descritor não faz sentido e vai confundir quem testar.

### Lab 9.4: Usando `dd` para Medir o Comportamento de Transferência

**Objetivo:** Usar `dd(1)` para gerar transferências de tamanho conhecido, ler os resultados de volta e verificar que os contadores concordam.

O `dd` é a ferramenta ideal aqui porque permite controlar o tamanho do bloco, o número de blocos e o comportamento em transferências parciais.

1. Recarregue o driver do Stage 3 do zero:
   ```sh
   % sudo kldunload myfirst; sudo kldload ./myfirst.ko
   ```
2. Escreva 512 bytes em um único bloco:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=512 count=1
   1+0 records in
   1+0 records out
   512 bytes transferred
   ```
3. Observe `bufused = 512`:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 512
   ```
4. Leia-os de volta com o mesmo tamanho de bloco:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/out bs=512 count=1
   1+0 records in
   1+0 records out
   512 bytes transferred
   ```
5. Verifique que o FIFO agora está vazio:
   ```sh
   % sysctl dev.myfirst.0.stats.bufused
   dev.myfirst.0.stats.bufused: 0
   ```
6. Escreva 8192 bytes em um bloco grande:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=8192 count=1
   dd: /dev/myfirst/0: No space left on device
   0+0 records in
   0+0 records out
   0 bytes transferred
   ```
   O driver aceitou 4096 bytes (o tamanho do buffer) dos 8192 solicitados e retornou uma escrita parcial para o restante.
7. Como alternativa, use `bs=4096` com `count=2`:
   ```sh
   % sudo dd if=/dev/urandom of=/dev/myfirst/0 bs=4096 count=2
   dd: /dev/myfirst/0: No space left on device
   1+0 records in
   0+0 records out
   4096 bytes transferred
   ```
   O primeiro bloco de 4096 bytes foi aceito integralmente; o segundo falhou com `ENOSPC`.
8. Drene:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/out bs=4096 count=1
   % sudo kldunload myfirst
   ```

**Critérios de sucesso:**

- O `dd` reporta as contagens de bytes esperadas em cada passo.
- O driver aceita até 4096 bytes e recusa o restante com `ENOSPC`.
- `bufused` acompanha o estado do buffer após cada operação.

### Lab 9.5: Um Pequeno Programa C de Round-Trip

**Objetivo:** Escrever um programa C curto em espaço do usuário que abre o dispositivo, escreve bytes conhecidos, fecha o descritor, o abre novamente, lê os bytes de volta e verifica se eles coincidem.

1. Salve o seguinte como `rw_myfirst.c` em `~/drivers/ch09-stage3`:

```c
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static const char payload[] = "round-trip test payload\n";

int
main(void)
{
        int fd;
        ssize_t n;

        fd = open("/dev/myfirst/0", O_WRONLY);
        if (fd < 0) { perror("open W"); return 1; }
        n = write(fd, payload, sizeof(payload) - 1);
        if (n != (ssize_t)(sizeof(payload) - 1)) {
                fprintf(stderr, "short write: %zd\n", n);
                return 2;
        }
        close(fd);

        char buf[128] = {0};
        fd = open("/dev/myfirst/0", O_RDONLY);
        if (fd < 0) { perror("open R"); return 3; }
        n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) { perror("read"); return 4; }
        close(fd);

        if ((size_t)n != sizeof(payload) - 1 ||
            memcmp(buf, payload, n) != 0) {
                fprintf(stderr, "mismatch: wrote %zu, read %zd\n",
                    sizeof(payload) - 1, n);
                return 5;
        }

        printf("round-trip OK: %zd bytes\n", n);
        return 0;
}
```

2. Compile e execute:
   ```sh
   % cc -o rw_myfirst rw_myfirst.c
   % sudo ./rw_myfirst
   round-trip OK: 24 bytes
   ```
3. Inspecione o `dmesg` para ver as duas aberturas e os dois destrutores.

**Critérios de sucesso:**

- O programa exibe `round-trip OK: 24 bytes`.
- O `dmesg` exibe um par abertura/destrutor para a escrita e outro para a leitura.

**Erros comuns:**

- Escrever menos bytes do que o payload e não verificar o valor de retorno. `write(2)` pode retornar uma contagem parcial; seu teste precisa tratar isso.
- Confundir `O_WRONLY` com `O_RDONLY`. `open(2)` verifica o modo em relação aos bits de acesso do nó; abrir com o modo errado retorna `EACCES` (ou similar).
- Assumir que `read(2)` retorna a contagem solicitada. Ele pode retornar menos; novamente, quem chama precisa fazer um loop.

### Lab 9.6: Inspecionando Round-Trips Binários

**Objetivo:** Confirmar que o driver lida com dados binários arbitrários, não apenas texto, enviando bytes aleatórios e verificando que os mesmos bytes retornam.

1. Com o Stage 3 carregado e o buffer vazio, escreva 256 bytes aleatórios:
   ```sh
   % sudo dd if=/dev/urandom of=/tmp/sent bs=256 count=1
   % sudo dd if=/tmp/sent of=/dev/myfirst/0 bs=256 count=1
   ```
2. Leia de volta o mesmo número de bytes:
   ```sh
   % sudo dd if=/dev/myfirst/0 of=/tmp/received bs=256 count=1
   ```
3. Compare:
   ```sh
   % cmp /tmp/sent /tmp/received && echo MATCH
   MATCH
   ```
4. Inspecione ambos os arquivos byte a byte:
   ```sh
   % od -An -tx1 /tmp/sent | head -2
   % od -An -tx1 /tmp/received | head -2
   ```
5. Experimente um padrão patológico: todos zeros, todos `0xff`, depois um arquivo cheio de um único byte. Confirme que cada padrão faz o round-trip exatamente.

**Critérios de sucesso:**

- O `cmp` não reporta diferenças.
- O driver preserva cada bit da entrada.
- Sem reordenamento de bytes, sem interpretações "prestativas", sem transformações inesperadas.

Este laboratório é curto, mas importante: ele verifica que seu driver é um armazenamento transparente de bytes, e não um filtro de texto que acidentalmente interpreta alguns bytes de forma especial. Se você observar diferenças entre os arquivos enviados e recebidos, há um bug no caminho de transferência, provavelmente uma contagem incorreta de comprimento ou um erro de off-by-one na aritmética do buffer.

### Lab 9.7: Observando um Driver em Execução de Ponta a Ponta

**Objetivo:** Combinar sysctl, dmesg, truss e vmstat em uma única observação de ponta a ponta do driver do Stage 3 sob carga real. Este laboratório não tem código novo; ele é a ponte entre "eu escrevi o driver" e "eu consigo ver o que ele está fazendo".

**Passos:**

1. Com o Stage 3 carregado do zero, abra quatro terminais. O terminal A vai executar os ciclos de carga e descarga do driver. O terminal B vai monitorar o sysctl. O terminal C vai acompanhar o dmesg. O terminal D vai executar uma carga de trabalho do usuário.
2. **Terminal A:**
   ```sh
   % sudo kldload ./myfirst.ko
   % vmstat -m | grep devbuf
   ```
   Anote os valores de `InUse` e `MemUse` da linha `devbuf`.
3. **Terminal B:**
   ```sh
   % watch -n 1 sysctl dev.myfirst.0.stats
   ```
4. **Terminal C:**
   ```sh
   % sudo dmesg -c > /dev/null
   % sudo dmesg -w
   ```
   O `-c` limpa as mensagens acumuladas; o `-w` monitora novas mensagens.
5. **Terminal D:**
   ```sh
   % cd examples/part-02/ch09-reading-and-writing/userland
   % make
   % sudo truss ./rw_myfirst rt 2>&1 | tail -10
   ```
6. Verifique o terminal B: você deve ver `open_count` incrementar em 2 (um para a escrita, um para a leitura), `active_fhs` voltar a 0 e `bytes_read == bytes_written`.
7. Verifique o terminal C: você deve ver duas linhas de abertura e duas linhas de destrutor geradas por `device_printf`.
8. No terminal A, execute `vmstat -m | grep devbuf` novamente. `InUse` e `MemUse` devem ter voltado aos valores anteriores ao carregamento, mais o que o próprio driver alocou (tipicamente apenas o buffer de 4 KiB e o softc).
9. **Teste de estresse:** no terminal D,
   ```sh
   % sudo ./stress_rw -s 5
   ```
   Observe o terminal B. Você deve ver `bufused` oscilar, os contadores subirem e `active_fhs` chegar a 2 enquanto o teste está em execução.
10. Quando o teste de estresse terminar, verifique no terminal B que `active_fhs` é 0. No terminal A,
    ```sh
    % sudo kldunload myfirst
    % vmstat -m | grep devbuf
    ```
    `InUse` deve ter voltado à linha de base anterior ao carregamento. Se não tiver voltado, seu driver vazou uma alocação e o `vmstat -m` acabou de te avisar.

**Critérios de sucesso:**

- Os contadores sysctl correspondem à carga de trabalho que você executou.
- O dmesg exibe um par abertura/destrutor por abertura/fechamento de descritor.
- A saída do truss corresponde ao seu modelo mental do que o programa fez.
- `vmstat -m | grep devbuf` retorna à linha de base após o descarregamento.
- Sem panics, sem avisos, sem drift inexplicável nos contadores.

**Por que este laboratório importa:** este é o primeiro laboratório que exercita toda a cadeia de observabilidade de uma vez. Em produção, o sinal de que algo está errado quase nunca vem de um crash; ele vem de um contador que saiu dos limites esperados, de uma linha no `dmesg` que ninguém esperava ou de uma leitura do `vmstat -m` que não corresponde à realidade. Desenvolver o hábito de observar as quatro superfícies juntas é o que separa "eu escrevi um driver" de "eu sou responsável por um driver".



## Exercícios Desafio

Estes desafios ampliam o material sem introduzir tópicos que pertencem a capítulos posteriores. Cada um usa apenas as primitivas que apresentamos. Tente-os antes de consultar a árvore de acompanhamento; o aprendizado está na tentativa, não na resposta.

### Desafio 9.1: Contadores de Leitura por Descritor

Estenda o Stage 2 para que o contador `reads` por descritor seja exposto via sysctl. O contador deve estar disponível por descritor ativo, o que significa um sysctl por `fh` em vez de por softc.

Este desafio é mais difícil do que parece: os sysctls são alocados e liberados em pontos conhecidos do ciclo de vida do softc, e a estrutura por descritor existe apenas enquanto o descritor existe. Uma solução limpa registra um nó sysctl por `fh` em `d_open` e o cancela no destrutor. Tenha cuidado com os tempos de vida; o contexto sysctl deve ser liberado antes da memória do `fh`.

*Dica:* `sysctl_ctx_init` e `sysctl_ctx_free` operam por contexto. Você pode dar a cada `fh` seu próprio contexto e liberá-lo no destrutor.

*Alternativa:* mantenha uma lista encadeada de ponteiros `fh` no softc (protegida pelo mutex) e exponha-a por meio de um handler sysctl personalizado que percorre a lista sob demanda. Esse é o padrão que `/usr/src/sys/kern/tty_info.c` usa para estatísticas por processo.

### Desafio 9.2: Um Teste com `readv(2)`

Escreva um programa em espaço do usuário que use `readv(2)` para ler do driver para três buffers separados de tamanhos 8, 16 e 32 bytes. Confirme que o driver entrega bytes para todos os três buffers em sequência.

O kernel e o `uiomove(9)` já tratam `readv(2)`; o driver não precisa de alterações. O objetivo deste desafio é se convencer desse fato.

*Dica:* `struct iovec iov[3] = {{buf1, 8}, {buf2, 16}, {buf3, 32}};`, depois `readv(fd, iov, 3)`. O valor de retorno é o total de bytes entregues nos três buffers; os valores individuais de `iov_len` não são modificados no lado do usuário.

### Desafio 9.3: Demonstração de Escrita Parcial

Modifique o `myfirst_write` do Stage 2 para aceitar no máximo 128 bytes por chamada, independentemente de `uio_resid`. Um programa do usuário que escreva 1024 bytes deve ver uma escrita parcial de 128 bytes a cada vez.

Em seguida, escreva um programa de teste curto que escreva 1024 bytes em uma única chamada `write(2)`, observe o valor de retorno da escrita parcial e faça um loop até que todos os 1024 bytes tenham sido aceitos.

Perguntas para refletir:

- O `cat` trata escritas parciais corretamente? (Sim.)
- O `echo > /dev/myfirst/0 "..."` as trata corretamente? (Geralmente sim, via `printf` no shell, mas às vezes não; vale a pena testar.)
- O que acontece se você remover o comportamento de escrita parcial e tentar exceder o tamanho do buffer? (Você recebe `ENOSPC` após a primeira escrita de 4096 bytes.)

Este desafio ensina você a separar "o driver faz a coisa certa" de "programas do usuário assumem o que os drivers fazem".

### Desafio 9.4: Um Sensor com `ls -l`

Faça com que a resposta do driver a uma leitura dependa da saída de `ls -l` do próprio dispositivo. Ou seja: cada leitura produz o timestamp atual do nó de dispositivo.

*Dica:* `sc->cdev->si_ctime` e `sc->cdev->si_mtime` são campos `struct timespec` do cdev. Você pode convertê-los em uma string com formatação `printf`, colocá-la em um buffer do kernel e enviá-la ao espaço do usuário com `uiomove_frombuf(9)`.

*Aviso:* `si_ctime` / `si_mtime` podem ser atualizados pelo devfs à medida que os nós são acessados. Observe o que acontece quando você executa `touch /dev/myfirst/0` e lê novamente.

### Desafio 9.5: Um Driver de Eco Reverso

Modifique o Estágio 3 para que cada leitura retorne os bytes na ordem inversa à que foram escritos. Uma escrita de `"hello"` seguida de uma leitura deve produzir `"olleh"`.

Este desafio é inteiramente sobre a organização do buffer. As chamadas a `uiomove` permanecem as mesmas; o que você muda são os endereços que passa a elas.

*Dica:* Você pode inverter o buffer a cada leitura (solução mais cara) ou armazenar os bytes em ordem inversa no lado da escrita (solução mais barata). Nenhuma das duas é a resposta "certa"; cada uma tem propriedades distintas de correção e concorrência. Escolha uma e argumente a favor dela em um comentário.

### Desafio 9.6: Round Trip Binário

Escreva um programa de usuário que grave uma `struct timespec` no driver e depois leia uma de volta. Compare as duas estruturas. Elas são iguais? Deveriam ser, pois `myfirst` é um armazenamento transparente de bytes.

Estenda o programa para gravar dois valores `struct timespec`, depois execute `lseek(fd, sizeof(struct timespec), SEEK_SET)` e leia o segundo. O que acontece? (Pista: o FIFO não suporta seeks de forma significativa.)

Este desafio ilustra o ponto "leitura e escrita transportam bytes, não tipos" da seção sobre transferência segura de dados. Os bytes fazem o round trip perfeitamente; a informação de tipo não.

### Desafio 9.7: Um Test Harness com Visualização Hexadecimal

Escreva um pequeno script shell que, dado um número de bytes N, gere N bytes aleatórios com `dd if=/dev/urandom bs=$N count=1`, redirecione-os para seu driver do Estágio 3, depois os leia de volta com `dd if=/dev/myfirst/0 bs=$N count=1`, e compare os dois fluxos com `cmp`. O script deve reportar sucesso para fluxos iguais e uma saída no estilo diff para fluxos divergentes. Execute-o com N = 1, 2, 4, ..., 4096 para varrer tamanhos pequenos, de fronteira e que preenchem a capacidade.

Perguntas a responder durante a varredura:

- Todo tamanho faz o round trip corretamente até 4096, inclusive?
- Com 4097, o que o driver faz? O test harness reporta o erro de forma compreensível?
- Existe algum tamanho em que `cmp` reporta uma diferença? Se sim, qual foi a causa subjacente?

Este desafio recompensa a combinação das ferramentas da seção Fluxo de Trabalho Prático: `dd` para transferências precisas, `cmp` para verificação byte a byte, `sysctl` para contadores e o shell para orquestração. Um test harness robusto como este é o tipo de hábito que compensa sempre que você refatora um driver e quer saber rapidamente se o comportamento ainda está correto.

### Desafio 9.8: Quem Tem o Descritor Aberto?

Escreva um pequeno programa C que abra `/dev/myfirst/0`, bloqueie em `pause()` (mantendo o descritor indefinidamente) e execute até receber `SIGTERM`. Em um segundo terminal, execute `fstat | grep myfirst` e depois `fuser /dev/myfirst/0`. Observe a saída. Agora tente `kldunload myfirst`. Que erro você recebe? Por quê?

Em seguida, encerre o processo com `SIGTERM` ou com `kill`. Observe o destrutor disparar no `dmesg`. Tente `kldunload` novamente. Deve ter sucesso.

Este desafio é curto, mas fundamenta um dos invariantes mais sutis do capítulo: um driver não pode ser descarregado enquanto qualquer descritor estiver aberto em um de seus cdevs, e o FreeBSD fornece aos operadores um conjunto padrão de ferramentas para localizar o responsável. Na próxima vez que um `kldunload` real falhar com `EBUSY`, você já terá visto a forma desse problema antes.



## Solução de Problemas Comuns

Todo erro que você provavelmente cometerá em `d_read` / `d_write` se enquadra em uma pequena quantidade de categorias. Esta seção é um guia de campo resumido.

### "Meu driver retorna zero bytes mesmo depois de eu ter escrito dados"

Geralmente, este é um de dois bugs.

**Bug 1**: Você esqueceu de atualizar `bufused` (ou equivalente) após o `uiomove` bem-sucedido. A escrita chegou, os bytes foram movidos, mas o estado do driver nunca refletiu a chegada. A próxima leitura vê `bufused == 0` e reporta EOF.

Correção: sempre atualize seus campos de rastreamento dentro de `if (error == 0) { ... }` após `uiomove` retornar.

**Bug 2**: Você redefine `bufused` (ou `bufhead`) em algum lugar inadequado. Um padrão comum é adicionar uma linha de reset dentro de `d_open` ou `d_close` "por capricho de organização". Isso apaga os dados que o chamador anterior escreveu.

Correção: redefina o estado geral do driver apenas em `attach` (ao carregar) ou `detach` (ao descarregar). O estado por descritor pertence a `fh`, redefinido por `malloc(M_ZERO)` e limpo pelo destrutor.

### "Minhas leituras retornam lixo"

O buffer não está inicializado. `malloc(9)` sem `M_ZERO` retorna um bloco de memória cujo conteúdo é indefinido. Se seu `d_read` alcançar além de `bufused`, ou ler de deslocamentos que não foram escritos, os bytes que você vê são resíduos de qualquer memória que o kernel reciclou.

Correção: sempre passe `M_ZERO` para `malloc` em `attach`. Sempre limite as leituras ao high-water mark atual (`bufused`), não ao tamanho total do buffer (`buflen`).

Existe uma variante mais grave desse bug. Um driver que retorna memória do kernel não inicializada para o espaço do usuário acaba de vazar estado do kernel para o espaço do usuário. Em produção, isso é uma falha de segurança. No desenvolvimento é um bug; em produção é um CVE.

### "O kernel entra em pânico com um pagefault em um endereço de usuário"

Você chamou `memcpy` ou `bcopy` diretamente em um ponteiro de usuário em vez de passar por `uiomove` / `copyin` / `copyout`. O acesso falhou, o kernel não tinha um handler de falha instalado e o resultado foi um pânico.

Correção: nunca dereferencie um ponteiro de usuário diretamente. Passe por `uiomove(9)` (em handlers) ou `copyin(9)` / `copyout(9)` (em outros contextos).

### "O driver recusa-se a ser descarregado"

Você tem pelo menos um descritor de arquivo ainda aberto. `detach` retorna `EBUSY` quando `active_fhs > 0`; o módulo não será descarregado até que cada `fh` tenha sido destruído.

Correção: feche o descritor no userland. Se um processo em segundo plano o estiver segurando, encerre o processo (após confirmar que é seu; não encerre daemons do sistema). `fstat -p <pid>` mostra quais arquivos um processo tem abertos; `fuser /dev/myfirst/0` mostra quais processos têm o nó aberto.

O Capítulo 10 introduzirá padrões com `destroy_dev_drain` para drivers que precisam forçar um leitor bloqueado a sair. O Capítulo 9 não bloqueia, então esse problema não ocorre em operação normal; quando ocorre, é porque o userland está segurando o descritor em algum lugar inesperado.

### "Meu handler de escrita retorna EFAULT"

Sua chamada a `uiomove` encontrou um endereço de usuário inválido. As causas comuns:

- Um programa do usuário chamou `write(fd, NULL, n)` ou `write(fd, (void*)0xdeadbeef, n)`.
- Um programa do usuário escreveu um ponteiro que havia liberado.
- Você passou acidentalmente um ponteiro do kernel como destino para `uiomove`. Isso pode acontecer se você construir uma uio manualmente para dados em espaço de kernel e depois passá-la para um handler que espera uma uio em espaço de usuário. O `copyout` resultante vê um endereço "de usuário" que é, na verdade, um endereço do kernel; dependendo da arquitetura, você recebe `EFAULT` ou uma corrupção sutil.

Correção: verifique `uio->uio_segflg`. Para handlers acionados pelo usuário, deve ser `UIO_USERSPACE`. Se você está passando uma uio em espaço de kernel, certifique-se de que `uio_segflg == UIO_SYSSPACE` e que seus caminhos de código conhecem a diferença.

### "Meus contadores estão errados sob escritas concorrentes"

Dois escritores disputaram `bufused`. Cada um leu o valor atual, somou a ele e escreveu de volta, e o segundo escritor sobrescreveu a atualização do primeiro com um valor desatualizado.

Correção: adquira `sc->mtx` em torno de cada leitura-modificação-escrita de estado compartilhado. A Parte 3 torna isso um tema de primeira importância; para o Capítulo 9, um único mutex em torno de toda a seção crítica é suficiente.

### "Os contadores de sysctl não refletem o estado real"

Duas variantes.

**Variante A**: o contador é um `size_t`, mas a macro de sysctl é `SYSCTL_ADD_U64`. Em arquiteturas de 32 bits, a macro lê 8 bytes onde o campo tem apenas 4 bytes de largura; metade do valor é lixo.

Correção: faça a macro de sysctl corresponder ao tipo do campo. `size_t` se emparelha com `SYSCTL_ADD_UINT` em plataformas de 32 bits e `SYSCTL_ADD_U64` em plataformas de 64 bits. Para portabilidade, use `uint64_t` para contadores e converta ao atualizar.

**Variante B**: o contador nunca é atualizado porque a atualização está dentro do bloco `if (error == 0)` e `uiomove` retornou um erro diferente de zero. Esse é, na verdade, o comportamento correto: você não deve contar bytes que não foram movidos. O sintoma só parece um bug se você estiver tentando usar o contador para depurar o erro.

Correção: adicione um contador `error_count` que incrementa a cada retorno diferente de zero, independentemente de `bytes_read` e `bytes_written`. Útil para depuração.

### "A primeira leitura após um carregamento limpo retorna zero bytes"

Normalmente intencional. No Estágio 3, um buffer vazio retorna zero bytes. Se você esperava a mensagem estática do Estágio 1, verifique que está executando o driver do Estágio 1, não um posterior.

Se for não intencional, verifique se `attach` está definindo `sc->buf`, `sc->buflen` e `sc->message_len` conforme esperado. Um bug comum é copiar o código de attach do Estágio 1 para o Estágio 2 e deixar a atribuição `sc->message = ...` no lugar, que então tem precedência sobre a linha de `malloc`.

### "O build falha com referência desconhecida a uiomove_frombuf"

Você esqueceu de incluir `<sys/uio.h>`. Adicione-o ao topo de `myfirst.c`.

### "Meu handler é chamado duas vezes para uma única read(2)"

Quase certamente não é. O mais provável: seu handler está sendo chamado uma vez com `uio_iovcnt > 1` (uma chamada `readv(2)`), e internamente o `uiomove` está esvaziando cada entrada de iovec em sequência. O laço interno em `uiomove` pode fazer múltiplas chamadas `copyout` dentro de uma única invocação do seu handler.

Verifique adicionando um `device_printf` na entrada e na saída do seu `d_read`. Você deve ver uma entrada e uma saída por chamada `read(2)` em espaço de usuário, independentemente do número de iovecs.



## Padrões Contrastados: Handlers Corretos vs. Bugados

O guia de solução de problemas acima é reativo: ele ajuda quando algo já deu errado. Esta seção é o complemento prescritivo. Cada entrada mostra uma forma plausível, porém errada, de escrever parte de um handler, a contrasta com a reescrita correta e explica a distinção. Estudar os contrastes com antecedência é a maneira mais rápida de evitar os bugs logo de início.

Leia cada par com atenção. A versão correta é o padrão que você deve usar como referência; a versão bugada é a forma que suas próprias mãos podem produzir quando você está trabalhando rápido. Reconhecer o erro na prática, meses depois, vale os cinco minutos que leva para internalizar a diferença hoje.

### Contraste 1: Retornando uma Contagem de Bytes

**Bugado:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... */
        error = uiomove_frombuf(sc->message, sc->message_len, uio);
        if (error)
                return (error);
        return (sc->message_len); /* BAD: returning a count */
}
```

**Correto:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* ... */
        return (uiomove_frombuf(sc->message, sc->message_len, uio));
}
```

**Por que importa.** O valor de retorno do handler é um errno, não uma contagem. O kernel calcula a contagem de bytes a partir da mudança em `uio->uio_resid` e a reporta ao espaço do usuário. Um retorno positivo diferente de zero é interpretado como um errno; se você retornar `sc->message_len`, o chamador receberá um valor de `errno` muito estranho. Por exemplo, retornar `75` se manifestaria como `errno = 75`, que no FreeBSD corresponde a `EPROGMISMATCH`. O bug é ao mesmo tempo errado e profundamente confuso para qualquer pessoa que o analise pelo lado do usuário.

A regra é simples e absoluta: handlers retornam valores de errno, nunca contagens. Se você quiser saber a contagem de bytes, calcule-a a partir da uio.

### Contraste 2: Tratando uma Requisição de Tamanho Zero

**Bugado:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        if (uio->uio_resid == 0)
                return (EINVAL); /* BAD: zero-length is legal */
        /* ... */
}
```

**Correto:**

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        /* No special case. uiomove handles zero-resid cleanly. */
        return (uiomove_frombuf(sc->message, sc->message_len, uio));
}
```

**Por que importa.** Uma chamada `read(fd, buf, 0)` é UNIX válido. Um driver que a rejeita com `EINVAL` quebra programas que usam leituras de zero bytes para verificar o estado do descritor. `uiomove` retorna zero imediatamente se a uio não tem nada a mover; seu handler não precisa tratar esse caso especialmente. Tratá-lo de forma errada é pior do que não tratá-lo de forma alguma.

### Contraste 3: Aritmética de Capacidade do Buffer

**Bugado:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - sc->bufused;
towrite = uio->uio_resid;            /* BAD: no clamp */
error = uiomove(sc->buf + sc->bufused, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
mtx_unlock(&sc->mtx);
return (error);
```

**Correto:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - sc->bufused;
if (avail == 0) {
        mtx_unlock(&sc->mtx);
        return (ENOSPC);
}
towrite = MIN((size_t)uio->uio_resid, avail);
error = uiomove(sc->buf + sc->bufused, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
mtx_unlock(&sc->mtx);
return (error);
```

**Por que isso importa.** A versão com bug passa para `uiomove` um comprimento igual a `uio_resid`, que pode exceder a capacidade restante do buffer. `uiomove` não moverá mais do que `uio_resid` bytes, mas o *destino* é `sc->buf + sc->bufused`, e o cálculo não leva em conta `sc->buflen`. Se o usuário escrever 8 KiB em um buffer de 4 KiB com `bufused = 0`, o handler vai escrever 4 KiB além do final de `sc->buf`. Isso é um clássico kernel heap overflow: a falha não será imediata, não implicará o seu driver, e pode se manifestar como um panic dentro de um subsistema completamente não relacionado meio segundo depois.

A versão correta limita a transferência a `avail`, garantindo que a aritmética de ponteiros permaneça dentro do buffer. Esse limite é uma chamada a `MIN`, e não é opcional.

### Contraste 4: Segurando um Spin Lock Durante `uiomove`

**Com bug:**

```c
mtx_lock_spin(&sc->spin);            /* BAD: spin lock, not a regular mutex */
error = uiomove(sc->buf + off, n, uio);
mtx_unlock_spin(&sc->spin);
return (error);
```

**Correto:**

```c
mtx_lock(&sc->mtx);                  /* MTX_DEF mutex */
error = uiomove(sc->buf + off, n, uio);
mtx_unlock(&sc->mtx);
return (error);
```

**Por que isso importa.** `uiomove(9)` pode dormir. Quando chama `copyin` ou `copyout`, a página do usuário pode ter sido paginada para disco, e o kernel pode precisar trazê-la de volta, o que exige aguardar por I/O. Dormir enquanto se segura um spin lock (`MTX_SPIN`) causa deadlock no sistema. O framework `WITNESS` do FreeBSD entra em panic na primeira vez que isso acontece, se `WITNESS` estiver habilitado. Em um kernel sem `WITNESS`, o resultado é um livelock silencioso.

A regra é direta: spin locks não podem ser segurados durante chamadas a funções que podem dormir, e `uiomove` pode dormir. Use um mutex `MTX_DEF` (o padrão, e o que `myfirst` usa) para o estado do softc que é acessado por handlers de I/O.

### Contraste 5: Reiniciando estado compartilhado em `d_open`

**Com bug:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        /* ... */
        mtx_lock(&sc->mtx);
        sc->bufused = 0;                 /* BAD: wipes other readers' data */
        sc->bufhead = 0;
        mtx_unlock(&sc->mtx);
        /* ... */
}
```

**Correto:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        struct myfirst_fh *fh;
        /* ... no shared-state reset ... */
        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* fh starts zeroed, which is correct per-descriptor state */
        /* ... register fh with devfs_set_cdevpriv, bump counters ... */
}
```

**Por que isso importa.** `d_open` é executado uma vez por descritor. Se dois leitores abrirem o dispositivo, a segunda abertura apagará tudo o que a primeira deixou para trás. O estado global do driver (`sc->bufused`, `sc->buf`, contadores) pertence ao driver como um todo e é reiniciado apenas em `attach` e `detach`. O estado por descritor pertence ao `fh`, que `malloc(M_ZERO)` inicializa com zeros automaticamente.

Um driver que reinicia o estado compartilhado em `d_open` parece funcionar corretamente quando há apenas um usuário e corrompe silenciosamente o estado quando dois usuários aparecem. O bug permanece invisível até o dia em que dois usuários lerem o dispositivo ao mesmo tempo.

### Contraste 6: Contabilizando antes de conhecer o resultado

**Com bug:**

```c
sc->bytes_written += towrite;       /* BAD: count before success */
error = uiomove(sc->buf + tail, towrite, uio);
if (error == 0)
        sc->bufused += towrite;
```

**Correto:**

```c
error = uiomove(sc->buf + tail, towrite, uio);
if (error == 0) {
        sc->bufused += towrite;
        sc->bytes_written += towrite;
}
```

**Por que isso importa.** Se `uiomove` falhar no meio do processo, alguns bytes podem ter sido transferidos e outros não. O contador `sc->bytes_written` deve refletir o que realmente chegou ao buffer, não o que o driver tentou transferir. Atualizar contadores antes de conhecer o resultado faz com que os contadores mintam. Se um usuário lê o sysctl para diagnosticar um problema, verá números que não correspondem à realidade.

A regra: atualize os contadores dentro do ramo `if (error == 0)`, para que apenas o caminho de sucesso os incremente. Esse é um custo pequeno por um grande benefício de correção.

### Contraste 7: Desreferenciando um ponteiro do usuário diretamente

**Com bug:**

```c
/* Imagine the driver somehow gets a user pointer, maybe through ioctl. */
static int
handle_user_string(void *user_ptr)
{
        char buf[128];
        memcpy(buf, user_ptr, 128);     /* BAD: user pointer in memcpy */
        /* ... */
}
```

**Correto:**

```c
static int
handle_user_string(void *user_ptr)
{
        char buf[128];
        int error;

        error = copyin(user_ptr, buf, sizeof(buf));
        if (error != 0)
                return (error);
        /* ... */
}
```

**Por que isso importa.** `memcpy` assume que ambos os ponteiros se referem a memória acessível no espaço de endereçamento atual. Um ponteiro do usuário não obedece a essa premissa. Dependendo da plataforma, o resultado de passar um ponteiro do usuário para `memcpy` em contexto do kernel varia de uma falha equivalente a `EFAULT` (em amd64 com SMAP habilitado) até corrupção silenciosa de dados (em plataformas sem separação usuário/kernel), chegando a um kernel panic puro e simples.

`copyin` e `copyout` são a única forma correta de acessar memória do usuário a partir do contexto do kernel. Eles instalam um handler de falha, validam o endereço, percorrem as tabelas de páginas com segurança e retornam `EFAULT` em qualquer falha. O custo de desempenho são algumas instruções extras; o benefício de correção é que o kernel não entra em panic quando um programa do usuário com bug está em execução.

### Contraste 8: Vazando uma estrutura por abertura em falha de `d_open`

**Com bug:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_fh *fh;
        int error;

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* ... set fields ... */
        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0)
                return (error);         /* BAD: fh is leaked */
        return (0);
}
```

**Correto:**

```c
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct myfirst_fh *fh;
        int error;

        fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
        /* ... set fields ... */
        error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
        if (error != 0) {
                free(fh, M_DEVBUF);     /* free before returning */
                return (error);
        }
        return (0);
}
```

**Por que isso importa.** Quando `devfs_set_cdevpriv` falha, o kernel não registra o destrutor, de modo que ele nunca será executado para este `fh`. Se o handler retornar sem liberar `fh`, a memória é vazada. Sob carga constante, falhas repetidas em `d_open` podem vazar memória suficiente para desestabilizar o kernel.

A regra: nos caminhos de tratamento de erro, toda alocação feita até aquele ponto deve ser liberada. O leitor do Capítulo 8 já viu esse padrão em attach; ele se aplica igualmente a `d_open`.

### Como usar esta tabela de comparação

Esses oito pares não são uma lista exaustiva. São os bugs que aparecem com mais frequência nos primeiros drivers de estudantes, e são os bugs que o texto deste capítulo tentou ajudá-lo a evitar. Leia-os uma vez agora. Antes de escrever seu primeiro driver real fora deste livro, leia-os novamente.

Um hábito útil durante o desenvolvimento: sempre que terminar um handler, passe-o mentalmente pela tabela de comparação. O handler retorna uma contagem de bytes? Ele trata especialmente o caso de resid zero? Tem um limite de capacidade? O tipo do mutex está correto? Ele reinicia o estado compartilhado em `d_open`? Ele contabiliza bytes em caso de falha? Ele desreferencia algum ponteiro do usuário diretamente? Ele vaza memória em falha de `d_open`? Oito perguntas, cinco minutos. O custo da verificação é pequeno; o custo de colocar em produção um desses bugs é grande.



## Autoavaliação antes do capítulo 10

O Capítulo 9 cobriu muito terreno. Antes de encerrá-lo, passe pela lista de verificação a seguir. Se algum item fizer você hesitar, vale a pena reler a seção correspondente antes de continuar. Isso não é um teste; é uma forma rápida de identificar os pontos onde seu modelo mental ainda pode estar frágil.

**Conceitos:**

- [ ] Consigo explicar em uma frase para que serve `struct uio`.
- [ ] Consigo nomear os três campos de `struct uio` que meu driver lê com mais frequência.
- [ ] Consigo explicar por que `uiomove(9)` é preferido em relação a `copyin` / `copyout` dentro de `d_read` e `d_write`.
- [ ] Consigo explicar por que `memcpy` através da fronteira usuário/kernel é inseguro.
- [ ] Consigo explicar a diferença entre `ENXIO`, `EAGAIN`, `ENOSPC` e `EFAULT` em termos de driver.

**Mecânica:**

- [ ] Consigo escrever um handler `d_read` mínimo que serve um buffer fixo usando `uiomove_frombuf(9)`.
- [ ] Consigo escrever um handler `d_write` mínimo que adiciona dados a um buffer do kernel com um limite de capacidade correto.
- [ ] Sei onde colocar a aquisição e a liberação do mutex em torno da transferência.
- [ ] Sei como propagar um errno de `uiomove` de volta ao espaço do usuário.
- [ ] Sei como marcar uma escrita como totalmente consumida com `uio_resid = 0`.

**Observabilidade:**

- [ ] Consigo ler `sysctl dev.myfirst.0.stats` e interpretar cada contador.
- [ ] Consigo identificar um vazamento de memória com `vmstat -m | grep devbuf`.
- [ ] Consigo usar `truss(1)` para ver quais syscalls meu programa de teste realiza.
- [ ] Consigo usar `fstat(1)` ou `fuser(8)` para descobrir quem está segurando um descritor.

**Armadilhas:**

- [ ] Não retornaria uma contagem de bytes em `d_read` / `d_write`.
- [ ] Não rejeitaria uma requisição de comprimento zero com `EINVAL`.
- [ ] Não reiniciaria `sc->bufused` dentro de `d_open`.
- [ ] Não seguraria um spin lock durante uma chamada a `uiomove`.

Qualquer "não" aqui é um sinal, não um veredicto. Releia a seção relevante; faça um pequeno experimento no seu laboratório; volte à lista. Quando todas as caixas estiverem marcadas, você estará solidamente pronto para o Capítulo 10.



## Encerrando

Você acabou de implementar os pontos de entrada que dão vida a um driver. No final do Capítulo 7, seu driver tinha um esqueleto. No final do Capítulo 8, tinha uma porta bem formada. Agora, ao final do Capítulo 9, os dados fluem pela porta nos dois sentidos.

A lição central do capítulo é mais curta do que parece. Todo `d_read` que você escrever terá a mesma estrutura de três linhas: obter o estado por abertura, verificar atividade, chamar `uiomove`. Todo `d_write` que você escrever terá uma estrutura semelhante, com uma decisão extra (quanto espaço tenho?) e um limite (`MIN(uio_resid, avail)`) que evita estouro de buffer. Todo o resto do capítulo é contexto: por que `struct uio` tem a forma que tem, por que `uiomove` é o único mecanismo seguro de transferência, por que os valores de errno importam, por que os contadores importam, por que o buffer precisa ser liberado em cada caminho de erro.

### As três ideias mais importantes

**Primeiro, `struct uio` é o contrato entre o seu driver e a maquinaria de I/O do kernel.** Ele carrega tudo o que seu handler precisa saber sobre uma chamada: o que o usuário pediu, onde está a memória do usuário, em que direção a transferência deve ocorrer e quanto progresso já foi feito. Você não precisa memorizar todos os sete campos. Você precisa reconhecer `uio_resid` (o trabalho restante), `uio_offset` (a posição, se isso importar) e `uio_rw` (a direção), e precisa confiar que `uiomove(9)` cuida do restante.

**Segundo, `uiomove(9)` é a fronteira entre a memória do usuário e a memória do kernel.** Tudo o que seu driver transfere entre os dois passa por ele (ou por um de seus parentes próximos: `uiomove_frombuf`, `copyin`, `copyout`). Isso não é uma sugestão. O acesso direto por ponteiro através da fronteira de confiança ou corrompe a memória ou vaza informações, e o kernel não tem como detectar o erro de forma barata antes que ele se torne um CVE. Se um ponteiro veio do espaço do usuário, roteie-o pelas funções de fronteira de confiança do kernel. Sempre.

**Terceiro, um handler correto costuma ser curto.** Se o seu `d_read` ou `d_write` tiver mais de quinze linhas, provavelmente há algo errado. Handlers mais longos ou duplicam lógica que pertence a outro lugar (no gerenciamento do buffer, na configuração do estado por abertura, nos sysctls), ou estão tentando fazer algo que o driver não deveria fazer em um handler de caminho de dados (tipicamente algo que pertence a `d_ioctl`). Mantenha os handlers curtos. Coloque a maquinaria que eles chamam em funções auxiliares com nomes claros. O seu eu futuro vai agradecer.

### A forma do driver com que você encerra o capítulo

O `myfirst` do Estágio 3 é um FIFO em memória, pequeno e honesto. As características mais relevantes:

- Um buffer de 4 KiB no kernel, alocado em `attach` e liberado em `detach`.
- Um mutex por instância protegendo `bufhead`, `bufused` e os contadores associados.
- Um `d_read` que esvazia o buffer e avança `bufhead`, colapsando para zero quando o buffer fica vazio.
- Um `d_write` que acrescenta dados ao buffer e retorna `ENOSPC` quando ele enche.
- Contadores por descritor armazenados em `struct myfirst_fh`, alocados em `d_open` e liberados no destrutor.
- Uma árvore sysctl que expõe o estado ativo do driver.
- Tratamento limpo de erro em `attach` e ordenação limpa em `detach`.

Essa forma voltará, reconhecível, em metade dos drivers que você lerá na Parte 4 e na Parte 6. É um padrão geral, não uma demonstração pontual.

### O que praticar antes de começar o capítulo 10

Cinco exercícios, em ordem aproximada de dificuldade crescente:

1. Reconstrua os três estágios do zero, sem olhar para a árvore de exemplos. Compare seu resultado com a árvore depois; as diferenças são o que você ainda precisa internalizar.
2. Introduza um bug intencional no Estágio 3: esqueça de reiniciar `bufhead` quando `bufused` chegar a zero. Observe o que acontece na segunda escrita grande. Explique o sintoma em termos do código.
3. Adicione um sysctl que expõe `sc->buflen`. Torne-o somente leitura. Em seguida, converta-o em um tunable que possa ser definido no momento do carregamento via `kenv` ou `loader.conf` e lido em `attach`. (O Capítulo 10 aborda tunables formalmente; esta é uma prévia.)
4. Escreva um script de shell que grave dados aleatórios de comprimento conhecido em `/dev/myfirst/0` e depois os leia de volta através de `sha256`. Compare os hashes. Os hashes coincidem mesmo quando o tamanho da escrita excede o buffer? (Não deveriam; pense no porquê.)
5. Encontre um driver em `/usr/src/sys/dev` que implemente tanto `d_read` quanto `d_write`. Leia seus handlers. Mapeie-os em relação aos padrões deste capítulo. Bons candidatos: `/usr/src/sys/dev/null/null.c` (você já o conhece), `/usr/src/sys/dev/random/randomdev.c`, `/usr/src/sys/dev/speaker/spkr.c`.

### O que vem no capítulo 10

O Capítulo 10 pega o driver do Estágio 3 e faz com que ele escale. Quatro novas capacidades aparecem:

- **Um buffer circular** substitui o buffer linear. Escritas e leituras podem acontecer continuamente sem o colapso explícito que o Estágio 3 usa.
- **Leituras bloqueantes** chegam. Um leitor que chama `read(2)` em um buffer vazio pode dormir até que dados estejam disponíveis, em vez de retornar zero bytes imediatamente. O primitivo do kernel é `msleep(9)`; o handler `d_purge` é a rede de segurança para o encerramento.
- **I/O não bloqueante** torna-se um recurso de primeira classe. Usuários com `O_NONBLOCK` recebem `EAGAIN` onde um chamador bloqueante dormiria.
- **Integração com `poll(2)` e `kqueue(9)`**. Um programa do usuário pode aguardar que o dispositivo se torne legível ou gravável sem tentar ativamente a operação. Essa é a forma padrão de integrar um dispositivo a um loop de eventos.

Todos os quatro se baseiam nas mesmas estruturas de `d_read` / `d_write` que você acabou de implementar. Você irá estender os handlers em vez de reescrevê-los, e o estado por descritor que você já tem em funcionamento carregará o controle necessário.

Antes de fechar o arquivo, uma última palavra de encorajamento. O material deste capítulo não é tão difícil quanto pode parecer em uma primeira leitura. O padrão é pequeno. As ideias são reais, mas são finitas, e você acabou de exercitar cada uma delas com código funcional. Quando você ler o `d_read` ou o `d_write` de um driver real na árvore de código-fonte, você reconhecerá o que a função está fazendo e por quê. Você não é mais um iniciante nisso. Você é um aprendiz com uma ferramenta real nas mãos.

## Referência: As Assinaturas e Funções Auxiliares Usadas Neste Capítulo

Uma referência consolidada para as declarações, funções auxiliares e constantes utilizadas ao longo do capítulo. Mantenha esta página marcada enquanto escreve drivers; a maioria das dúvidas de iniciantes é resolvida com uma consulta a uma dessas tabelas.

### Assinaturas de `d_read` e `d_write`

De `/usr/src/sys/sys/conf.h`:

```c
typedef int d_read_t(struct cdev *dev, struct uio *uio, int ioflag);
typedef int d_write_t(struct cdev *dev, struct uio *uio, int ioflag);
```

O valor de retorno é zero em caso de sucesso e um errno positivo em caso de falha. A contagem de bytes é calculada a partir da variação em `uio->uio_resid` e reportada ao espaço do usuário como valor de retorno de `read(2)` / `write(2)`.

### O `struct uio` Canônico

De `/usr/src/sys/sys/uio.h`:

```c
struct uio {
        struct  iovec *uio_iov;         /* scatter/gather list */
        int     uio_iovcnt;             /* length of scatter/gather list */
        off_t   uio_offset;             /* offset in target object */
        ssize_t uio_resid;              /* remaining bytes to process */
        enum    uio_seg uio_segflg;     /* address space */
        enum    uio_rw uio_rw;          /* operation */
        struct  thread *uio_td;         /* owner */
};
```

### As Enumerações `uio_seg` e `uio_rw`

De `/usr/src/sys/sys/_uio.h`:

```c
enum uio_rw  { UIO_READ, UIO_WRITE };
enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_NOCOPY };
```

### Família `uiomove`

De `/usr/src/sys/sys/uio.h`:

```c
int uiomove(void *cp, int n, struct uio *uio);
int uiomove_frombuf(void *buf, int buflen, struct uio *uio);
int uiomove_fromphys(struct vm_page *ma[], vm_offset_t offset, int n,
                     struct uio *uio);
int uiomove_nofault(void *cp, int n, struct uio *uio);
int uiomove_object(struct vm_object *obj, off_t obj_size, struct uio *uio);
```

No código de driver para iniciantes, apenas `uiomove` e `uiomove_frombuf` são de uso comum. Os demais oferecem suporte a subsistemas específicos do kernel (I/O de páginas físicas, cópias sem page fault, objetos respaldados por VM) e estão fora do escopo deste capítulo.

### `copyin` e `copyout`

De `/usr/src/sys/sys/systm.h`:

```c
int copyin(const void * __restrict udaddr,
           void * __restrict kaddr, size_t len);
int copyout(const void * __restrict kaddr,
            void * __restrict udaddr, size_t len);
int copyinstr(const void * __restrict udaddr,
              void * __restrict kaddr, size_t len,
              size_t * __restrict lencopied);
```

Use essas funções nos caminhos de controle (`d_ioctl`) onde um ponteiro de usuário chega fora da abstração uio. Dentro de `d_read` e `d_write`, prefira `uiomove`.

### Bits de `ioflag` Relevantes para Dispositivos de Caracteres

De `/usr/src/sys/sys/vnode.h`:

```c
#define IO_NDELAY       0x0004  /* FNDELAY flag set in file table */
```

Definido quando o descritor está em modo não bloqueante. Seu `d_read` ou `d_write` pode usar esse bit para decidir se deve bloquear (flag ausente) ou retornar `EAGAIN` (flag definido). A maioria dos outros flags `IO_*` é de nível de sistema de arquivos e não tem relevância para dispositivos de caracteres.

### Alocação de Memória

De `/usr/src/sys/sys/malloc.h`:

```c
void *malloc(size_t size, struct malloc_type *type, int flags);
void  free(void *addr, struct malloc_type *type);
```

Flags comuns: `M_WAITOK`, `M_NOWAIT`, `M_ZERO`. Tipos comuns para drivers: `M_DEVBUF` (genérico) ou um tipo específico do driver declarado via `MALLOC_DECLARE` / `MALLOC_DEFINE`.

### Estado por Abertura (herdado do Capítulo 8, utilizado aqui)

De `/usr/src/sys/sys/conf.h`:

```c
int  devfs_set_cdevpriv(void *priv, d_priv_dtor_t *dtr);
int  devfs_get_cdevpriv(void **datap);
void devfs_clear_cdevpriv(void);
```

O padrão é: alocar em `d_open`, registrar com `devfs_set_cdevpriv`, recuperar em cada handler seguinte com `devfs_get_cdevpriv` e liberar no destrutor que `devfs_set_cdevpriv` registrou.

### Valores de Errno Usados Neste Capítulo

| Errno         | Significado no contexto de um driver                              |
|---------------|-------------------------------------------------------------------|
| `0`           | Sucesso.                                                           |
| `ENXIO`       | Dispositivo não configurado (softc ausente, não conectado).        |
| `EFAULT`      | Endereço de usuário inválido. Geralmente propagado a partir de `uiomove`. |
| `EIO`         | Erro de entrada/saída. Problema de hardware.                       |
| `ENOSPC`      | Sem espaço disponível no dispositivo. Buffer cheio.               |
| `EAGAIN`      | Bloquearia; relevante no modo não bloqueante (Capítulo 10).        |
| `EINVAL`      | Argumento inválido.                                                |
| `EACCES`      | Permissão negada em `open(2)`.                                     |
| `EPIPE`       | Pipe interrompido. Não utilizado pelo `myfirst`.                   |

### Padrões Úteis de `device_printf(9)`

```c
device_printf(sc->dev, "open via %s fh=%p\n", devtoname(sc->cdev), fh);
device_printf(sc->dev, "write rejected: buffer full (used=%zu)\n",
    sc->bufused);
device_printf(sc->dev, "read delivered %zd bytes\n",
    (ssize_t)(before - uio->uio_offset));
```

Esses padrões são escritos para facilitar a leitura. Uma linha no `dmesg` que você precisa decodificar é uma linha que provavelmente não será lida quando importar.

### As Três Etapas em Resumo

| Etapa | `d_read`                                                      | `d_write`                                       |
|-------|---------------------------------------------------------------|-------------------------------------------------|
| 1     | Serve mensagem fixa via `uiomove_frombuf`                     | Descarta escritas (como `/dev/null`)            |
| 2     | Serve buffer até `bufused`                                    | Acrescenta ao buffer, `ENOSPC` se cheio         |
| 3     | Esvazia buffer a partir de `bufhead`, reinicia ao vazio       | Acrescenta em `bufhead + bufused`, `ENOSPC` se cheio |

A Etapa 3 é a base sobre a qual o Capítulo 10 é construído.

### Lista Consolidada de Arquivos do Capítulo

Arquivos complementares em `examples/part-02/ch09-reading-and-writing/`:

- `stage1-static-message/`: código-fonte e Makefile do driver da Etapa 1.
- `stage2-readwrite/`: código-fonte e Makefile do driver da Etapa 2.
- `stage3-echo/`: código-fonte e Makefile do driver da Etapa 3.
- `userland/rw_myfirst.c`: pequeno programa C para exercitar ciclos de leitura e escrita.
- `userland/stress_rw.c`: teste de estresse multiprocesso para o Laboratório 9.3 e além.
- `README.md`: um mapa resumido da árvore de arquivos complementares.

Cada etapa é independente; você pode construir, carregar e exercitar qualquer uma delas sem precisar construir as demais. Os Makefiles são idênticos, exceto pelo nome do driver (sempre `myfirst`) e por flags opcionais de ajuste.



## Apêndice A: Uma Análise Detalhada do Loop Interno de `uiomove`

Para quem quiser ver exatamente o que `uiomove(9)` faz, este apêndice percorre o loop principal de `uiomove_faultflag` conforme ele aparece em `/usr/src/sys/kern/subr_uio.c`. Você não precisa ler isso para escrever um driver. Este apêndice existe porque uma leitura atenta do loop esclarecerá todas as dúvidas futuras sobre a semântica do uio.

### A Configuração

Na entrada, a função possui:

- Um ponteiro do kernel `cp` fornecido pelo chamador (seu driver).
- Um inteiro `n` fornecido pelo chamador (o máximo de bytes a transferir).
- O uio fornecido pelo despacho do kernel.
- Um booleano `nofault` que indica se page faults durante a cópia devem ser tratados ou são fatais.

A função verifica alguns invariantes: a direção é `UIO_READ` ou `UIO_WRITE`, a thread proprietária é a thread atual quando o segmento é de espaço do usuário e `uio_resid` é não negativo. Qualquer violação dispara um `KASSERT` e causará um panic no kernel com `INVARIANTS` ativado.

### O Loop Principal

```c
while (n > 0 && uio->uio_resid) {
        iov = uio->uio_iov;
        cnt = iov->iov_len;
        if (cnt == 0) {
                uio->uio_iov++;
                uio->uio_iovcnt--;
                continue;
        }
        if (cnt > n)
                cnt = n;

        switch (uio->uio_segflg) {
        case UIO_USERSPACE:
                switch (uio->uio_rw) {
                case UIO_READ:
                        error = copyout(cp, iov->iov_base, cnt);
                        break;
                case UIO_WRITE:
                        error = copyin(iov->iov_base, cp, cnt);
                        break;
                }
                if (error)
                        goto out;
                break;

        case UIO_SYSSPACE:
                switch (uio->uio_rw) {
                case UIO_READ:
                        bcopy(cp, iov->iov_base, cnt);
                        break;
                case UIO_WRITE:
                        bcopy(iov->iov_base, cp, cnt);
                        break;
                }
                break;
        case UIO_NOCOPY:
                break;
        }
        iov->iov_base = (char *)iov->iov_base + cnt;
        iov->iov_len -= cnt;
        uio->uio_resid -= cnt;
        uio->uio_offset += cnt;
        cp = (char *)cp + cnt;
        n -= cnt;
}
```

Cada iteração realiza uma unidade de trabalho: copiar até `cnt` bytes (onde `cnt` é `MIN(iov->iov_len, n)`) entre a entrada atual do iovec e o buffer do kernel. A direção é escolhida pelos dois comandos `switch` aninhados. Após uma cópia bem-sucedida, todos os campos de contabilidade avançam em sincronia: a entrada do iovec diminui em `cnt`, o resid do uio diminui em `cnt`, o offset do uio cresce em `cnt`, o ponteiro do kernel `cp` avança em `cnt` e o `n` do chamador diminui em `cnt`.

Quando uma entrada do iovec é completamente esvaziada (`cnt == 0` no início do loop), a função avança para a próxima entrada. Quando o `n` do chamador chega a zero ou o resid do uio chega a zero, o loop termina.

Se `copyin` ou `copyout` retornar um valor diferente de zero, a função salta para `out` sem atualizar os campos daquela iteração, de modo que a contabilidade de cópia parcial permanece consistente: os bytes que foram copiados estão refletidos em `uio_resid`, e os que não foram copiados ainda estão pendentes.

### O Que Você Deve Reter

Três invariantes decorrem do loop que são relevantes para o código do seu driver.

- **Sua chamada a `uiomove(cp, n, uio)` transfere no máximo `MIN(n, uio->uio_resid)` bytes.** Não há como solicitar mais do que o uio comporta; a função limita ao menor dos dois valores.
- **Em uma transferência parcial, o estado permanece consistente.** `uio_resid` reflete exatamente os bytes que não foram transferidos. Você pode fazer outra chamada e ela continuará corretamente de onde parou.
- **O tratamento de falhas está dentro do loop, não ao redor dele.** Uma falha durante um `copyin` / `copyout` retorna `EFAULT` para o restante; os campos continuam consistentes.

Esses três fatos explicam por que a estrutura de três linhas à qual sempre retornamos (`uiomove`, verificar erro, atualizar estado) é suficiente. O kernel realiza o trabalho complexo dentro do loop; seu driver só precisa cooperar.



## Apêndice B: Por Que `read(fd, buf, 0)` É Permitido

Uma breve nota sobre uma dúvida frequente: por que UNIX permite uma chamada `read(fd, buf, 0)` ou `write(fd, buf, 0)`?

Há duas respostas, e ambas valem a pena conhecer.

**A resposta prática**: I/O de comprimento zero é um teste gratuito. Um programa no espaço do usuário que deseja verificar se um descritor está em estado razoável pode chamar `read(fd, NULL, 0)` sem se comprometer com uma transferência real. Se o descritor estiver quebrado, a chamada retorna um erro. Se estiver íntegro, retorna zero com custo quase nulo.

**A resposta semântica**: a interface de I/O do UNIX usa contagens de bytes de forma consistente, e tratar zero como caso especial dá mais trabalho do que simplesmente permitir. Uma chamada com `count == 0` é um no-op bem definido: o kernel não precisa fazer nada e pode retornar zero imediatamente. A alternativa, retornar `EINVAL` para chamadas com contagem zero, forçaria todo programa que calcula dinamicamente uma contagem a se proteger desse caso. Esse é o tipo de mudança que quebra décadas de código sem nenhum benefício.

A consequência do lado do driver, que mencionamos anteriormente: seu handler não deve entrar em panic nem retornar erro para um `uio_resid` igual a zero. O kernel trata esse caso efetivamente por você quando você usa `uiomove`, que retorna zero imediatamente se não há nada a transferir.

Se você se pegar escrevendo `if (uio->uio_resid == 0) return (EINVAL);` em um driver, pare. Essa é a resposta errada. I/O de contagem zero é válido; retorne zero.



## Apêndice C: Uma Breve Visita ao Caminho de Leitura de `/dev/zero`

Como análise final, vale a pena percorrer exatamente o que acontece quando um programa no espaço do usuário chama `read(2)` em `/dev/zero`. O driver é `/usr/src/sys/dev/null/null.c` e o handler é `zero_read`. Uma vez que você entenda esse caminho, você terá compreendido tudo o que foi abordado no Capítulo 9.

### Do Espaço do Usuário ao Despacho do Kernel

O usuário chama:

```c
ssize_t n = read(fd, buf, 1024);
```

A biblioteca C realiza o syscall `read`. O kernel consulta `fd` na tabela de arquivos do processo chamador, recupera o `struct file`, identifica seu vnode e despacha a chamada para o devfs.

O devfs identifica o cdev associado ao vnode, adquire uma referência sobre ele e chama seu ponteiro de função `d_read` (`zero_read`) com o uio que o kernel preparou.

### Dentro de `zero_read`

```c
static int
zero_read(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
        void *zbuf;
        ssize_t len;
        int error = 0;

        KASSERT(uio->uio_rw == UIO_READ,
            ("Can't be in %s for write", __func__));
        zbuf = __DECONST(void *, zero_region);
        while (uio->uio_resid > 0 && error == 0) {
                len = uio->uio_resid;
                if (len > ZERO_REGION_SIZE)
                        len = ZERO_REGION_SIZE;
                error = uiomove(zbuf, len, uio);
        }
        return (error);
}
```

- Verificar que a direção está correta. Boa prática; um `KASSERT` não custa nada em kernels de produção.
- Definir `zbuf` para apontar para `zero_region`, uma grande área pré-alocada preenchida com zeros.
- Loop: enquanto o chamador quiser mais bytes, determinar o tamanho da transferência (mínimo entre `uio_resid` e o tamanho da região de zeros), chamar `uiomove` e acumular qualquer erro.
- Retornar.

### Dentro de `uiomove`

Na primeira iteração, `uiomove` vê `uio_resid = 1024`, `len = 1024` (já que `ZERO_REGION_SIZE` é muito maior), `uio_segflg = UIO_USERSPACE`, `uio_rw = UIO_READ`. Ele seleciona `copyout(zbuf, buf, 1024)`. O kernel realiza a cópia, tratando qualquer page fault no buffer do usuário. Com sucesso, `uio_resid` cai para zero, `uio_offset` cresce em 1024 e o iovec é completamente consumido.

### De Volta à Pilha de Chamadas

`uiomove` retorna zero. O loop em `zero_read` vê `uio_resid == 0` e encerra. `zero_read` retorna zero.

O devfs libera sua referência no cdev. O kernel calcula a contagem de bytes como `1024 - 0 = 1024`. `read(2)` retorna 1024 para o usuário.

O buffer do usuário agora contém 1024 bytes zerados.

### O Que Isso Revela Sobre Seu Próprio Driver

Duas observações.

Primeiro, cada decisão no caminho de dados de `zero_read` é uma decisão que você também está tomando. Quanto transferir por iteração; de qual buffer ler; como tratar o erro de `uiomove`. As escolhas do seu driver diferirão nos detalhes (seu buffer não é uma região de zeros pré-alocada, seu tamanho de bloco não é `ZERO_REGION_SIZE`), mas a estrutura é idêntica.

Segundo, tudo acima de `zero_read` é maquinaria do kernel que você não precisa escrever. Você implementa o handler, e o kernel cuida do syscall, da busca pelo descritor de arquivo, do despacho VFS, do roteamento pelo devfs, da contagem de referências e do tratamento de falhas. Esse é o poder da abstração: você acrescenta o conhecimento específico do seu driver, e todo o resto vem de graça.

O lado oposto é que, ao escrever um driver, você está se comprometendo a *cooperar* com essa maquinaria. Todo invariante do qual `uiomove` e devfs dependem passa a ser sua responsabilidade manter. O capítulo foi guiando você por esses invariantes um a um, construindo três drivers pequenos que cada um exercita um subconjunto diferente.

A esta altura, o padrão já deve ser familiar.

## Apêndice D: Valores de Retorno Comuns de `read(2)` / `write(2)` no Lado do Usuário

Um guia de referência rápida sobre o que um programa em espaço do usuário enxerga ao se comunicar com o seu driver. Isso não é código de driver; é a visão do outro lado da fronteira de confiança. Consultá-lo de tempos em tempos é a melhor vacina contra os bugs sutis que surgem quando o driver faz algo diferente do que um programa UNIX bem-comportado espera.

### `read(2)`

- Um inteiro positivo: essa quantidade de bytes foi colocada no buffer do chamador. Um valor menor do que o solicitado indica uma leitura parcial; o chamador faz um loop.
- Zero: fim de arquivo. Nenhum byte será produzido neste descritor. O chamador para.
- `-1` com `errno = EAGAIN`: modo não bloqueante, nenhum dado disponível no momento. O chamador aguarda (via `select(2)` / `poll(2)` / `kqueue(2)`) e tenta novamente.
- `-1` com `errno = EINTR`: um sinal interrompeu a leitura. O chamador normalmente tenta novamente, a menos que o handler do sinal indique o contrário.
- `-1` com `errno = EFAULT`: o ponteiro de buffer era inválido. O chamador tem um bug.
- `-1` com `errno = ENXIO`: o dispositivo foi removido. O chamador deve fechar o descritor e desistir.
- `-1` com `errno = EIO`: o dispositivo reportou um erro de hardware. O chamador pode tentar novamente ou reportar o erro.

### `write(2)`

- Um inteiro positivo: essa quantidade de bytes foi aceita. Um valor menor do que o oferecido indica uma escrita parcial; o chamador faz um loop com o restante.
- Zero: teoricamente possível, raramente visto na prática. Normalmente tratado da mesma forma que uma escrita parcial de zero bytes.
- `-1` com `errno = EAGAIN`: modo não bloqueante, sem espaço disponível no momento. O chamador aguarda e tenta novamente.
- `-1` com `errno = ENOSPC`: sem espaço permanentemente. O chamador para de escrever ou reabre o descritor.
- `-1` com `errno = EPIPE`: o leitor fechou. Relevante para dispositivos semelhantes a pipes, mas não para `myfirst`.
- `-1` com `errno = EFAULT`: o ponteiro de buffer era inválido.
- `-1` com `errno = EINTR`: interrompido por um sinal. Normalmente tentado novamente.

### O Que Isso Significa Para o Seu Driver

Duas conclusões.

Primeiro, `EAGAIN` é como os chamadores não bloqueantes esperam que um driver diga "sem dados / sem espaço agora, volte mais tarde". Um chamador não bloqueante que recebe `EAGAIN` não o trata como erro; ele aguarda uma notificação (geralmente via `poll(2)`) e tenta novamente. O Capítulo 10 implementa esse mecanismo para `myfirst`.

Segundo, `ENOSPC` é como um driver sinaliza uma condição permanente de falta de espaço em uma escrita. Ele difere de `EAGAIN` porque o chamador não espera que novas tentativas tenham sucesso em breve. Para o `myfirst` Stage 3, usamos `ENOSPC` quando o buffer está cheio e não há um leitor drenando ativamente; o Capítulo 10 vai sobrepor `EAGAIN` à mesma condição para leitores e escritores não bloqueantes.

Um driver que retorna o `errno` errado aqui é quase indistinguível de um driver com comportamento incorreto. O custo de acertar é mínimo. O custo de errar aparece em programas de usuário confusos meses depois.



## Apêndice E: O Guia de Referência em Uma Página

Se você tem apenas cinco minutos antes de começar o Capítulo 10, aqui está a versão em uma página de tudo o que vimos acima.

**As assinaturas:**

```c
static int myfirst_read(struct cdev *dev, struct uio *uio, int ioflag);
static int myfirst_write(struct cdev *dev, struct uio *uio, int ioflag);
```

Retorne zero em caso de sucesso, um errno positivo em caso de falha. Nunca retorne uma contagem de bytes.

**A estrutura em três linhas para leituras:**

```c
error = devfs_get_cdevpriv((void **)&fh);
if (error) return error;
return uiomove_frombuf(sc->buf, sc->buflen, uio);
```

Ou, para um buffer dinâmico:

```c
mtx_lock(&sc->mtx);
toread = MIN((size_t)uio->uio_resid, sc->bufused);
error = uiomove(sc->buf + offset, toread, uio);
if (error == 0) { /* update state */ }
mtx_unlock(&sc->mtx);
return error;
```

**A estrutura em três linhas para escritas:**

```c
mtx_lock(&sc->mtx);
avail = sc->buflen - (sc->bufhead + sc->bufused);
if (avail == 0) { mtx_unlock(&sc->mtx); return ENOSPC; }
towrite = MIN((size_t)uio->uio_resid, avail);
error = uiomove(sc->buf + sc->bufhead + sc->bufused, towrite, uio);
if (error == 0) { sc->bufused += towrite; }
mtx_unlock(&sc->mtx);
return error;
```

**O que lembrar sobre uio:**

- `uio_resid`: bytes ainda pendentes. `uiomove` decrementa esse valor.
- `uio_offset`: posição, se relevante. `uiomove` incrementa esse valor.
- `uio_rw`: direção. Confie em `uiomove` para usá-lo.
- Todo o resto: não mexa.

**O que não fazer:**

- Não dereferencie ponteiros de usuário diretamente.
- Não use `memcpy` / `bcopy` entre espaço do usuário e espaço do kernel.
- Não retorne contagens de bytes.
- Não redefina o estado global do driver em `d_open` / `d_close`.
- Não esqueça o `M_ZERO` no `malloc(9)`.
- Não segure um spin lock durante `uiomove`.

**Valores de errno:**

- `0`: sucesso.
- `ENXIO`: dispositivo não disponível.
- `ENOSPC`: buffer cheio (permanente).
- `EAGAIN`: bloquearia (modo não bloqueante).
- `EFAULT`: vindo de `uiomove`, propague.
- `EIO`: erro de hardware.

É isso para este capítulo.



## Resumo do Capítulo

Este capítulo construiu o caminho de dados. Partindo dos stubs do Capítulo 8, implementamos `d_read` e `d_write` em três estágios, cada um formando um driver completo e carregável.

- **Stage 1** exercitou `uiomove_frombuf(9)` contra uma string estática do kernel, com tratamento de offset por descritor que tornava o progresso de dois leitores concorrentes independentes entre si.
- **Stage 2** introduziu um buffer dinâmico no kernel, um caminho de escrita que acrescentava dados a ele e um caminho de leitura que os servia. O buffer era dimensionado no momento do attach, e um buffer cheio rejeitava novas escritas com `ENOSPC`.
- **Stage 3** transformou o buffer em uma fila FIFO. As leituras drenavam a partir do início, as escritas acrescentavam ao final, e o driver zerava `bufhead` quando o buffer ficava vazio.

Ao longo do caminho, dissecamos a `struct uio` campo a campo, explicamos por que `uiomove(9)` é a única maneira legítima de cruzar a fronteira de confiança entre espaço do usuário e espaço do kernel em um handler de leitura ou escrita, e construímos um pequeno vocabulário de valores errno que um driver bem-comportado utiliza: `ENXIO`, `EFAULT`, `ENOSPC`, `EAGAIN`, `EIO`. Percorremos o loop interno de `uiomove` para que suas garantias pareçam conquistadas, não misteriosas. E encerramos com cinco laboratórios, seis desafios, um guia de solução de problemas e um guia de referência em uma página.

O driver do Stage 3 é o ponto de passagem para o Capítulo 10. Ele move bytes corretamente. Ainda não os move com eficiência: um buffer vazio retorna zero bytes imediatamente, um buffer cheio retorna `ENOSPC` imediatamente, não há bloqueio, sem integração com `poll(2)`, sem ring buffer. O Capítulo 10 corrige tudo isso, construindo sobre exatamente as estruturas que acabamos de desenhar.

O padrão que você acabou de aprender se repete. Cada handler de I/O de dispositivo de caracteres em `/usr/src/sys/dev` é construído sobre a mesma assinatura de três argumentos, a mesma `struct uio` e o mesmo primitivo `uiomove(9)`. As diferenças entre drivers estão em como preparam os dados, não em como os movem. Agora que você reconhece a maquinaria de movimentação, cada handler que você abrir se torna legível quase imediatamente.

Você agora sabe o suficiente para ler qualquer `d_read` ou `d_write` na árvore do FreeBSD e entender o que está sendo feito. Isso é um marco significativo. Reserve um momento para reconhecer essa conquista antes de virar a página.
