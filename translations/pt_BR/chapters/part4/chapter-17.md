---
title: "Simulando Hardware"
description: "O Capítulo 17 retoma o bloco de registradores estático introduzido no Capítulo 16 e o faz se comportar como um dispositivo real: um callout aciona mudanças de estado autônomas, um protocolo disparado por escrita agenda eventos com atraso, a semântica read-to-clear espelha o hardware real e um caminho de injeção de falhas ensina o tratamento de erros sem arriscar silício real. O driver evolui de 0.9-mmio para 1.0-simulated, ganha um novo arquivo de simulação e chega à Parte 4 pronto para encontrar dispositivos PCI reais no Capítulo 18."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 17
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 195
language: "pt-BR"
---
# Simulando Hardware

## Orientação ao Leitor e Resultados Esperados

O Capítulo 16 terminou com um driver que possuía um bloco de registradores. O módulo `myfirst` na versão `0.9-mmio` carrega uma região de 64 bytes de memória do kernel moldada para se parecer com um dispositivo: dez registradores de 32 bits nomeados, um pequeno conjunto de máscaras de bits, um ID de dispositivo que retorna `0x4D594649`, uma revisão de firmware que codifica `1.0` e um registrador `STATUS` cujo bit `READY` é ativado no attach e desativado no detach. Todo acesso passa por `bus_space_read_4` e `bus_space_write_4`, encapsulados pelas macros já conhecidas `CSR_READ_4`, `CSR_WRITE_4` e `CSR_UPDATE_4`. O mutex do driver (`sc->mtx`) protege cada acesso aos registradores. Um log de acessos registra os últimos 64 acessos, uma tarefa de ticker alterna `SCRATCH_A` sob demanda e um pequeno arquivo `HARDWARE.md` documenta toda a superfície.

Esse driver já é capaz de fazer muita coisa. Ele consegue ler registradores. Ele consegue escrever registradores. Ele consegue observar seus próprios acessos, detectar offsets fora dos limites em kernels de depuração e impor a disciplina de locking que todos os capítulos posteriores irão depender. O que ele ainda não consegue fazer é se comportar como um dispositivo real. Seus bits de `STATUS` nunca mudam por conta própria. Seu registrador `DATA_IN` não inicia uma operação multi-ciclo. Seu `INTR_STATUS` não acumula eventos. Escrever em `CTRL.GO` não agenda nada. Nada na simulação tem um pulso próprio; toda mudança no bloco de registradores é consequência direta da última escrita feita pelo espaço do usuário.

Dispositivos reais não são assim. Um sensor de temperatura atualiza seu valor a cada poucos milissegundos sem que ninguém solicite. Um controlador serial gera uma interrupção quando um byte chega ao FIFO de recepção, muito tempo depois da última escrita do driver. O anel de descritores de uma placa de rede se enche de pacotes enquanto o driver dorme. As partes interessantes de um driver de dispositivo vêm de reagir a mudanças, não de produzi-las. Ensinar o leitor a escrever esses caminhos reativos requer um dispositivo simulado que produza mudanças, e o Capítulo 17 é onde a simulação aprende a fazer isso.

### O Que Este Capítulo Aborda

O escopo do Capítulo 17 é estreito, mas profundo. Ele pega o bloco de registradores estático do Capítulo 16 e adiciona quatro novas propriedades:

- Comportamento autônomo. Um callout dispara em um ritmo controlado pelo driver e atualiza bits de `STATUS`, bits de `INTR_STATUS` ou registradores de dados como se a máquina de estados interna de um dispositivo real estivesse rodando em silício. O leitor vê os valores mudarem entre duas leituras de sysctl consecutivas, mesmo sem que nenhuma escrita do espaço do usuário tenha ocorrido entre elas.

- Protocolo acionado por comando. Escrever um bit designado em `CTRL` agenda uma mudança de estado postergada. Após um atraso configurável, `STATUS.DATA_AV` é ativado e, opcionalmente, `INTR_STATUS.DATA_AV` é travado. Esse é o padrão que todo dispositivo de comando-resposta segue: uma escrita inicia algo, e uma mudança de status posterior sinaliza que aquilo terminou.

- Temporização realista. Atrasos na ordem de microssegundos usam `DELAY(9)` onde apropriado e `pause_sbt(9)` onde um sleep é seguro. Atrasos na ordem de milissegundos usam `callout_reset_sbt(9)` para que nenhuma thread fique bloqueada enquanto o dispositivo simulado trabalha. Cada escolha tem seu lugar; cada escolha tem seu custo; o Capítulo 17 ensina ambos.

- Injeção de falhas. Um sysctl permite ao leitor solicitar que a simulação falhe. As falhas assumem várias formas: a próxima leitura retorna um valor de erro fixo, a próxima escrita nunca ativa `DATA_AV`, uma fração aleatória de operações configura `STATUS.ERROR`, um bit `STATUS.BUSY` permanece ativo indefinidamente. Cada modo exercita um caminho diferente de tratamento de erros no driver, e o driver aprende a detectar, recuperar ou reportar.

O capítulo constrói essas propriedades sobre o driver do Capítulo 16 sem substituí-lo. O mesmo mapa de registradores, os mesmos acessores, o mesmo `sc->mtx`, o mesmo log de acessos. O que cresce é o backend da simulação. O arquivo `myfirst_hw.c` do Capítulo 16 ganha um arquivo irmão, `myfirst_sim.c`, que contém os callouts da simulação, os ganchos de injeção de falhas e os helpers que fazem o bloco de registradores ganhar vida. Ao final do capítulo, o driver estará na versão `1.0-simulated`, e a superfície de hardware que a simulação apresenta ao driver é rica o suficiente para exercitar quase todas as lições da Parte 3.

### Por Que o Comportamento Autônomo Merece um Capítulo Próprio

Uma pergunta natural neste ponto é se o comportamento dirigido por callout e a injeção de falhas realmente merecem um capítulo inteiro. O Capítulo 16 já colocou um bloco de registradores diante do driver, e o Capítulo 18 substituirá esse bloco por hardware PCI real. A simulação não seria apenas um degrau do qual deveríamos sair o mais rápido possível?

Três respostas, cada uma merecedora de atenção.

Primeiro, a simulação do Capítulo 16 era estática de propósito. Ela existia para permitir que o leitor praticasse o acesso a registradores sem também ter que gerenciar o comportamento do dispositivo. O Capítulo 17 é onde o comportamento do dispositivo entra em cena, e o lugar certo para introduzi-lo não é no meio do capítulo sobre PCI real, onde o leitor já está equilibrando a alocação de BAR, os IDs de fornecedor e dispositivo e o glue do newbus. Um capítulo dedicado à simulação dá ao leitor espaço para raciocinar sobre como um dispositivo se comporta, como os atrasos moldam o protocolo e como as falhas se propagam, sem as distrações que o hardware real traz.

Segundo, a simulação não é um andaime descartável após o Capítulo 18. É uma parte permanente do desenvolvimento de drivers. Drivers reais são testados contra dispositivos simulados muito depois de rodarem pela primeira vez em hardware real, porque a simulação é a única forma de produzir falhas determinísticas, temporização reproduzível e condições de falha sob demanda. Todo subsistema sério do FreeBSD tem alguma forma de simulação ou harness de teste, e todo autor que escreve drivers de verdade aprende a construir um. O Capítulo 17 ensina a técnica em pequena escala, onde o leitor consegue ver cada peça em movimento.

Terceiro, a simulação do Capítulo 17 é um dispositivo de ensino por si só. Ao escrever um dispositivo falso, o leitor é forçado a pensar no que um dispositivo real faz: quando ativa um bit, quando o apaga, quando falha, quando trava um valor, quando esquece. Um leitor que escreveu a simulação entende o protocolo de uma forma que um leitor que apenas usou a simulação não entende. O capítulo trata tanto da disciplina de pensar como hardware quanto do código que o implementa.

### Onde o Capítulo 16 Deixou o Driver

Um breve resumo de onde você deve estar. O Capítulo 17 estende o driver produzido ao final da Etapa 4 do Capítulo 16, marcado como versão `0.9-mmio`. Se algum dos itens abaixo parecer incerto, volte ao Capítulo 16 antes de começar este capítulo.

- Seu driver compila sem erros e se identifica como `0.9-mmio` no `kldstat -v`.
- O softc contém um ponteiro `sc->hw` para uma `struct myfirst_hw` que guarda `regs_buf`, `regs_size`, `regs_tag`, `regs_handle` e o log de acessos do Capítulo 16.
- Todo acesso a registradores no driver passa por `CSR_READ_4(sc, off)`, `CSR_WRITE_4(sc, off, val)` ou `CSR_UPDATE_4(sc, off, clear, set)`.
- Os acessores verificam que `sc->mtx` está sendo mantido, via `MYFIRST_ASSERT`, em kernels de depuração.
- `sysctl dev.myfirst.0` lista os sysctls `reg_*` para cada registrador, o sysctl gravável `reg_ctrl_set`, o alternador `access_log_enabled` e o dumper `access_log`.
- `HARDWARE.md` documenta o mapa de registradores e a API `CSR_*`.
- `LOCKING.md` documenta a ordem de detach, incluindo o novo passo `myfirst_hw_detach`.

Esse driver é o que o Capítulo 17 estende. As adições são novamente moderadas em número de linhas: um novo arquivo `myfirst_sim.c`, quatro novos callouts, dois novos grupos de sysctl, uma pequena estrutura de estado para injeção de falhas e um conjunto de helpers de protocolo. A mudança no modelo mental é maior do que o número de linhas sugere.

### O Que Você Vai Aprender

Ao terminar este capítulo, você será capaz de:

- Explicar o que "simular hardware" significa no espaço do kernel, por que a simulação é uma parte permanente do desenvolvimento de drivers e não uma muleta temporária, e o que diferencia uma boa simulação de uma enganosa.
- Projetar um mapa de registradores para um dispositivo simulado, com seções separadas para controle, status, dados, interrupções e metadados, e justificar cada escolha em relação ao protocolo que o dispositivo deve implementar.
- Escolher larguras de registradores, offsets, layouts de campos de bits e semânticas de acesso (somente leitura, somente escrita, leitura-para-limpar, escrita-de-um-para-limpar, leitura/escrita) que espelham padrões usados em drivers reais do FreeBSD.
- Implementar um backend de hardware simulado na memória do kernel que se comporta de forma autônoma, com um callout que atualiza registradores em um ritmo regular e uma tarefa que reage às escritas emitidas pelo driver.
- Expor o dispositivo falso ao driver usando a mesma abstração `bus_space(9)` que drivers reais utilizam, de forma que o código do driver não tenha conhecimento de se está falando com silício ou com uma struct na memória do kernel.
- Testar o comportamento do dispositivo pelo lado do driver escrevendo comandos, fazendo polling do status, lendo dados e verificando invariantes sob diversas cargas.
- Adicionar temporização e atraso à simulação usando `DELAY(9)`, `pause_sbt(9)` e `callout_reset_sbt(9)`, e explicar por que cada ferramenta é adequada para a escala de tempo em que é utilizada.
- Simular erros e condições de falha com segurança, com um caminho de injeção de falhas controlado por sysctl capaz de reproduzir timeouts, erros de dados, estados de ocupado permanente e falhas aleatórias, e usar essas falhas injetadas para exercitar os caminhos de tratamento de erros do driver.
- Refatorar a simulação para que ela viva em seu próprio arquivo (`myfirst_sim.c`), documentar sua interface em um `SIMULATION.md`, versionar o driver como `1.0-simulated` e executar uma rodada completa de testes de regressão.
- Reconhecer os limites da simulação e os cenários em que apenas hardware real (ou hardware virtual apoiado por hypervisor) produzirá resultados confiáveis.

A lista é longa; cada item é específico. O objetivo do capítulo é a composição.

### O Que Este Capítulo Não Aborda

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 17 permaneça focado.

- **Dispositivos PCI reais.** O subsistema `pci(4)`, a correspondência de IDs de fornecedor e dispositivo, a alocação de BAR por meio de `bus_alloc_resource_any`, `pci_enable_busmaster` e o glue que conecta um driver a um barramento real pertencem ao Capítulo 18. O Capítulo 17 permanece em simulação e usa o atalho do Capítulo 16 para a tag e o handle.
- **Interrupções.** A simulação produz mudanças de estado que imitam o que um handler de interrupção veria. Ela ainda não registra um handler de interrupção real via `bus_setup_intr(9)`, e não divide o trabalho entre um handler de filtro e uma thread de interrupção. O Capítulo 19 cobre isso. O Capítulo 17 faz polling por meio de callouts e syscalls do espaço do usuário; os callouts substituem uma fonte de interrupção.
- **DMA.** Anéis de descritores, listas scatter-gather, tags `bus_dma(9)`, bounce buffers e flush de cache em torno de DMA são tópicos dos Capítulos 20 e 21. O registrador de dados do Capítulo 17 permanece como um único slot de 32 bits.
- **Simuladores de sistema completo.** QEMU, bhyve, dispositivos virtio e os tipos de simulação que os hypervisors oferecem são mencionados de passagem como ponte para o Capítulo 18. O Capítulo 17 permanece no kernel; a simulação roda dentro do mesmo kernel que o driver.
- **Verificação de protocolo e métodos formais.** A verificação de hardware real usa ferramentas formais para provar que o tratamento de protocolo de um driver está correto. Essas ferramentas estão fora do escopo deste livro. O Capítulo 17 usa `INVARIANTS`, `WITNESS`, testes de estresse e injeção deliberada de falhas para construir confiança.
- **Emulação de dispositivo apoiada por hypervisor.** A simulação do Capítulo 17 roda no próprio espaço de endereços do kernel. Dispositivos Virtio em uma VM, NICs emuladas no QEMU e a emulação de dispositivos do bhyve rodam todas em um hypervisor; o driver as vê por meio de PCI real. Esse caminho é competência do Capítulo 18.

Manter-se dentro dessas fronteiras faz do Capítulo 17 um capítulo sobre como fazer um dispositivo falso se comportar como um real. O vocabulário é o que se transfere; os subsistemas específicos são o que os Capítulos 18 a 22 aplicam esse vocabulário.

### Tempo Estimado de Investimento

- **Leitura apenas**: três a quatro horas. Os conceitos de simulação são pequenos, mas se acumulam; cada seção introduz um novo padrão de comportamento, e é a composição que torna o capítulo rico.
- **Leitura mais digitação dos exemplos trabalhados**: oito a dez horas em duas sessões. O driver evolui em cinco etapas; cada etapa é uma pequena refatoração que se integra ao `myfirst_hw.c` existente ou ao novo `myfirst_sim.c`.
- **Leitura mais todos os laboratórios e desafios**: treze a dezesseis horas em três ou quatro sessões, incluindo testes de estresse com o kernel de depuração, percorrendo os cenários de injeção de falhas e lendo alguns drivers reais baseados em callout.

As seções 3, 6 e 7 são as mais densas. Se a interação entre um callout, um mutex e uma mudança de estado parecer opaca na primeira leitura, isso é esperado: três primitivos da Parte 3 estão se compondo de uma forma nova. Pare, releia a tabela de temporização da Seção 6 e continue quando a composição tiver se assentado.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Stage 4 do Capítulo 16 (`0.9-mmio`). O ponto de partida pressupõe todos os acessores de registrador do Capítulo 16, a divisão entre `myfirst.c` e `myfirst_hw.c`, as macros `CSR_*` e o log de acesso.
- Sua máquina de laboratório roda FreeBSD 14.3 com `/usr/src` em disco e compatível com o kernel em execução.
- Um kernel de debug com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está compilado, instalado e inicializando sem erros.
- Você tem familiaridade com `callout_init_mtx`, `callout_reset_sbt` e `callout_drain` do Capítulo 13, e com `taskqueue` do Capítulo 14.
- Você compreende `arc4random(9)` ao nível de leitura. Se for novidade, dê uma passada rápida na página de manual antes da Seção 7.

Se algum item acima estiver incerto, resolva-o agora em vez de avançar pelo Capítulo 17 tentando raciocinar com uma base instável. Código de simulação produz bugs sensíveis a timing, e um kernel de debug somado a bases sólidas da Parte 3 captura a maioria deles no primeiro contato.

### Como Aproveitar ao Máximo Este Capítulo

Três hábitos vão trazer retorno rápido.

Primeiro, mantenha um segundo terminal aberto na máquina onde você está testando. A simulação faz os registradores mudarem entre duas leituras de sysctl, e a única forma de perceber isso é ler o mesmo sysctl duas vezes seguidas. Um shell que executa `sysctl dev.myfirst.0.reg_status` em loop a cada 200 milissegundos é como os exemplos do capítulo ganham vida. Se você não consegue manter dois terminais lado a lado, um loop `watch -n 0.2 sysctl ...` em um painel do tmux funciona igualmente bem.

Segundo, leia o log de acesso após cada experimento. O log registra cada acesso a registrador com um timestamp, um offset, um valor e uma tag de contexto. Para uma simulação estática, o log é entediante: leituras do espaço do usuário, escritas do espaço do usuário, nada mais. Para a simulação do Capítulo 17, o log é denso: callouts disparando, eventos atrasados se ativando, falhas sendo injetadas, timeouts expirando. Todo comportamento interessante deixa um rastro, e é nesse rastro que reside a intuição que o capítulo transmite.

Terceiro, quebre a simulação de propósito. Defina a taxa de injeção de falhas como 100%. Defina o intervalo do callout como zero. Faça o dispositivo simulado ficar sempre ocupado. Veja como o driver reage. As lições de tratamento de erros do capítulo ficam muito mais claras quando você já viu os modos de falha se manifestarem. A simulação é segura: você pode injetar quantas falhas quiser sem arriscar hardware real, e o pior caso é um kernel panic que um kernel de debug captura de forma limpa.

### Roteiro pelo Capítulo

As seções, em ordem, são:

1. **Por Que Simular Hardware?** O argumento em favor da simulação como parte permanente do desenvolvimento de drivers, a diferença entre uma boa simulação e uma enganosa, e os objetivos que a simulação do Capítulo 17 busca atingir.
2. **Projetando o Mapa de Registradores.** Como escolher registradores, larguras, layouts e semânticas de acesso para um dispositivo simulado, com um exemplo trabalhado das adições que o Capítulo 17 faz sobre o mapa do Capítulo 16.
3. **Implementando o Backend de Hardware Simulado.** O primeiro callout que atualiza registradores de forma autônoma, um helper `myfirst_sim_tick` que conduz a máquina de estados da simulação, e o primeiro estágio do driver do Capítulo 17 (`1.0-sim-stage1`).
4. **Expondo o Dispositivo Falso ao Driver.** A tag, o handle, o caminho de acesso e como a abstração do Capítulo 16 se propaga sem modificação. Uma seção curta; o ponto é que o trabalho do Capítulo 16 já fez a maior parte.
5. **Testando o Comportamento do Dispositivo a partir do Driver.** Escrever um comando, fazer polling de status, ler dados e tratar a condição de corrida entre a observação do driver e as próprias atualizações de estado da simulação. Stage 2 do driver do Capítulo 17.
6. **Adicionando Timing e Atrasos.** `DELAY(9)` na escala de microssegundos, `pause_sbt(9)` na escala de milissegundos e `callout_reset_sbt(9)` na escala de segundos; quando cada um é seguro; quando não é; qual latência o driver pode esperar de cada um. Stage 3.
7. **Simulando Erros e Condições de Falha.** Um framework de injeção de falhas construído sobre a simulação, com modos que cobrem timeouts, erros de dados, stuck-busy e falhas aleatórias. Ensinando o driver a detectar, se recuperar ou reportar. Stage 4.
8. **Refatorando e Versionando Seu Driver de Hardware Simulado.** A divisão final em `myfirst_sim.c`, um novo `SIMULATION.md`, o incremento de versão para `1.0-simulated` e a passagem de regressão. Stage 5.

Após as oito seções vêm os laboratórios práticos, os exercícios desafio, uma referência de resolução de problemas, um Encerrando que fecha a história do Capítulo 17 e abre a do Capítulo 18, e uma ponte para o Capítulo 18. O material de referência ao final do capítulo foi feito para ser mantido à mão enquanto você trabalha nos laboratórios.

Se esta é a sua primeira leitura, avance linearmente e faça os laboratórios em ordem. Se está revisitando, as Seções 3, 6 e 7 são razoavelmente independentes e formam boas leituras de uma sessão só.



## Seção 1: Por Que Simular Hardware?

O Capítulo 16 já apresentou o argumento em favor da simulação como rampa de entrada. Um leitor que não possui exatamente o dispositivo referenciado pelo livro ainda pode praticar acesso a registradores contra um bloco simulado. Esse argumento é válido e continua se aplicando no Capítulo 17, mas não é o único motivo pelo qual a simulação merece um capítulo próprio. A simulação faz parte de como o desenvolvimento sério de drivers é feito, e entender por quê é o objetivo desta seção.

### O Kit de Ferramentas do Desenvolvedor de Drivers em Atividade

Escolha qualquer driver real do FreeBSD e percorra o histórico de commits de trás para frente. A maioria dos drivers com desenvolvimento ativo compartilha um padrão comum: os primeiros commits constroem o driver contra hardware real. Commits posteriores, muitas vezes bem posteriores, adicionam testes, harnesses, scaffolding e instrumentação que permitem exercitar o driver sem que o hardware original esteja na mesa do desenvolvedor. Parte dessa instrumentação roda no espaço do usuário. Parte é um dispositivo falso apoiado por um hypervisor que fala o mesmo protocolo que o real. Parte é um conjunto de stubs no lado do kernel que fingem ser o hardware com fidelidade suficiente para rodar as máquinas de estados do driver.

Por que os desenvolvedores de drivers investem nisso? Porque hardware real não é reproduzível. O comportamento de um dispositivo real depende da sua revisão de firmware, da sua temperatura, do seu parceiro de link, da integridade de sinal da sua conexão física e de fatores imprevisíveis. Um driver que passa nos testes contra uma instância de um dispositivo pode falhar contra uma instância ligeiramente diferente. Um driver que passa nos testes hoje pode falhar amanhã porque a atualização automática de firmware do dispositivo foi executada durante a noite. Um driver cuja falha depende de timing pode passar em cem execuções e falhar na centésima primeira. Hardware real é um alvo móvel, e um alvo móvel é uma base ruim para testes de regressão.

A simulação fixa o alvo. Um dispositivo simulado se comporta da mesma forma toda vez que os mesmos inputs chegam. As condições de falha de um dispositivo simulado estão sob o controle do autor. Um dispositivo simulado pode ser rodado em loop apertado sem se desgastar. Um dispositivo simulado não requer hardware de laboratório, cabeamento, fonte de alimentação nem a ajuda de um colega. Cada propriedade que torna a simulação conveniente para o aprendizado também a torna valiosa para testes em produção.

A conclusão é que a simulação do Capítulo 17 não é scaffolding descartável. Os padrões que você aprende aqui são os mesmos que os committers do FreeBSD usam para testar drivers aos quais ninguém mais tem acesso porque o silicon tem vinte anos. As mesmas técnicas capturam regressões antes que elas cheguem à máquina de um cliente. A mesma disciplina, aplicada ao dispositivo PCI real no Capítulo 18, permite exercitar o driver em uma estação de trabalho sem rede alguma, dizendo a um backend virtio-net para descartar pacotes, corromper frames ou duplicar descritores a uma taxa controlada.

### Objetivos da Simulação: Comportamento, Timing e Efeitos Colaterais

Quando dizemos "simular um dispositivo", normalmente queremos dizer uma de três coisas, e vale a pena ser explícito sobre qual delas pretendemos.

O primeiro tipo de simulação é **comportamental**. A simulação expõe uma interface de registradores, e escritas e leituras produzem os mesmos efeitos que um dispositivo real produziria. A máquina de estados está correta: escrever em `CTRL.GO` move o dispositivo de idle para busy e depois para done, ativando `STATUS.DATA_AV` no momento certo. O caminho de dados está correto: dados escritos em `DATA_IN` aparecem em `DATA_OUT` após algum processamento. A simulação não precisa corresponder ao timing do dispositivo real; ela apenas precisa seguir as mesmas regras.

O segundo tipo de simulação é **consciente de timing**. A simulação impõe os atrasos que o dispositivo real imporia. Uma escrita que o dispositivo real leva 500 microssegundos para processar leva 500 microssegundos na simulação, mais ou menos, sujeita à resolução de timing do próprio CPU. Um bit de status que o dispositivo real ativa 2 milissegundos após um comando ativa 2 milissegundos após o comando na simulação. Um driver que passa pela simulação consciente de timing provavelmente funcionará no hardware real; um driver que passa apenas pela simulação comportamental pode falhar quando a latência do dispositivo real produzir uma condição de corrida que o driver não considerou.

O terceiro tipo de simulação é **consciente de efeitos colaterais**. A simulação espelha as partes sutis do protocolo: leituras que limpam estado, escritas que não têm efeito, registradores que retornam todos-uns quando o dispositivo está em determinado modo, bits que são write-one-to-clear versus bits que são read-to-clear. Um driver que passa por esse tipo de simulação tem confiança nos detalhes do protocolo de baixo nível, não apenas no fluxo de estado de alto nível.

Os três tipos se acumulam. Uma simulação realista é os três ao mesmo tempo: comportamento, timing e efeitos colaterais. O Capítulo 17 constrói uma simulação dos três tipos, embora a precisão de timing seja limitada pelo que a maquinaria de escalonamento do kernel consegue entregar. O objetivo não é corresponder ao silicon microssegundo a microssegundo; o objetivo é tornar a simulação realista o suficiente para que os caminhos de protocolo importantes do driver sejam exercitados e as condições de corrida que esses caminhos contêm tenham a chance de se manifestar.

### O Que uma Boa Simulação Ensina e o Que Não Ensina

Uma boa simulação ensina protocolo. Ela exercita cada ramo da máquina de estados do driver, cada caminho de erro, cada verificação de status, cada tolerância a atraso. Um driver escrito contra uma boa simulação é robusto nos cenários que a simulação cobre.

Uma boa simulação não ensina silicon. A hierarquia de memória do CPU, os pipelines internos do dispositivo, a integridade de sinal das trilhas da PCB, a interação com o restante do tráfego do sistema, o comportamento térmico do chip sob carga: nada disso está na simulação. Um driver que passa pela simulação ainda pode falhar no hardware real se a falha tiver raiz na física.

Essa distinção importa. A simulação do Capítulo 17 é uma ferramenta de ensino para protocolo, e o driver que ela produz é um veículo de aprendizado, não um driver de rede em produção. Os Capítulos 20 a 22 introduzem DMA, interrupções e preocupações com desempenho de dispositivos reais, todos os quais estendem a simulação em direções que precisam de hardware real para validar completamente. O Capítulo 17 é o primeiro passo de uma progressão mais longa: acerte o protocolo primeiro, depois acerte os detalhes do hardware real.

Há uma segunda sutileza que vale nomear. Uma simulação pode ensinar o protocolo *errado* se for inconsistente com o dispositivo real. Um leitor que aprende a esperar o comportamento da simulação e depois encontra hardware real cujo comportamento diverge ficará confuso. A simulação do Capítulo 17 foi deliberadamente projetada para corresponder aos padrões comuns em drivers reais do FreeBSD: `STATUS.READY` limpa enquanto o dispositivo está ocupado e ativa quando está pronto, `STATUS.DATA_AV` trava até que o driver leia os dados, `INTR_STATUS` é read-to-clear, `CTRL.RESET` é write-one-to-self-clear. Esses padrões não são universais; diferentes dispositivos escolhem diferentes convenções. Mas são comuns, e um driver que lida bem com esses padrões aprendeu uma habilidade transferível.

### Sem Dependência de Hardware: O Benefício Prático

Para este livro, o benefício mais imediato da simulação é que o leitor não precisa de hardware específico para acompanhar. Um leitor estudando o Capítulo 16 em um laptop sem dispositivos externos, em uma máquina virtual sem PCI passthrough, em um computador de placa única ARM com apenas os periféricos integrados, ainda consegue compilar o driver e observar seu comportamento. O Capítulo 17 preserva essa propriedade. A simulação vive inteiramente na memória do kernel; o driver não precisa de nada além de um sistema FreeBSD 14.3 em funcionamento.

Isso é importante para um livro didático. Um leitor que precisa parar, encomendar hardware, instalá-lo e esperar a entrega antes de continuar é um leitor que talvez não continue. Um leitor que consegue compilar e executar o driver na mesma sessão em que lê o capítulo é um leitor que está aprendendo. Cada laboratório e exercício desafio do Capítulo 17 foi projetado para rodar em qualquer sistema FreeBSD x86 sem hardware externo.

Um benefício secundário é que a simulação permite que o livro avance além do que o hardware do leitor é capaz de fazer. O driver do Capítulo 17 exercita caminhos de erro que um dispositivo real quase nunca acionaria em operação normal. Um leitor que possui uma placa de rede de nível de produção teria dificuldade em forçar a placa nos tipos de modos de falha que a injeção de falhas do Capítulo 7 ensina. Um dispositivo simulado pode ser instruído a falhar sob demanda, quantas vezes forem necessárias, em qualquer condição que o leitor queira exercitar. A superfície de aprendizado é mais ampla.

### Controle Total: O Benefício Pedagógico

A simulação dá ao leitor mais controle do que o hardware real jamais oferece. O leitor pode congelar o tempo na simulação, avançar por um número conhecido de ticks e observar cada estado intermediário. O leitor pode desabilitar o comportamento autônomo da simulação, fazer uma única manipulação e reativá-la. O leitor pode injetar uma falha específica, observar como o driver reage e então corrigir a falha alternando um sysctl. Nada disso é possível com hardware de silício real.

Esse nível de controle é pedagogicamente valioso. Um capítulo que explica "quando o dispositivo está ocupado, o driver deve aguardar `STATUS.READY`" consegue demonstrar o cenário de forma concreta. O Capítulo 17 faz exatamente isso: um sysctl instrui a simulação a entrar em um estado de fake-busy, e o leitor pode digitar comandos enquanto o driver aguarda pacientemente que o bit de busy seja limpo. Em hardware real, produzir esse cenário de forma confiável exigiria uma carga de trabalho específica que o dispositivo rejeitaria, ou um fixture de teste artificial, ou um depurador. Na simulação, trata-se de uma mudança de uma linha no sysctl.

Controle também significa que o leitor pode experimentar sem consequências. Definir um registrador com um valor inválido, emitir um comando que o dispositivo não espera, pedir ao driver que acesse um registrador fora do intervalo mapeado: cada uma dessas ações é segura na simulação. O pior caso é um KASSERT disparando e um panic que um kernel de depuração captura de forma limpa. O leitor pode tentar dez ideias ruins em uma hora, aprender com cada uma delas e sair melhor por isso. A experimentação com hardware real carrega riscos reais, e os leitores (com razão) exercem mais cautela nesse caso.

### Experimentos Seguros: O Benefício de Redução de Riscos

A palavra "seguro" merece uma explicação mais detalhada. Um kernel panic causado por um dispositivo simulado é embaraçoso; um kernel panic causado por um dispositivo real pode ser pior. Um driver que escreve um bit errado no registrador `CTRL` de um dispositivo real pode inutilizar o dispositivo. Um driver que corrompe a configuração de DMA de um dispositivo real pode escrever lixo em regiões arbitrárias da RAM. Um driver que trata mal uma interrupção real pode deixar o sistema em um estado em que interrupções legítimas sejam perdidas indefinidamente. Cada um desses modos de falha já apareceu em ambientes FreeBSD em produção ao longo dos anos, e cada um deles exigiu uma atualização do kernel para ser corrigido.

A simulação elimina a maior parte desse risco. O dispositivo simulado é uma struct na memória do kernel; um bit errado vai para uma memória que não está conectada a nada mais. O DMA simulado ainda não existe (ele chega no Capítulo 20). As interrupções simuladas são callouts que o driver pode interromper a qualquer momento. Um iniciante escrevendo seu primeiro driver pode cometer todos os erros que o ciclo de desenvolvimento de drivers contém sem causar nenhum problema físico.

Vale a pena internalizar isso. A simulação do Capítulo 17 dá ao leitor permissão para experimentar com agressividade, que é exatamente o que um iniciante precisa fazer para construir confiança. Um leitor que nunca quebrou um driver é um leitor que ainda não aprendeu como os drivers quebram. O Capítulo 17 é o capítulo em que o leitor aprende tanto a quebrar um driver de propósito quanto a reconhecê-lo quando ele quebra por conta própria.

### O Que Vamos Simular no Capítulo 17

Uma lista resumida dos comportamentos específicos que o capítulo apresentará. Cada um é adicionado na seção que o discute; a lista aqui é um roteiro.

- **Atualizações autônomas de `STATUS`.** Um callout dispara a cada 100 milissegundos e atualiza campos em `STATUS` como se um monitor de hardware em segundo plano estivesse amostrando o estado interno do dispositivo. O driver pode observar as atualizações; o conjunto de testes pode verificá-las.
- **Eventos atrasados disparados por comandos.** Escrever em `CTRL.GO` agenda um callout que, após um atraso configurável (padrão de 500 milissegundos), define `STATUS.DATA_AV` e copia `DATA_IN` para `DATA_OUT`. Esse é o padrão clássico de "dispositivo assíncrono".
- **`INTR_STATUS` com leitura que limpa.** Ler `INTR_STATUS` retorna os bits de interrupção pendentes atuais e os limpa atomicamente. Escrever em `INTR_STATUS` não tem efeito (dispositivos reais variam; alguns permitem write-one-to-clear, que é uma variante comum que discutiremos).
- **Dados de sensor simulados.** Um registrador `SENSOR` armazena um valor que a simulação atualiza periodicamente, imitando uma leitura de temperatura ou pressão. O driver pode fazer polling nele; uma ferramenta no espaço do usuário pode plotar os valores.
- **Tolerância a timeout.** O caminho de comando do driver aguarda até um número configurável de milissegundos para que `STATUS.DATA_AV` seja afirmado. Se a simulação for configurada com um atraso maior do que o timeout, o driver reporta um timeout e se recupera.
- **Injeção aleatória de falhas.** Um sysctl controla a probabilidade de que qualquer operação injete uma falha. Os tipos de falha incluem: `STATUS.ERROR` definido após um comando, o próximo comando expirando completamente, uma leitura retornando `0xFFFFFFFF`, o dispositivo reportando fake-busy indefinidamente. Cada falha exercita um caminho diferente do driver.

Todos esses comportamentos são implementados pela Seção 8. O capítulo os constrói um de cada vez para que o leitor possa internalizar cada um antes de avançar.

### O Lugar da Simulação no Livro

O Capítulo 17 situa-se entre o Capítulo 16 (acesso estático a registradores) e o Capítulo 18 (hardware PCI real). É a ponte entre o vocabulário abstrato e o dispositivo concreto. Um leitor que conclui o Capítulo 17 tem um driver que exercitou quase todos os padrões de protocolo que o livro discutirá, contra um dispositivo simulado que se comporta como um real. Esse leitor chega ao Capítulo 18 com um driver que já está próximo de um nível de produção em sua estrutura, precisando apenas do código de integração PCI real para rodar em silício físico.

A simulação também é uma referência. Quando o Capítulo 18 substitui o bloco de registradores simulado por um PCI BAR real, o leitor pode comparar o comportamento da simulação com o comportamento do dispositivo real e perceber as diferenças. A simulação é o padrão de referência. Quando o Capítulo 19 adiciona interrupções, as mudanças de estado guiadas por callout da simulação tornam-se o modelo para o que um handler de interrupção reage. Quando o Capítulo 20 adiciona DMA, os registradores de dados da simulação tornam-se o modelo para o que o motor de DMA lê e escreve. A simulação é o vocabulário de ensino que todos os capítulos posteriores estendem.

### Encerrando a Seção 1

A simulação é uma parte permanente do desenvolvimento de drivers, não um andaime temporário. Ela oferece aos autores de drivers testes reproduzíveis, injeção controlada de falhas e a capacidade de exercitar caminhos de erro que o hardware real raramente produz. O Capítulo 17 constrói uma simulação de três tipos (comportamental, consciente do tempo, consciente de efeitos colaterais) sobre o driver do Capítulo 16. A simulação é deliberadamente projetada para corresponder a padrões comuns em drivers FreeBSD reais, para que a intuição do leitor se transfira para hardware real mais tarde.

Os objetivos específicos do capítulo incluem atualizações autônomas de `STATUS`, eventos atrasados disparados por comandos, semântica de leitura que limpa, dados de sensor simulados, tolerância a timeout e injeção aleatória de falhas. Cada um é introduzido na seção que o requer; juntos, produzem um driver rico o suficiente para exercitar quase todas as lições da Parte 3.

A Seção 2 dá o próximo passo. Antes de simularmos o comportamento do dispositivo, precisamos de um mapa de registradores no qual o comportamento opera. O Capítulo 16 nos deu um mapa estático; o Capítulo 17 o estende para comportamento dinâmico.



## Seção 2: Projetando o Mapa de Registradores

O Capítulo 16 deu ao dispositivo simulado um mapa de registradores. O trabalho do Capítulo 17 estende esse mapa, mas as extensões não são automáticas. Adicionar um registrador a um dispositivo real requer uma equipe de engenheiros de hardware, uma nova revisão do chip e um novo datasheet. Adicionar um registrador a um dispositivo simulado é fácil em termos mecânicos, mas o trabalho de design (o que o registrador deve fazer? quem escreve nele? quem o lê? quais são seus efeitos colaterais?) é o mesmo, e a disciplina de fazê-lo bem também é a mesma.

Esta seção percorre o design, não apenas o resultado. Ao final da Seção 2, você terá um mapa de registradores que suporta todos os comportamentos listados na Seção 1, documentado de forma suficientemente clara para que a implementação nas Seções 3 a 7 seja quase mecânica. O exercício é transferível: toda vez que você ler um datasheet real, reconhecerá as decisões que os designers de hardware tomaram, e terá um vocabulário para avaliar se essas decisões foram sábias.

### O Que É um Mapa de Registradores?

Um mapa de registradores é o catálogo completo da interface de registradores de um dispositivo. Ele lista cada registrador, cada offset, cada largura, cada campo, cada bit, cada tipo de acesso, cada valor de reset e cada efeito colateral. O mapa de registradores de um dispositivo real pode chegar a centenas de páginas; o de um dispositivo simples pode caber em uma única página. A forma é semelhante em ambos os casos: uma tabela, ou um conjunto de tabelas, com cabeçalhos que respondem às perguntas que um autor de driver precisa fazer.

Um mapa de registradores não é texto narrativo. É um documento de referência. O autor do driver o consulta ao escrever código, e o autor dos testes o consulta ao escrever testes. Um mapa de registradores vago, incompleto ou ambíguo produz drivers incorretos de formas difíceis de diagnosticar. Um mapa de registradores preciso produz drivers que correspondem ao que o dispositivo espera.

Para um dispositivo simulado, o mapa de registradores desempenha um papel extra. Ele também é a especificação contra a qual o comportamento da simulação é testado. Se a simulação discordar do mapa de registradores, a simulação está errada e deve ser corrigida. Se o driver discordar do mapa de registradores, o driver está errado e deve ser corrigido. O mapa é o contrato. Alterações no mapa exigem alterações tanto na simulação quanto no driver. É exatamente assim que o desenvolvimento de dispositivos reais funciona: o datasheet é o contrato, e a equipe de hardware, a equipe de firmware e a equipe de drivers trabalham todos com base nele.

### O Mapa do Capítulo 16 como Ponto de Partida

Relembre o mapa de registradores do Capítulo 16:

| Offset | Largura | Nome            | Acesso         | Comportamento no Capítulo 16                   |
|--------|---------|-----------------|----------------|------------------------------------------------|
| 0x00   | 32 bits | `CTRL`          | R/W            | Memória simples de leitura/escrita.            |
| 0x04   | 32 bits | `STATUS`        | R/W            | Memória simples de leitura/escrita.            |
| 0x08   | 32 bits | `DATA_IN`       | R/W            | Memória simples de leitura/escrita.            |
| 0x0c   | 32 bits | `DATA_OUT`      | R/W            | Memória simples de leitura/escrita.            |
| 0x10   | 32 bits | `INTR_MASK`     | R/W            | Memória simples de leitura/escrita.            |
| 0x14   | 32 bits | `INTR_STATUS`   | R/W            | Memória simples de leitura/escrita.            |
| 0x18   | 32 bits | `DEVICE_ID`     | Somente leitura | Fixo em `0x4D594649`.                         |
| 0x1c   | 32 bits | `FIRMWARE_REV`  | Somente leitura | Fixo em `0x00010000`.                         |
| 0x20   | 32 bits | `SCRATCH_A`     | R/W            | Memória simples de leitura/escrita.            |
| 0x24   | 32 bits | `SCRATCH_B`     | R/W            | Memória simples de leitura/escrita.            |

Dez registradores, 40 bytes de espaço definido, 64 bytes alocados para deixar espaço para crescimento. Todo acesso é direto: leituras retornam o que o último armazenamento gravou, escritas vão diretamente para a memória. Sem efeitos colaterais nas leituras. Sem efeitos colaterais nas escritas. Nenhuma máquina de estados de hardware por trás dos registradores.

O Capítulo 17 muda duas coisas nesse mapa. Primeiro, os registradores existentes ganham semântica comportamental: `STATUS.DATA_AV` será definido pela simulação, não apenas pelas escritas do driver; `INTR_STATUS` torna-se read-to-clear. Segundo, o mapa adiciona um pequeno número de novos registradores que a simulação do Capítulo 17 requer: um registrador `SENSOR`, um registrador `DELAY_MS`, um registrador `FAULT_MASK` e alguns outros. A alocação de 64 bytes do Capítulo 16 já tem espaço; nenhuma alteração no alocador é necessária.

### Semântica de Acesso a Registradores

Antes de escrevermos qualquer registrador, devemos nomear a semântica de acesso que nos interessa. A semântica de acesso de um registrador são as regras sobre o que acontece quando o driver o lê ou escreve. As principais categorias de uso comum são:

**Read-only (RO).** O registrador retorna um valor produzido pelo dispositivo ou pela simulação. Escritas são ignoradas ou produzem um erro. `DEVICE_ID` e `FIRMWARE_REV` são exemplos.

**Read/Write (RW).** O registrador armazena o que o driver escreve. Leituras retornam o último valor escrito. `CTRL`, `SCRATCH_A` e `SCRATCH_B` são exemplos.

**Write-only (WO).** O registrador aceita escritas, mas leituras retornam um valor fixo (frequentemente zero, às vezes lixo, ocasionalmente o último valor lido de uma fonte diferente). `DATA_IN` costuma ser write-only em dispositivos reais.

**Read-to-clear (RC).** Ler o registrador retorna seu valor atual e em seguida o limpa. Escritas são tipicamente ignoradas. `INTR_STATUS` é o exemplo clássico.

**Write-one-to-clear (W1C).** Escrever 1 em um bit limpa esse bit; escrever 0 não tem efeito. Leituras retornam o valor atual. `INTR_STATUS` em alguns dispositivos usa W1C em vez de RC.

**Write-one-to-set (W1S).** Escrever 1 define o bit; escrever 0 não tem efeito. Menos comum que W1C, mas utilizado em alguns hardwares para registradores de controle em que o driver não precisaria fazer uma operação leitura-modificação-escrita.

**Read/write with side effect (RWSE).** O registrador pode ser lido e escrito, mas a escrita (ou a leitura) dispara algo além de uma simples atualização de memória. `CTRL` frequentemente se enquadra nessa categoria: escrever `CTRL.RESET` dispara um reset do dispositivo, que é um efeito colateral.

**Sticky (latched).** Um bit, uma vez definido pelo hardware, permanece definido até que o driver o limpe explicitamente. Bits de erro em `STATUS` costumam ser sticky. `STATUS.ERROR` será sticky no mapa do Capítulo 17.

**Reserved.** O bit não tem significado definido. Escritas devem preservar o valor existente ou escrever zero; leituras podem retornar qualquer valor. Um driver que ignora bits reservados é robusto contra futuras revisões de hardware; um driver que depende do comportamento de bits reservados é frágil.

Cada tipo de acesso tem implicações no código do driver. Um driver que lê um registrador RC deve fazê-lo exatamente quando o protocolo exige, pois cada leitura extra consome um evento. Um driver que lê um registrador WO está lendo lixo, o que é um bug à espera de se manifestar caso o valor seja alguma vez utilizado. Um driver que realiza uma operação leitura-modificação-escrita em um registrador com bit sticky precisa ter cuidado para não limpar um bit que não pretendia tocar. Um driver que escreve em um registrador W1C usa `CSR_WRITE_4(sc, REG, mask)` e não `CSR_UPDATE_4(sc, REG, mask, 0)`; as duas chamadas se parecem, mas produzem comportamentos diferentes no hardware.

A simulação do Capítulo 17 usa semânticas RO, RW, RC, sticky e RWSE. Ela não introduz W1C ou W1S (embora os exercícios desafio convidem o leitor a adicioná-los). O objetivo da simulação é cobrir os casos comuns de forma completa; os casos menos comuns se tornam extensões naturais assim que os mais comuns forem entendidos.

### Adições do Capítulo 17

O mapa de registradores do Capítulo 17 acrescenta os seguintes registradores:

| Offset | Largura | Nome            | Acesso    | Comportamento no Capítulo 17                                        |
|--------|---------|-----------------|-----------|---------------------------------------------------------------------|
| 0x28   | 32 bit  | `SENSOR`        | RO        | Valor de sensor simulado. Atualizado por um callout a cada 100 ms.  |
| 0x2c   | 32 bit  | `SENSOR_CONFIG` | RW        | Configurações de intervalo de atualização e amplitude do sensor.    |
| 0x30   | 32 bit  | `DELAY_MS`      | RW        | Número de milissegundos que o dispositivo leva por comando.         |
| 0x34   | 32 bit  | `FAULT_MASK`    | RW        | Quais falhas simuladas estão habilitadas. Bitfield.                 |
| 0x38   | 32 bit  | `FAULT_PROB`    | RW        | Probabilidade de falha aleatória por operação (0..10000).           |
| 0x3c   | 32 bit  | `OP_COUNTER`    | RO        | Contagem de comandos que o dispositivo simulado processou.          |

Seis novos registradores, usando offsets de `0x28` a `0x3c`, cabendo inteiramente dentro da região de 64 bytes alocada pelo Capítulo 16. Nenhuma mudança no softc, nenhuma mudança no alocador, nenhuma mudança no `bus_space_map`.

Cada registrador tem uma função específica:

- **`SENSOR`** é a temperatura, pressão ou tensão simulada. A interpretação exata não importa para a simulação; o que importa é que o valor muda por conta própria, ensinando ao leitor como lidar com registradores cujo valor é produzido de forma autônoma. O valor oscila em torno de um valor base usando uma fórmula simples que o callout implementará.

- **`SENSOR_CONFIG`** permite que o driver (ou o usuário por meio de um sysctl) controle o comportamento do sensor. Os 16 bits inferiores são o intervalo de atualização em milissegundos; os 16 bits superiores são a amplitude da oscilação. Um valor de `0x0064_0040` significa "intervalo de 100 ms, amplitude 64". Alterar o registrador modifica a próxima atualização da simulação.

- **`DELAY_MS`** é o tempo que um comando leva na simulação. Escrever em `CTRL.GO` agenda a assertiva de `STATUS.DATA_AV` `DELAY_MS` milissegundos depois. O valor padrão é 500. Defini-lo como 0 faz os comandos serem concluídos no próximo tick do callout; defini-lo com um valor grande permite ao leitor exercitar o caminho de timeout do driver.

- **`FAULT_MASK`** é um bitfield que seleciona quais modos de falha estão ativos. O bit 0 habilita a falha "operação atinge timeout"; o bit 1 habilita a falha "leitura retorna todos os bits em 1"; o bit 2 habilita a falha "bit de erro definido após cada comando"; o bit 3 habilita a falha "dispositivo sempre ocupado". Vários bits podem ser definidos simultaneamente.

- **`FAULT_PROB`** controla a probabilidade de que uma única operação injete uma falha, em décimos de ponto base. Um valor de 10000 equivale a 100% (falha sempre); 5000 equivale a 50%; 0 desativa a injeção aleatória de falhas completamente. Isso dá ao leitor controle refinado sobre a agressividade com que as falhas são injetadas.

- **`OP_COUNTER`** conta todos os comandos que a simulação processou. É somente leitura; escritas são ignoradas. O driver pode usá-lo para verificar que um comando realmente chegou à simulação, e o usuário pode lê-lo a partir do espaço do usuário para verificar a atividade.

### Adições ao Header para os Novos Registradores

O header das adições está em `myfirst_hw.h`, estendendo as definições do Capítulo 16:

```c
/* Chapter 17 additions to the simulated device's register map. */

#define MYFIRST_REG_SENSOR        0x28
#define MYFIRST_REG_SENSOR_CONFIG 0x2c
#define MYFIRST_REG_DELAY_MS      0x30
#define MYFIRST_REG_FAULT_MASK    0x34
#define MYFIRST_REG_FAULT_PROB    0x38
#define MYFIRST_REG_OP_COUNTER    0x3c

/* SENSOR_CONFIG register fields. */
#define MYFIRST_SCFG_INTERVAL_MASK  0x0000ffffu  /* interval in ms */
#define MYFIRST_SCFG_INTERVAL_SHIFT 0
#define MYFIRST_SCFG_AMPLITUDE_MASK 0xffff0000u  /* oscillation range */
#define MYFIRST_SCFG_AMPLITUDE_SHIFT 16

/* FAULT_MASK register bits. */
#define MYFIRST_FAULT_TIMEOUT    0x00000001u  /* next op times out        */
#define MYFIRST_FAULT_READ_1S    0x00000002u  /* reads return 0xFFFFFFFF  */
#define MYFIRST_FAULT_ERROR      0x00000004u  /* STATUS.ERROR after op    */
#define MYFIRST_FAULT_STUCK_BUSY 0x00000008u  /* STATUS.BUSY latched on   */

/* CTRL register: GO bit (new for Chapter 17). */
#define MYFIRST_CTRL_GO          0x00000200u  /* bit 9: start command     */

/* STATUS register: BUSY and DATA_AV are now dynamic (set by simulation). */
/* (The mask constants were already defined in Chapter 16.)               */
```

O novo bit CTRL `MYFIRST_CTRL_GO` não se sobrepõe a nenhum bit existente. `MYFIRST_CTRL_ENABLE` é o bit 0, `RESET` é o bit 1, `MODE_MASK` cobre os bits 4 a 7, `LOOPBACK` é o bit 8. O novo bit `GO` na posição 9 se encaixa perfeitamente na lacuna.

Os comentários no header são intencionalmente breves. Em um driver real, comentários nesse nível apontariam para a seção do datasheet (por exemplo, `/* See datasheet section 4.2.1 */`). Para a simulação, o datasheet é este capítulo; um leitor que busca o comportamento definitivo de `MYFIRST_FAULT_ERROR` deve consultar a Seção 7 do Capítulo 17.

### Escrevendo uma Tabela de Mapa de Registradores para o Seu Próprio Dispositivo

O exercício de escrever um mapa de registradores é uma das coisas mais valiosas que você pode fazer como autor de drivers iniciante. Mesmo que você nunca escreva a simulação ou o driver, o ato de se sentar e pensar "quais seriam os registradores deste dispositivo?" o força a encarar o dispositivo como uma entidade com um protocolo, e não como uma caixa mágica.

Um modelo útil:

1. Decida o que o dispositivo faz em alto nível. Uma frase.
2. Identifique os comandos que o dispositivo deve aceitar. Cada comando pode se tornar um bit em um registrador `CTRL`, um valor em um registrador `COMMAND`, ou ter seu próprio registrador.
3. Identifique os estados que o dispositivo reporta. Cada estado pode se tornar um bit em um registrador `STATUS`, um valor em um registrador `STATE`, ou ter seu próprio registrador somente leitura.
4. Identifique os dados que o dispositivo produz ou consome. Dados de entrada vão em um registrador `DATA_IN` ou em um FIFO. Dados de saída vêm de um registrador `DATA_OUT` ou de um FIFO.
5. Identifique as interrupções que o dispositivo pode gerar. Cada fonte de interrupção se torna um bit em `INTR_STATUS` (ou equivalente).
6. Identifique quaisquer metadados que o driver precisa na inicialização. ID do dispositivo, revisão de firmware, flags de funcionalidade, bits de capacidade.
7. Reserve espaço para registradores de configuração que o driver pode querer ajustar. No Capítulo 17, `DELAY_MS`, `FAULT_MASK` e `FAULT_PROB` são registradores de configuração; um dispositivo real pode ter limiares de timeout, opções de recuperação de erros, configurações de gerenciamento de energia.
8. Decida a largura de cada registrador. 32 bits é o padrão mais comum em dispositivos modernos. Larguras menores existem em dispositivos legados ou em dispositivos onde a quantidade de registradores é muito grande.
9. Decida o offset de cada registrador. Mantenha registradores relacionados contíguos sempre que possível. Alinhe os offsets à largura do registrador (registradores de 32 bits em offsets divisíveis por 4).
10. Decida a semântica de acesso (RO, RW, RC, W1C, ...). Seja explícito; ambiguidade aqui produz bugs.
11. Decida o valor de reset de cada registrador. O que o registrador deve conter ao ligar? Ao resetar? No attach do módulo?
12. Documente os efeitos colaterais de cada registrador não trivial.

Esta lista é longa, mas cada passo é pequeno. Uma equipe de desenvolvimento de um dispositivo real pode levar semanas para produzir um mapa de registradores tão detalhado. O mapa de um dispositivo simulado pode ser produzido em uma tarde. O mapa do Capítulo 17 é o resultado deste exercício aplicado a um dispositivo didático.

### Exemplo de Layout e Decisões de Design

Para tornar a disciplina concreta, vamos percorrer as decisões de design por trás das adições do Capítulo 17.

**Decisão 1: Agrupar registradores relacionados.** Os novos registradores `SENSOR` (0x28) e `SENSOR_CONFIG` (0x2c) são adjacentes. Um driver que lê o código do sensor pode acessar ambos com uma única leitura de região, se necessário, e um leitor que analisa o dump de registradores os vê juntos. A mesma lógica coloca `FAULT_MASK` (0x34) e `FAULT_PROB` (0x38) adjacentes.

**Decisão 2: Colocar a configuração após o diagnóstico.** `DEVICE_ID` (0x18) e `FIRMWARE_REV` (0x1c) são registradores de diagnóstico somente leitura; eles vêm primeiro na seção de offsets altos. `SENSOR` e sua configuração vêm em seguida. `DELAY_MS`, `FAULT_MASK` e `FAULT_PROB` são configuração; eles vêm depois. `OP_COUNTER` é outro registrador de diagnóstico; fica no final.

**Decisão 3: Usar o registrador CTRL existente para o novo bit GO.** Em vez de adicionar um registrador `COMMAND` dedicado, o bit `GO` em `CTRL` é como o driver inicia um comando. Isso corresponde ao padrão do Capítulo 16 (os bits `ENABLE` e `RESET` estão ambos em `CTRL`) e economiza um registrador.

**Decisão 4: Tornar `OP_COUNTER` somente leitura.** Um contador gravável convidaria bugs em que o driver acidentalmente reseta o contador no meio de uma operação. Um contador somente leitura é sempre monotônico (módulo do overflow de 32 bits) e sempre reflete a visão da simulação.

**Decisão 5: Codificar dois campos em `SENSOR_CONFIG`.** Em vez de dois registradores separados para intervalo e amplitude, um único registrador com dois campos de 16 bits economiza um slot e corresponde a um padrão comum de hardware. Um dispositivo real pode ter um ID de comando de 8 bits no byte mais alto e um argumento de 24 bits nos três bytes inferiores de um registrador; o princípio é o mesmo.

**Decisão 6: Usar `FAULT_PROB` em décimos de ponto base (intervalo de 0 a 10000).** Usar 10000 como escala completa em vez de 100 oferece duas casas decimais extras de precisão. Probabilidades de falha de 0,5%, 0,75%, 1,25% tornam-se expressáveis como inteiros sem aritmética fracionária.

Cada decisão é pequena; a composição é o que torna o mapa utilizável. Um mapa de registradores projetado descuidadamente produz um driver cheio de correções improvisadas; um mapa projetado com cuidado produz um driver que se escreve sozinho.

### Convenções de Nomenclatura para Constantes

Uma nota sobre as convenções de nomenclatura que o Capítulo 17 usa para máscaras de bits, que espelham o que drivers FreeBSD reais fazem.

O prefixo `MYFIRST_` marca tudo como pertencente ao driver. Dentro dele, `REG_` indica um offset de registrador, `CTRL_` indica um bit ou campo em `CTRL`, `STATUS_` indica um bit em `STATUS`, e assim por diante. O padrão é `<driver>_<registrador>_<campo>`. Por exemplo, `MYFIRST_STATUS_READY` é o bit `READY` no registrador `STATUS`. O nome é longo, mas não é ambíguo, e editores modernos o completam rapidamente.

Máscaras que cobrem múltiplos bits terminam com `_MASK`; deslocamentos para alinhar a máscara terminam com `_SHIFT`. `MYFIRST_CTRL_MODE_MASK` é a máscara para o campo `MODE` de 4 bits; `MYFIRST_CTRL_MODE_SHIFT` é o número de bits para deslocar à direita e trazer o campo para o bit zero. Extrair o campo se lê como `(ctrl & MYFIRST_CTRL_MODE_MASK) >> MYFIRST_CTRL_MODE_SHIFT`; defini-lo se lê como `(mode << MYFIRST_CTRL_MODE_SHIFT) & MYFIRST_CTRL_MODE_MASK`.

Valores fixos terminam com `_VALUE`. `MYFIRST_DEVICE_ID_VALUE` é o valor constante do registrador `DEVICE_ID`. Esta convenção mantém os valores distinguíveis das máscaras à primeira vista.

Drivers reais às vezes se desviam dessa convenção, frequentemente por razões históricas ou porque os nomes dos registradores são longos. O driver `if_em` usa prefixos mais curtos (`EM_` em vez de `E1000_` em alguns lugares) para caber dentro de comprimentos de linha razoáveis. O driver `if_ale` usa `CSR_READ_4(sc, ALE_REG_NAME)` com `ALE_` como prefixo. O princípio é o mesmo em todos os drivers: cada constante é visivelmente parte do namespace de um driver, e o padrão dentro do namespace é consistente.

Para o Capítulo 17, consistência importa mais do que brevidade. O leitor ainda está aprendendo os padrões; um nome longo e explícito é mais educativo do que um nome curto e ambíguo.

### Validando o Mapa Antes de Escrever o Código

Antes de implementar a simulação, vale a pena validar o mapa com algumas verificações de sanidade.

**Verificação 1: Os offsets cabem dentro da região alocada.** A região do Capítulo 16 tem 64 bytes. O maior offset definido no mapa do Capítulo 17 é `0x3c`, com largura 4, portanto o último byte usado está no offset `0x3f`. `0x40` é um byte além da região alocada. O mapa cabe exatamente; não há espaço para mais um registrador sem ampliar a alocação. Essa é uma restrição de design que vale conhecer.

**Verificação 2: Todos os offsets têm alinhamento de 4 bytes.** Cada registrador tem 32 bits de largura, e todo offset é múltiplo de 4. `bus_space_read_4` e `bus_space_write_4` exigem alinhamento de 4 bytes na maioria das plataformas; um acesso desalinhado causaria falha em algumas arquiteturas e comportamento incorreto silencioso em outras. O mapa segue a regra.

**Verificação 3: Nenhum registrador se sobrepõe a outro.** Cada registrador de 32 bits ocupa 4 bytes consecutivos. Verifique que offsets adjacentes diferem por pelo menos 4. E diferem: 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c.

**Verificação 4: Nenhum bit reservado é usado para múltiplos propósitos.** Um bit definido para um campo não deve aparecer em outro campo. `MYFIRST_CTRL_ENABLE` é o bit 0, `RESET` é o bit 1, `MODE` cobre os bits 4 a 7, `LOOPBACK` é o bit 8, `GO` é o bit 9. Nenhuma sobreposição.

**Verificação 5: A semântica de acesso é consistente com a intenção da simulação.** `DEVICE_ID` e `FIRMWARE_REV` são RO, o que significa que a simulação se recusará a alterá-los após o attach. `INTR_STATUS` será RC, significando que a simulação o limpa quando o driver o lê. `OP_COUNTER` é RO, o que significa que escritas não fazem nada. Todo o resto é RW (possivelmente com efeitos colaterais). Isso é consistente com o que a Seção 1 descreveu.

**Verificação 6: Os valores de reset fazem sentido.** No attach, `DEVICE_ID` vale `0x4D594649`, `FIRMWARE_REV` vale `0x00010000`, `STATUS` tem o bit `READY` ativo e todo o restante é zero. `DELAY_MS` tem como padrão 500 (500 ms por comando). `SENSOR_CONFIG` tem como padrão 0x0064_0040 (intervalo de 100 ms, amplitude 64). `FAULT_MASK` tem como padrão 0 (nenhuma falha habilitada). `FAULT_PROB` tem como padrão 0 (nenhuma falha aleatória). Os valores padrão descrevem uma simulação que se comporta como um dispositivo confiável até que seja instruída de outra forma.

Essas verificações são o tipo de coisa que um autor de driver disciplinado percorre antes de escrever qualquer código. Elas levam alguns minutos e identificam vários bugs comuns antes que eles apareçam.

### Documentando o Mapa

O Capítulo 16 introduziu `HARDWARE.md`. O Capítulo 17 o expande. Os títulos de seção em `HARDWARE.md` ao final do Capítulo 17 serão:

1. Versão e escopo
2. Tabela resumo de registradores (todos os registradores, todos os offsets, todas as larguras, todos os tipos de acesso, todos os valores padrão)
3. Campos do registrador CTRL (tabela de cada bit do CTRL)
4. Campos do registrador STATUS (tabela de cada bit do STATUS)
5. Campos de INTR_MASK e INTR_STATUS (tabela de cada fonte de interrupção)
6. Campos de SENSOR_CONFIG (intervalo e amplitude)
7. Campos de FAULT_MASK (tabela de cada tipo de falha)
8. Referência registrador por registrador (um parágrafo por registrador, descrevendo comportamento, efeitos colaterais e uso)
9. Sequências comuns (escrita-seguida-de-leitura-para-verificação, comando-e-espera, injeção-de-falha-para-teste)
10. Observabilidade (cada sysctl que lê ou escreve um registrador)
11. Notas de simulação (quais registradores são dinâmicos, quais são estáticos, o que os callouts fazem)

Este documento é longo, talvez 100 linhas. É a fonte única da verdade sobre o que o driver espera da simulação. Um leitor que esquece o comportamento de um registrador abre o `HARDWARE.md` e encontra a resposta. Um contribuidor que adiciona um novo registrador atualiza o `HARDWARE.md` primeiro, depois altera o código.

### Erros Comuns no Design de Register Maps

Iniciantes que projetam seu primeiro register map costumam cometer os mesmos erros. Um breve catálogo para que você possa evitá-los.

**Erro 1: Usar números mágicos no código em vez de constantes nomeadas.** Um driver que escreve `CSR_WRITE_4(sc, 0x00, 0x1)` em vez de `CSR_WRITE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_ENABLE)` é difícil de ler e impossível de refatorar. A regra é: todo offset é uma constante; todo bit é uma constante; todo campo é uma macro.

**Erro 2: Definições de bits sobrepostos.** Dois nomes, um bit. Um leitor que faz um OR bit a bit de `FLAG_A` e `FLAG_B` obtém um valor que corresponde a uma terceira coisa completamente diferente, porque `FLAG_A` e `FLAG_B` acabam sendo o mesmo bit. A defesa é um comentário ao lado de cada nome de bit listando seu número de bit, mais uma verificação de sanidade em tempo de compilação (`_Static_assert((MYFIRST_CTRL_ENABLE & MYFIRST_CTRL_RESET) == 0, "bits overlap")` se você quiser ser defensivo).

**Erro 3: Acessos não alinhados.** Definir um registrador de 32 bits no offset `0x06` parece inofensivo, mas falha em qualquer arquitetura onde acessos não alinhados são proibidos. Sempre alinhe os offsets à largura do registrador.

**Erro 4: Esquecer os bits reservados.** Um registrador de 32 bits onde apenas os bits 0 a 3 são definidos pode ter valores não especificados nos bits 4 a 31. Um driver que escreve `CSR_WRITE_4(sc, REG, 0xF)` está bem por enquanto, mas uma revisão futura que defina o bit 5 pode quebrar porque o driver está efetivamente escrevendo zero nele toda vez. A defesa é leitura-modificação-escrita em registradores com bits reservados: `CSR_UPDATE_4(sc, REG, mask_of_fields_we_touch, new_field_values)`.

**Erro 5: Efeitos colaterais na leitura que o driver não espera.** Um registrador RC que o driver lê para fins de depuração limpa o estado em que o driver estava prestes a agir. A defesa é documentar cada efeito colateral explicitamente no header e no `HARDWARE.md`, para que um leitor possa ver o perigo antes de introduzir o bug.

**Erro 6: Assumir que as escritas sempre têm sucesso.** Uma escrita em um registrador RO pode ser silenciosamente ignorada, pode gerar uma falha, ou pode fazer algo surpreendente em determinado hardware. A defesa é usar a semântica de acesso correta e fazer testes de estresse no uso de cada registrador pelo driver.

**Erro 7: Larguras inconsistentes.** Um registrador de 32 bits ao lado de um de 8 bits onde a largura de acesso não é óbvia pelo nome. A defesa é fazer com que a largura faça parte da chamada do acessor (`CSR_READ_4` vs `CSR_READ_1`) e nomear o tipo do registrador em algum lugar visível.

**Erro 8: Layouts de bits excessivamente complexos.** Um campo de 16 bits que abrange dois byte lanes não adjacentes, exigindo coleta de bits no driver. Dispositivos reais fazem isso às vezes (muitas vezes por causa de restrições históricas), mas um dispositivo simulado deve se manter com campos contíguos. A simulação é um instrumento didático; a clareza vence a esperteza.

**Erro 9: Valores de reset ausentes.** Um registrador cujo valor de reset não é especificado força o driver a adivinhar o que o registrador contém após o attach. A defesa é inicializar cada registrador com um valor documentado no caminho de attach.

**Erro 10: Documentação desatualizada.** O código muda, a documentação não. Dois meses depois o driver funciona, mas a documentação mente. A defesa é a disciplina de atualizar o `HARDWARE.md` no mesmo commit que altera o código.

Cada erro é pequeno isoladamente e custoso em conjunto. Um driver escrito sem esses erros é um driver que se lê bem, testa bem e envelhece bem.

### Encerrando a Seção 2

Um register map é a especificação da interface visível ao programador de um dispositivo. O mapa do Capítulo 17 estende o do Capítulo 16 com seis novos registradores: `SENSOR`, `SENSOR_CONFIG`, `DELAY_MS`, `FAULT_MASK`, `FAULT_PROB` e `OP_COUNTER`. Cada registrador tem uma finalidade, uma largura, um tipo de acesso, um valor de reset e um comportamento documentado. As escolhas de design por trás de cada registrador valem a pena ser compreendidas, pois espelham as decisões que os projetistas de dispositivos reais tomam.

O mapa é documentado no header (`myfirst_hw.h`) e no `HARDWARE.md`. O header é consumido pelo compilador; o markdown é consumido por humanos. Ambos são mantidos sincronizados à medida que o código evolui.

A Seção 3 pega o mapa e implementa a simulação. O primeiro callout dispara; a primeira atualização autônoma de registrador acontece; o bloco estático do Capítulo 16 ganha uma pulsação.



## Seção 3: Implementando o Backend de Hardware Simulado

A Seção 2 produziu um register map. A Seção 3 faz o mapa se comportar. O primeiro trabalho da simulação é atualizar os registradores de forma autônoma, sem que o driver precise solicitar. É assim que o hardware real se comporta: um sensor atualiza seu valor em um clock, um controlador de interrupções trava eventos à medida que chegam, o contador de recepção de uma placa de rede incrementa conforme os frames chegam. O primeiro passo de simulação do Capítulo 17 é produzir uma pulsação semelhante.

A ferramenta é uma que o leitor já conhece: um `callout`. O Capítulo 13 introduziu `callout_init_mtx` e `callout_reset` para timers internos de driver. O Capítulo 17 usa a mesma primitiva para um propósito diferente: conduzir o estado do dispositivo simulado. O callout dispara a cada 100 milissegundos, adquire o mutex do driver, atualiza alguns registradores, libera o mutex e agenda seu próximo disparo. Do ponto de vista do driver, os registradores simplesmente mudam; do ponto de vista da simulação, um timer está em execução.

### O Primeiro Arquivo de Simulação

O layout de arquivos do Capítulo 16 era `myfirst.c` para o ciclo de vida do driver, `myfirst_hw.c` para a camada de acesso ao hardware, e `myfirst_hw.h` para as definições compartilhadas. O Capítulo 17 adiciona um novo arquivo, `myfirst_sim.c`, para o backend de simulação. A separação mantém o código de simulação fora do arquivo de acesso ao hardware, de forma que um capítulo posterior possa substituir `myfirst_sim.c` por código PCI real sem tocar nos acessores.

Crie `myfirst_sim.c` ao lado dos arquivos do Capítulo 16. A primeira versão é pequena:

```c
/*-
 * myfirst_sim.c -- Chapter 17 simulated hardware backend.
 *
 * Adds a callout that drives autonomous register changes, a
 * command-scheduling callout for command-triggered delays, and
 * the fault-injection state that Section 7 will populate.
 *
 * This file assumes Chapter 16's register access layer
 * (myfirst_hw.h, myfirst_hw.c) is present and functional.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/random.h>
#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_sim.h"
```

Os includes seguem as convenções do FreeBSD: `sys/param.h` primeiro, depois `sys/systm.h`, depois os headers de subsistema específicos. `sys/random.h` será necessário para `arc4random` mais adiante; incluí-lo agora evita uma segunda edição. Os headers privados do driver vêm por último.

O arquivo compila em conjunto com o `myfirst.h` e `myfirst_hw.h` existentes, mais um novo `myfirst_sim.h` que conterá a API da simulação. Um esboço desse header:

```c
/* myfirst_sim.h -- Chapter 17 simulation API. */
#ifndef _MYFIRST_SIM_H_
#define _MYFIRST_SIM_H_

struct myfirst_softc;

/* Attach/detach the simulation. Called from myfirst_attach/detach. */
int  myfirst_sim_attach(struct myfirst_softc *sc);
void myfirst_sim_detach(struct myfirst_softc *sc);

/* Enable/disable the simulation's autonomous updates. */
void myfirst_sim_enable(struct myfirst_softc *sc);
void myfirst_sim_disable(struct myfirst_softc *sc);

/* Command scheduling: called when the driver writes CTRL.GO. */
void myfirst_sim_start_command(struct myfirst_softc *sc);

/* Sysctl registration for simulation controls. */
void myfirst_sim_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_SIM_H_ */
```

Cinco funções, um arquivo de header, um arquivo de código-fonte. A API é pequena. O estado interno da simulação vive em uma `struct myfirst_sim`, apontada pela softc via `sc->sim`, no mesmo padrão de `sc->hw`.

### A Estrutura de Estado da Simulação

Dentro de `myfirst_sim.h`, defina a estrutura de estado:

```c
struct myfirst_sim {
        /* Autonomous update callout. Fires every sensor_interval_ms. */
        struct callout       sensor_callout;

        /* Command completion callout. Fires DELAY_MS after CTRL.GO. */
        struct callout       command_callout;

        /* Last scheduled command's data. Saved so command_cb can
         * latch DATA_OUT when it fires. */
        uint32_t             pending_data;

        /* Whether a command is currently in flight. */
        bool                 command_pending;

        /* Baseline sensor value; the callout oscillates around this. */
        uint32_t             sensor_baseline;

        /* Counter used by the sensor oscillation algorithm. */
        uint32_t             sensor_tick;

        /* Operation counter. Written to OP_COUNTER on every completion. */
        uint32_t             op_counter;

        /* Whether the simulation is running. Stops cleanly at detach. */
        bool                 running;
};
```

Sete campos. Dois são callouts (atualizações de sensor e conclusão de comando). Um é estado booleano (comando em andamento). Um é o dado pendente salvo. Três são contadores internos. Um é o flag de execução.

A softc ganha um ponteiro para a estrutura de simulação:

```c
struct myfirst_softc {
        /* ... all existing fields, including hw ... */
        struct myfirst_sim   *sim;
};
```

A alocação e a inicialização acontecem em `myfirst_sim_attach`, chamado de `myfirst_attach` após `myfirst_hw_attach` já ter configurado `sc->hw`. Colocar o sim attach após o hw attach é deliberado: a simulação lê e escreve registradores pelos acessores do Capítulo 16, portanto os acessores devem estar prontos antes de a simulação iniciar.

### O Sensor Callout: A Primeira Atualização Autônoma

A menor atualização de simulação útil é o sensor callout. Ele dispara a cada 100 milissegundos, atualiza o registrador `SENSOR` e se recarrega. O código:

```c
static void
myfirst_sim_sensor_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t config, interval_ms, amplitude, phase, value;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        /* Read the current SENSOR_CONFIG. */
        config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
        interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
            MYFIRST_SCFG_INTERVAL_SHIFT;
        amplitude = (config & MYFIRST_SCFG_AMPLITUDE_MASK) >>
            MYFIRST_SCFG_AMPLITUDE_SHIFT;

        /* Compute a simple oscillation. phase cycles 0..7 and back. */
        sim->sensor_tick++;
        phase = sim->sensor_tick & 0x7;
        value = sim->sensor_baseline +
                ((phase < 4) ? phase : (7 - phase)) *
                (amplitude / 4);

        /* Publish. */
        CSR_WRITE_4(sc, MYFIRST_REG_SENSOR, value);

        /* Re-arm at the current interval. */
        if (interval_ms == 0)
                interval_ms = 100;
        callout_reset_sbt(&sim->sensor_callout,
            interval_ms * SBT_1MS, 0,
            myfirst_sim_sensor_cb, sc, 0);
}
```

Passo a passo.

Primeiro, o callout verifica o lock. Um callout que foi agendado com `callout_init_mtx(&co, &mtx, 0)` adquire automaticamente `mtx` antes de chamar o callback, portanto a asserção é redundante para um driver bem disciplinado, mas é barata e documenta o invariante. Kernels de depuração detectam qualquer violação.

Segundo, o callout verifica o flag `running`. Se a simulação está sendo desmontada (caminho de detach), o callout sai imediatamente sem se recarregar. O caminho de detach então drena o callout com `callout_drain`, que aguarda qualquer callback em andamento terminar.

Terceiro, o callback lê `SENSOR_CONFIG` para conhecer o intervalo e a amplitude. Ler a configuração a cada vez significa que o usuário pode alterá-la por meio de um sysctl e o próximo callback usará o novo valor, sem nenhum mecanismo de sinalização além da escrita no registrador.

Quarto, o callback calcula um valor oscilante. O truque `phase < 4 ? phase : (7 - phase)` produz uma onda triangular que vai 0, 1, 2, 3, 4, 3, 2, 1, 0, 1, ... ao longo de chamadas sucessivas. Multiplicado por `amplitude / 4`, ele oscila entre `baseline` e `baseline + amplitude`. A fórmula é simples por design; o objetivo é produzir mudança visível, não modelar um sensor real.

Quinto, o callback escreve o novo valor em `SENSOR`. A escrita é o que o driver ou um observador no espaço do usuário verá.

Sexto, o callback se recarrega. `callout_reset_sbt` recebe um tempo binário com sinal, portanto `interval_ms * SBT_1MS` converte milissegundos para as unidades corretas. O argumento `pr` (precisão) é zero, o que significa que o kernel pode atrasar o callback em até 100% do intervalo para agrupar com outros timers; um valor menor forçaria uma temporização mais estrita. O argumento `flags` é zero, o que significa nenhuma flag especial como `C_DIRECT_EXEC` ou `C_HARDCLOCK`; o callback é executado em uma thread de callout normal com o mutex registrado mantido.

Se `interval_ms` for zero, o código assume o padrão de 100 ms. Um intervalo zero se recarregaria imediatamente com zero de atraso, o que colocaria o kernel em um loop fechado; a guarda é defensiva.

### Inicializando o Sensor Callout

Em `myfirst_sim_attach`:

```c
int
myfirst_sim_attach(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim;

        sim = malloc(sizeof(*sim), M_MYFIRST, M_WAITOK | M_ZERO);

        /* Initialise the callouts with the main mutex. */
        callout_init_mtx(&sim->sensor_callout, &sc->mtx, 0);
        callout_init_mtx(&sim->command_callout, &sc->mtx, 0);

        /* Pick a baseline sensor value; 0x1000 is arbitrary but visible. */
        sim->sensor_baseline = 0x1000;

        /* Default config: 100 ms interval, amplitude 64. */
        MYFIRST_LOCK(sc);
        CSR_WRITE_4(sc, MYFIRST_REG_SENSOR_CONFIG,
            (100 << MYFIRST_SCFG_INTERVAL_SHIFT) |
            (64 << MYFIRST_SCFG_AMPLITUDE_SHIFT));
        CSR_WRITE_4(sc, MYFIRST_REG_DELAY_MS, 500);
        CSR_WRITE_4(sc, MYFIRST_REG_FAULT_MASK, 0);
        CSR_WRITE_4(sc, MYFIRST_REG_FAULT_PROB, 0);
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, 0);
        MYFIRST_UNLOCK(sc);

        sc->sim = sim;
        return (0);
}
```

A função aloca a estrutura de simulação, inicializa seus callouts com o mutex do driver (para que os callbacks executem com `sc->mtx` mantido automaticamente), escolhe um valor de sensor baseline, escreve a configuração padrão nos registradores de simulação sob o lock e armazena o ponteiro na softc.

Os callouts ainda não foram iniciados. A simulação está pronta mas ociosa. Uma função separada, `myfirst_sim_enable`, inicia os callouts; um usuário pode ativar e desativar a simulação por meio de um sysctl. Essa separação é útil para depuração e para testes que precisam de um estado inicial sabidamente silencioso.

### Habilitando e Desabilitando a Simulação

```c
void
myfirst_sim_enable(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t config, interval_ms;

        MYFIRST_LOCK_ASSERT(sc);

        if (sim->running)
                return;

        sim->running = true;

        config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
        interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
            MYFIRST_SCFG_INTERVAL_SHIFT;
        if (interval_ms == 0)
                interval_ms = 100;

        callout_reset_sbt(&sim->sensor_callout,
            interval_ms * SBT_1MS, 0,
            myfirst_sim_sensor_cb, sc, 0);
}

void
myfirst_sim_disable(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        sim->running = false;

        callout_stop(&sim->sensor_callout);
        callout_stop(&sim->command_callout);
}
```

`myfirst_sim_enable` define `running` como true e agenda o primeiro sensor callout. `myfirst_sim_disable` limpa o flag e para ambos os callouts. `callout_stop` não aguarda nenhum callback em andamento terminar; apenas cancela qualquer reagendamento pendente. O callback em andamento perceberá `running == false` em sua próxima invocação e encerrará.

Observe que nem `callout_stop` nem `callout_reset_sbt` liberam o mutex. Ambos podem ser chamados com o mutex adquirido sem problema. É por isso que `callout_init_mtx` foi utilizado: o mutex é passado ao subsistema de callout de modo que os callbacks sempre executem com ele adquirido, e as chamadas de controle nunca precisam disputar o lock.

Observe também que `myfirst_sim_disable` não drena os callouts. A drenagem deve ocorrer no detach, não no disable. O motivo é que `callout_drain` dorme, e dormir com um mutex adquirido é ilegal. A drenagem acontece no caminho do detach, após o mutex ter sido liberado.

### Conectando a Simulação ao Attach e ao Detach

Em `myfirst_attach` (o arquivo principal `myfirst.c`), adicione a chamada de attach da simulação após o attach da camada de hardware:

```c
int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        int error;

        sc = device_get_softc(dev);

        /* ... existing Chapter 11-15 init ... */

        /* Chapter 16: hardware layer. */
        error = myfirst_hw_attach(sc);
        if (error != 0)
                goto fail_hw;

        /* Chapter 17: simulation backend. */
        error = myfirst_sim_attach(sc);
        if (error != 0)
                goto fail_sim;

        /* ... existing sysctl and cdev setup ... */

        myfirst_hw_add_sysctls(sc);
        myfirst_sim_add_sysctls(sc);

        /* Enable the simulation by default. */
        MYFIRST_LOCK(sc);
        myfirst_sim_enable(sc);
        MYFIRST_UNLOCK(sc);

        return (0);

fail_sim:
        myfirst_hw_detach(sc);
fail_hw:
        /* ... existing unwind ... */
        return (error);
}
```

A simulação é conectada depois da camada de hardware e antes do registro dos sysctls. Ela é habilitada ao final do attach, quando tudo o mais já está pronto. Se qualquer etapa falhar, o caminho de desfazimento percorre a sequência na ordem inversa: desabilita o que foi habilitado, desconecta o que foi conectado, libera o que foi alocado.

Em `myfirst_detach`, o detach da simulação ocorre cedo, antes do detach de hardware:

```c
int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* ... existing cdev destroy, wait for outstanding fds ... */

        /* Chapter 17: stop the simulation, drain its callouts. */
        MYFIRST_LOCK(sc);
        myfirst_sim_disable(sc);
        MYFIRST_UNLOCK(sc);
        myfirst_sim_detach(sc);

        /* Chapter 16: detach the hardware layer. */
        myfirst_hw_detach(sc);

        /* ... existing synchronization primitive teardown ... */

        return (0);
}
```

Observe a ordenação. A desabilitação acontece com o lock mantido; o detach ocorre após a liberação do lock. O detach é onde a drenagem acontece:

```c
void
myfirst_sim_detach(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim;

        if (sc->sim == NULL)
                return;

        sim = sc->sim;
        sc->sim = NULL;

        callout_drain(&sim->sensor_callout);
        callout_drain(&sim->command_callout);

        free(sim, M_MYFIRST);
}
```

`callout_drain` aguarda qualquer callback em execução terminar. Se o callback estiver rodando em outro CPU quando `callout_drain` é chamado, `callout_drain` bloqueia até que o callback conclua e retorne. Após o retorno de `callout_drain`, nenhum callback será executado novamente para aquele callout, portanto liberar o estado da simulação é seguro.

O padrão é o mesmo que o Capítulo 13 ensinou para callouts em geral. A única sutileza é que o mutex deve ser liberado antes de chamar `callout_drain`; se o callback estiver aguardando para adquirir o mutex quando chamamos o drain, e estivermos segurando o mutex, entramos em deadlock. A ordenação unlock-antes-de-drain é essencial.

### Primeiro Teste: As Atualizações do Sensor São Visíveis

Construa e carregue o driver. Em seguida:

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4096
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4128
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4144
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4160
```

O valor muda entre as leituras. O valor de referência é `0x1000 = 4096`, e a oscilação sobe `amplitude / 4 = 16` por tick, resultando nos valores 4096, 4112, 4128, 4144, 4160, 4144, 4128, 4112, 4096, ... Quatro valores para cima, quatro para baixo, de volta à referência.

Se você ler rápido o suficiente, poderá ver o mesmo valor duas vezes (o callout dispara a cada 100 ms; um sysctl que leva menos tempo para ser despachado do que 100 ms pode ver o mesmo estado). Diminua o ritmo e leia aproximadamente uma vez por segundo, e você verá a oscilação completa.

Tente alterar a configuração:

```text
# sysctl dev.myfirst.0.reg_sensor_config=0x01000040
```

Isso define o intervalo para `0x0100 = 256` ms e a amplitude para `0x0040 = 64`. O sensor agora é atualizado a cada 256 ms. Você pode ver a mudança no log de acessos:

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sleep 2
# sysctl dev.myfirst.0.access_log_enabled=0
# sysctl dev.myfirst.0.access_log
```

O log deve mostrar aproximadamente 8 escritas em `SENSOR` na janela de 2 segundos (2000 ms / 256 ms ~= 7,8). Antes da mudança de configuração, a taxa teria sido de 20 escritas em 2 segundos.

Isso é o que a simulação está produzindo. O driver não faz nada durante esse tempo; a simulação roda por conta própria, conduzida pelo callout do sensor.

### Segundo Callout: A Conclusão de Comando

O callout do sensor é periódico. O callout de comando é one-shot: dispara uma vez, quando um comando é concluído, e não faz nada até que o próximo comando seja emitido.

```c
static void
myfirst_sim_command_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t status;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running || !sim->command_pending)
                return;

        /* Complete the command: copy pending data to DATA_OUT,
         * set STATUS.DATA_AV, clear STATUS.BUSY, increment OP_COUNTER. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, sim->pending_data);

        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~MYFIRST_STATUS_BUSY;
        status |= MYFIRST_STATUS_DATA_AV;
        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

        sim->op_counter++;
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, sim->op_counter);

        sim->command_pending = false;
}
```

O callback faz quatro coisas. Copia os dados pendentes para `DATA_OUT` (o "resultado" da simulação). Atualiza `STATUS`: `BUSY` é limpo, `DATA_AV` é definido. Incrementa `OP_COUNTER`. E limpa o flag `command_pending`.

O comando é iniciado por `myfirst_sim_start_command`, chamada pelo driver quando ele escreve `CTRL.GO`:

```c
void
myfirst_sim_start_command(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t data_in, delay_ms;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        if (sim->command_pending) {
                /* Command already in flight. Real devices might reject
                 * this, set an error bit, or queue the command. For the
                 * simulation, the simplest behaviour is to treat it as
                 * a no-op. The driver should not do this. */
                device_printf(sc->dev,
                    "sim: overlapping command; ignored\n");
                return;
        }

        /* Snapshot DATA_IN now; the callout fires later. */
        data_in = CSR_READ_4(sc, MYFIRST_REG_DATA_IN);
        sim->pending_data = data_in;

        /* Set STATUS.BUSY, clear STATUS.DATA_AV. */
        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                status |= MYFIRST_STATUS_BUSY;
                status &= ~MYFIRST_STATUS_DATA_AV;
                CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
        }

        /* Read the configured delay. */
        delay_ms = CSR_READ_4(sc, MYFIRST_REG_DELAY_MS);
        if (delay_ms == 0) {
                /* Zero delay: complete on the next tick. */
                delay_ms = 1;
        }

        sim->command_pending = true;

        callout_reset_sbt(&sim->command_callout,
            delay_ms * SBT_1MS, 0,
            myfirst_sim_command_cb, sc, 0);
}
```

A função tira um snapshot de `DATA_IN` no estado da simulação, define `STATUS.BUSY`, limpa `STATUS.DATA_AV`, lê o `DELAY_MS` configurado e agenda o callout de comando. O callout de comando dispara `delay_ms` milissegundos depois e conclui o comando.

Um ponto sutil: por que tirar um snapshot de `DATA_IN` em vez de lê-lo no callout de comando? Porque o driver pode escrever em `DATA_IN` novamente antes que o callout dispare. A simulação deve usar o valor que estava em `DATA_IN` quando `CTRL.GO` foi escrito, não o que quer que esteja lá quando o callout executar. Fazer o snapshot no momento do início é o comportamento correto para a maioria dos dispositivos reais também: o dispositivo lê o registrador de comando quando o comando é emitido e não o relê durante a execução.

### Interceptando `CTRL.GO`

A função `myfirst_sim_start_command` é chamada quando o driver escreve `CTRL.GO`. A interceptação de escrita fica no helper `myfirst_ctrl_update` existente (introduzido no Capítulo 16):

```c
void
myfirst_ctrl_update(struct myfirst_softc *sc, uint32_t old, uint32_t new)
{
        /* Chapter 16 behaviour: log ENABLE transitions. */
        if ((old & MYFIRST_CTRL_ENABLE) != (new & MYFIRST_CTRL_ENABLE)) {
                device_printf(sc->dev, "CTRL.ENABLE now %s\n",
                    (new & MYFIRST_CTRL_ENABLE) ? "on" : "off");
        }

        /* Chapter 17: if GO was set, start a command in the simulation. */
        if (!(old & MYFIRST_CTRL_GO) && (new & MYFIRST_CTRL_GO)) {
                myfirst_sim_start_command(sc);
                /* Clear the GO bit; it is a one-shot trigger. */
                CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_GO, 0);
        }
}
```

A extensão monitora o bit `GO` transitando de 0 para 1. Quando isso ocorre, ela chama `myfirst_sim_start_command` e imediatamente limpa o bit `GO`, porque `GO` é um gatilho one-shot: ele se afirma, o comando começa, o bit volta a zero. Este é um padrão comum para bits de "início" em hardware real; o hardware limpa o bit automaticamente assim que o comando foi aceito.

O comportamento de auto-limpeza significa que o driver não precisa se lembrar de limpar o bit ele mesmo. Ele escreve `CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO)` para iniciar um comando, e a simulação cuida do resto.

### Segundo Teste: O Caminho de Comando

Com o código do caminho de comando no lugar, a simulação agora suporta um ciclo de comando. Experimente:

```text
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1

# sysctl dev.myfirst.0.reg_ctrl_set_datain=0x12345678   # (new sysctl for DATA_IN)
# sysctl dev.myfirst.0.reg_ctrl_set=0x200                # bit 9 = CTRL.GO

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3    # BUSY | READY

# sleep 1
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 9    # READY | DATA_AV

# sysctl dev.myfirst.0.reg_data_out
dev.myfirst.0.reg_data_out: 305419896   # 0x12345678

# sysctl dev.myfirst.0.reg_op_counter
dev.myfirst.0.reg_op_counter: 1
```

Ler o status logo após `CTRL.GO` mostra `BUSY|READY`. Aguardar o atraso de 500 ms e ler novamente mostra `READY|DATA_AV`. Ler `DATA_OUT` retorna o valor que estava em `DATA_IN` quando `CTRL.GO` foi escrito. `OP_COUNTER` foi incrementado em um.

Um novo sysctl `reg_ctrl_set_datain` foi implicado no exemplo; é uma adição trivial que espelha `reg_ctrl_set`, mas escreve em `DATA_IN` em vez de `CTRL`. O padrão é idêntico ao `reg_ctrl_set` do Capítulo 16: um `SYSCTL_ADD_PROC` com um handler com permissão de escrita. Adicione um sysctl por registrador que o usuário possa querer acessar diretamente.

### Proteção Contra Comandos Sobrepostos

A verificação de `command_pending` em `myfirst_sim_start_command` impede comandos sobrepostos. Se o driver escrever `CTRL.GO` enquanto um comando já está em execução, a simulação ignora o segundo comando e registra um aviso. Isso não é realista para todo dispositivo real (alguns enfileiram comandos, outros os rejeitam com um erro), mas é o comportamento correto mais simples para uma simulação didática.

O driver, por sua vez, não deve emitir um comando enquanto um já está em execução. O driver pode fazer polling em `STATUS.BUSY` para verificar se o dispositivo está pronto para aceitar um novo comando, e aguardar até que `BUSY` seja limpo antes de escrever `CTRL.GO`. A Seção 5 ensina esse padrão no driver.

### Sysctls para os Controles da Simulação

Os registradores da simulação já estão expostos através dos sysctls de registrador do Capítulo 16 (`reg_sensor`, `reg_sensor_config`, `reg_delay_ms`, `reg_fault_mask`, `reg_fault_prob`, `reg_op_counter`). O Capítulo 17 adiciona alguns sysctls de nível superior que não são mapeados em registradores:

```c
void
myfirst_sim_add_sysctls(struct myfirst_softc *sc)
{
        struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
        struct sysctl_oid *tree = sc->sysctl_tree;

        SYSCTL_ADD_BOOL(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_running", CTLFLAG_RD,
            &sc->sim->running, 0,
            "Whether the simulation callouts are active");

        SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_sensor_baseline", CTLFLAG_RW,
            &sc->sim->sensor_baseline, 0,
            "Baseline value around which SENSOR oscillates");

        SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_op_counter_mirror", CTLFLAG_RD,
            &sc->sim->op_counter, 0,
            "Mirror of the OP_COUNTER value (for observability)");

        /* Add writeable sysctls for the register-mapped fields. */
        SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "reg_delay_ms_set",
            CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
            sc, MYFIRST_REG_DELAY_MS, myfirst_sysctl_reg_write,
            "IU", "Command delay in milliseconds (writeable)");

        SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "reg_sensor_config_set",
            CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
            sc, MYFIRST_REG_SENSOR_CONFIG, myfirst_sysctl_reg_write,
            "IU", "Sensor config: interval|amplitude (writeable)");
}
```

Os sysctls de nível superior `sim_running`, `sim_sensor_baseline` e `sim_op_counter_mirror` expõem estado interno que não é mapeado em registradores. Os demais adicionam sysctls com permissão de escrita para os registradores do Capítulo 17, usando o handler `myfirst_sysctl_reg_write` existente do Capítulo 16.

A adição mantém a visão do usuário consistente: todo estado interessante é visível através de `sysctl dev.myfirst.0`, e os sysctls com permissão de escrita seguem o padrão de sufixo `_set` que o driver já usa.

### O que o Estágio 1 Realizou

Ao final da Seção 3, o driver tem:

- Um novo arquivo `myfirst_sim.c` com o backend de simulação.
- Um novo header `myfirst_sim.h` com a API da simulação.
- Uma nova estrutura de estado da simulação apontada por `sc->sim`.
- Dois callouts: um para atualizações periódicas do sensor, um para conclusão de comando one-shot.
- Uma extensão de `myfirst_ctrl_update` que intercepta `CTRL.GO`.
- Sysctls com permissão de escrita para os novos registradores que devem ser configuráveis.

A tag de versão passa a ser `1.0-sim-stage1`. O driver ainda faz tudo o que o Capítulo 16 ensinou; agora ele também tem dois callouts que produzem comportamento autônomo nos registradores.

Construa, carregue e teste:

```text
# cd examples/part-04/ch17-simulating-hardware/stage1-backend
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.sim_running
# sysctl dev.myfirst.0.reg_sensor
# sleep 1
# sysctl dev.myfirst.0.reg_sensor
# sysctl dev.myfirst.0.reg_op_counter
# kldunload myfirst
```

Você deve ver que `sim_running` é 1, o valor do sensor muda, e `op_counter` permanece zero até que você emita um comando. Se `kldunload` retornar sem erros, o caminho de detach está correto.

### O que o Estágio 1 Não É

O Estágio 1 tem a simulação rodando, mas os caminhos de syscall do driver ainda não conhecem o novo comportamento. Um `write(2)` em `/dev/myfirst0` ainda escreve no ring buffer, não no dispositivo simulado. Os bytes que entram no ring buffer nunca aparecem em `DATA_IN`, nunca causam um comando, nunca aparecem em `DATA_OUT`. A simulação está pronta; o driver ainda não a está usando de verdade.

A Seção 4 dá o próximo passo: conecta a simulação ao caminho de dados do driver, para que uma escrita do espaço do usuário cause um comando simulado, e o resultado do comando seja o que a leitura do espaço do usuário enxerga. Até lá, a simulação é um enfeite.

### Encerrando a Seção 3

O comportamento de um dispositivo simulado vem de um pequeno conjunto de primitivos. Um callout para atualizações periódicas, um callout para eventos temporizados one-shot, e uma estrutura de estado compartilhada que ambos os callouts consultam. A simulação do Capítulo 17 usa três destes: um callout de sensor que roda sempre, um callout de comando que roda por comando, e um flag interno `command_pending` que impede comandos sobrepostos.

A simulação vive em seu próprio arquivo, `myfirst_sim.c`, que depende de `myfirst_hw.h` do Capítulo 16 para as definições de registradores. A separação mantém o código de simulação isolado; um capítulo posterior que substituir a simulação por código real voltado para PCI pode deletar `myfirst_sim.c` e substituí-lo por `myfirst_pci.c` sem tocar no driver.

A Seção 4 começa a integração real: o caminho de dados do driver começa a enviar e receber pela simulação, e a simulação começa a se parecer com um dispositivo de verdade do ponto de vista do driver.



## Seção 4: Expondo o Dispositivo Falso ao Driver

A Seção 3 construiu um backend de simulação que roda em callouts e escreve nos registradores através dos acessores do Capítulo 16. A Seção 4 faz uma pergunta diferente: como o driver enxerga a simulação? Do lado do driver, qual é o caminho de acesso? Como é o código, e como ele difere do código que conversaria com hardware real?

A resposta curta é que o caminho de acesso não muda. O propósito central da abstração `bus_space(9)` do Capítulo 16 é que o driver não sabe, e não precisa saber, se o bloco de registradores é real ou simulado. A Seção 4 é deliberadamente uma seção mais curta porque o trabalho pesado foi feito no Capítulo 16. O que resta é uma análise cuidadosa de como a abstração sobrevive à adição de comportamento dinâmico, e dos poucos pequenos hooks que o driver precisa para deixar a simulação conduzir seu próprio comportamento.

### A Tag e o Handle, Ainda

O Capítulo 16 configurou o `bus_space_tag_t` e o `bus_space_handle_t` da simulação em `myfirst_hw_attach`:

```c
#if defined(__amd64__) || defined(__i386__)
        hw->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation supports x86 only"
#endif
        hw->regs_handle = (bus_space_handle_t)(uintptr_t)hw->regs_buf;
```

Esse código não muda para o Capítulo 17. A simulação escreve nos registradores através de `CSR_WRITE_4`, que expande para `myfirst_reg_write`, que expande para `bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value)`. A mesma tag; o mesmo handle; o mesmo acessor. Os callouts que a Seção 3 introduziu usam exatamente a mesma API que o caminho de syscall.

Isso não é coincidência. A razão pela qual `bus_space` existe é precisamente para ocultar a diferença entre acesso real e simulado a registradores. Um driver que usa `bus_space` corretamente pode ser apontado para um BAR PCI real ou para uma alocação via `malloc(9)` sem nenhuma alteração no código de acesso. A simulação exercita essa propriedade.

Um exercício útil neste momento: abra `/usr/src/sys/dev/ale/if_ale.c` em uma janela e `myfirst_hw.c` em outra. Compare o código de acesso aos registradores. O driver ALE usa `CSR_READ_4(sc, ALE_REG_XYZ)`; seu driver usa `CSR_READ_4(sc, MYFIRST_REG_XYZ)`. A expansão é idêntica. A única diferença é a tag e o handle: o ALE os obtém de `rman_get_bustag(sc->ale_res[0])` e `rman_get_bushandle(sc->ale_res[0])`; seu driver os obtém da configuração de simulação do Capítulo 16. Substituir a simulação por hardware real, que é o que o Capítulo 18 fará, é uma mudança de uma única função em `myfirst_hw_attach`.

### Por que a Abstração Importa Mais Agora

No Capítulo 16, o argumento para usar `bus_space` era voltado para o futuro: "quando você eventualmente migrar para hardware real, o código do driver não precisará ser alterado". No Capítulo 17, o argumento se torna concreto: o bloco de registradores agora muda por conta própria, e o driver não tem como saber se uma mudança veio de um callout de simulação ou da máquina de estados interna de um dispositivo real. A abstração está fazendo trabalho real.

Considere um exemplo simples. Uma função do driver que faz polling em `STATUS.DATA_AV`:

```c
static int
myfirst_wait_for_data(struct myfirst_softc *sc, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (status & MYFIRST_STATUS_DATA_AV)
                        return (0);

                /* Release the lock, sleep briefly, reacquire. */
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfwait", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

Essa função lê `STATUS`, verifica um bit e, dependendo do resultado, retorna ou dorme por um milissegundo e tenta novamente. Ela não sabe se o bit está sendo definido pelo callout de comando da simulação do Capítulo 17, pela máquina de estados interna de um dispositivo real, ou por qualquer outra coisa. Ela simplesmente faz polling.

A chamada a `pause_sbt` merece atenção. A função libera o lock do driver antes de dormir e o readquire depois. Dormir com um lock mantido bloquearia todos os outros contextos que precisam desse lock, incluindo os callouts de simulação; liberar o lock antes de dormir permite que a simulação execute. `pause_sbt` recebe um identificador de sleep (`"mfwait"`), um tempo binário com sinal (1 ms aqui), uma precisão e flags. A precisão zero significa que o kernel pode agrupar esse sleep com outros sleeps, reduzindo as interrupções de timer.

A Seção 6 revisita a escolha entre `DELAY(9)`, `pause_sbt(9)` e `callout_reset_sbt(9)` com mais profundidade. A versão curta: fazer polling em um registrador mil vezes por segundo dormindo entre cada tentativa não é o padrão mais eficiente, mas é correto, simples e legível. O driver do Capítulo 17 o utiliza porque o protocolo é pequeno e a sobrecarga do polling não tem impacto relevante. Drivers reais de alto desempenho usam interrupções (Capítulo 19) em vez disso.

### A Integração do Caminho de Comando

O caminho de dados atual do driver (`myfirst_write`, do Capítulo 10) escreve bytes no ring buffer. O Capítulo 17 quer rotear esses bytes pela simulação: o usuário escreve um byte, o driver o escreve em `DATA_IN`, define `CTRL.GO`, aguarda `STATUS.DATA_AV`, lê `DATA_OUT` e escreve o resultado no ring buffer. Esse é o ciclo de comando completo.

Para o Estágio 2 do driver do Capítulo 17, adicionamos o caminho de código do ciclo de comando. A função de escrita torna-se:

```c
static int
myfirst_write_cmd(struct myfirst_softc *sc, uint8_t byte)
{
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        /* Wait for the device to be ready for a new command. */
        error = myfirst_wait_for_ready(sc, 100);
        if (error != 0)
                return (error);

        /* Write the byte to DATA_IN. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)byte);

        /* Issue the command. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        /* Wait for the command to complete (STATUS.DATA_AV). */
        error = myfirst_wait_for_data(sc, 2000);
        if (error != 0)
                return (error);

        /* Check for errors. */
        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (status & MYFIRST_STATUS_ERROR) {
                        /* Clear the error latch. */
                        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                            MYFIRST_STATUS_ERROR, 0);
                        return (EIO);
                }
        }

        /* Read the result. DATA_OUT will hold the byte the simulation
         * echoed (or, if loopback is active, the same byte we wrote). */
        (void)CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);

        /* Clear DATA_AV so the next command can set it. */
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);

        return (0);
}
```

Seis etapas. Aguarda o estado de pronto, escreve `DATA_IN`, define `CTRL.GO`, aguarda `DATA_AV`, verifica erros e lê `DATA_OUT`. Limpa `DATA_AV` para ser cuidadoso com a próxima iteração.

O helper `myfirst_wait_for_ready` faz polling de `STATUS.BUSY`:

```c
static int
myfirst_wait_for_ready(struct myfirst_softc *sc, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (!(status & MYFIRST_STATUS_BUSY))
                        return (0);
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfrdy", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

Mesma estrutura de `myfirst_wait_for_data`; apenas o bit sendo verificado é diferente. Ambos os helpers são candidatos razoáveis a uma implementação compartilhada:

```c
static int
myfirst_wait_for_bit(struct myfirst_softc *sc, uint32_t mask,
    uint32_t target, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if ((status & mask) == target)
                        return (0);
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfwait", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

Então `wait_for_ready` chama `wait_for_bit(sc, MYFIRST_STATUS_BUSY, 0, timeout_ms)` e `wait_for_data` chama `wait_for_bit(sc, MYFIRST_STATUS_DATA_AV, MYFIRST_STATUS_DATA_AV, timeout_ms)`. A abstração vale a pena extrair assim que você tiver dois usuários; a Seção 6 revisita esse padrão.

### Onde o Comando se Integra com a Syscall de Escrita

O `myfirst_write` existente do Capítulo 10 empurra bytes para o ring buffer. O Capítulo 17 não substitui esse caminho; ele o aumenta. Para o Estágio 2, adicionamos um hook de ciclo de comando por byte que envia cada byte pela simulação além de empurrá-lo para o ring.

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t buf[64];
        size_t n;
        int error;

        /* ... existing validation ... */

        while (uio->uio_resid > 0) {
                n = MIN(uio->uio_resid, sizeof(buf));
                error = uiomove(buf, n, uio);
                if (error != 0)
                        return (error);

                MYFIRST_LOCK(sc);
                for (size_t i = 0; i < n; i++) {
                        /* Send each byte through the simulated device. */
                        error = myfirst_write_cmd(sc, buf[i]);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }

                        /* Existing Chapter 10 path: push into ring. */
                        error = cbuf_put(sc->cbuf, buf[i]);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }
                }
                MYFIRST_UNLOCK(sc);
        }

        return (0);
}
```

O loop processa um byte por vez para maior clareza. Cada byte passa pelo ciclo de comando e depois é inserido no ring. Um driver real normalmente faria pipeline disso (escreveria vários bytes em um FIFO, emitiria um único comando e leria vários bytes de volta), mas o ponto de ensino do Capítulo 17 é sobre comandos individuais; o pipeline é um refinamento para mais tarde.

Observe o locking. O lock externo cobre o lote inteiro (até 64 bytes). Cada chamada a `myfirst_write_cmd` libera e readquire o lock internamente quando dorme. Outros contextos que precisam do lock (notavelmente os callouts da simulação) têm chance de executar entre os comandos.

### Teste do Estágio 2

Construa, carregue e teste:

```text
# kldload ./myfirst.ko
# echo -n "hi" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_op_counter
dev.myfirst.0.reg_op_counter: 2

# dd if=/dev/myfirst0 bs=1 count=2 2>/dev/null
hi
```

Dois bytes escritos; `OP_COUNTER` incrementado por dois. As leituras retornam os mesmos dois bytes porque a simulação ecoa `DATA_IN` para `DATA_OUT`. O teste valida o fluxo de ponta a ponta: o espaço do usuário escreve, o driver comanda, a simulação processa, o driver lê, o espaço do usuário recebe.

O ciclo completo leva cerca de 1 segundo (500 ms por byte, dois bytes). Isso é lento, mas é realista: um dispositivo real com latência similar (por exemplo, um sensor conectado via SPI que leva 500 ms para produzir uma leitura) se comportaria exatamente assim. Acelerar significa reduzir `DELAY_MS` (tente `sysctl dev.myfirst.0.reg_delay_ms_set=10` para 10 ms por comando).

### O Que Não Muda

Algumas propriedades do driver do Capítulo 16 que não mudam no Estágio 2 merecem ser enumeradas.

O log de acesso continua funcionando. Toda leitura e escrita de registrador que o driver realiza aparece no log. As próprias escritas de registrador da simulação também aparecem (porque passam pelos mesmos acessadores). O log é denso, o que é bom: você pode ver o heartbeat da simulação ao lado da atividade do driver.

A interface sysctl continua funcionando. Todo registrador é legível; todo registrador com permissão de escrita pode ser escrito. As conveniências do Capítulo 16 (ler todos os registradores, escrever registradores específicos) estão inalteradas.

A disciplina de locking continua funcionando. Todo acesso a registrador ocorre sob `sc->mtx`; os callouts da simulação foram projetados para rodar com `sc->mtx` mantido (via `callout_init_mtx`), então não há inversões de ordem de lock para se preocupar.

O caminho de detach continua funcionando. O detach da simulação foi adicionado à ordem de detach existente, e o esvaziamento dos callouts da simulação acontece antes de a região de hardware ser liberada.

### O Que Ainda Precisa de Atenção

Algumas pontas soltas que as Seções 5 e 6 irão aparar.

Os timeouts em `myfirst_wait_for_ready` e `myfirst_wait_for_data` estão codificados diretamente (100 ms e 2000 ms respectivamente). Um timeout configurável (talvez exposto por um sysctl) seria melhor; a Seção 5 adiciona um.

O sleep de `pause_sbt` de 1 ms é um padrão razoável, mas não é ótimo para todas as situações. Em uma simulação rápida (por exemplo, `DELAY_MS=0`), fazer polling a cada 1 ms é desperdiçador; em uma simulação lenta (por exemplo, `DELAY_MS=5000`), fazer polling a cada 1 ms é excessivo. A Seção 6 discute estratégias melhores.

O caminho de escrita do ciclo de comando não interage com a injeção de falhas da simulação. Se a simulação estiver configurada para falhar um comando (trabalho da Seção 7), o driver precisa reconhecer e se recuperar da falha. A Seção 5 adiciona a recuperação básica de erros; a Seção 7 adiciona a infraestrutura de injeção de falhas.

O caminho de leitura do driver ainda não está integrado. As leituras atualmente puxam do ring buffer, que contém o que quer que os comandos de escrita tenham empurrado. Um padrão mais realista teria as leituras também emitindo comandos (um comando "por favor me dê mais um byte"), mas esse é um tópico da Seção 5.

### Encerrando a Seção 4

Expor o dispositivo simulado ao driver não é um mecanismo novo no Capítulo 17. A abstração `bus_space(9)` do Capítulo 16 já cuida disso; as escritas da simulação e as leituras do driver passam pela mesma tag e handle, e o driver nem sabe nem se importa que o bloco de registradores é simulado. O que o Estágio 2 adiciona é o caminho de código do ciclo de comando: o driver escreve `DATA_IN`, define `CTRL.GO`, aguarda `DATA_AV` e lê `DATA_OUT`. Duas funções helper (`myfirst_wait_for_ready` e `myfirst_wait_for_data`) tornam o polling explícito; uma função guarda-chuva (`myfirst_wait_for_bit`) fatoriza a estrutura compartilhada. O driver agora exercita a simulação em um padrão realista de comando e resposta.

A Seção 5 aprofunda a história dos testes. Ela adiciona recuperação de erros, timeouts configuráveis, uma separação mais clara entre os caminhos de leitura e escrita, e os primeiros testes de carga que submetem a simulação a tráfego suficiente para expor condições de corrida.



## Seção 5: Testando o Comportamento do Dispositivo a Partir do Driver

O código de ciclo de comando da Seção 4 funciona para um único byte. Ele expira corretamente quando a simulação está lenta; retorna um valor de dados quando a simulação responde; lê `STATUS` nos pontos apropriados. O que ainda não experimentou é volume, diversidade ou concorrência. Um driver que passa em um teste de um byte pode ainda falhar sob carga, sob escritores concorrentes ou quando a simulação estiver configurada com parâmetros incomuns.

A Seção 5 trata do teste de stress da interação do driver com o dispositivo simulado. Ela adiciona timeouts configuráveis, um caminho de recuperação de erros mais robusto, uma integração do caminho de leitura que permite à simulação dirigir leituras além de escritas, e um conjunto de testes de carga que exercitam o driver por toda a sua superfície comportamental. Ao final da Seção 5, o driver está no Estágio 2 do Capítulo 17 e sobreviveu a uma execução de stress significativa.

### Timeouts Configuráveis

Timeouts codificados diretamente são frágeis. 100 ms é suficiente para o `DELAY_MS` padrão de 500, mas e se o usuário quiser testar uma configuração lenta? Aumentar `DELAY_MS` para 2000 significaria que todo comando expiraria com o limite de 100 ms, o que é um modo de falha falso. Torne os timeouts configuráveis.

Adicione dois campos ao softc (ou a uma sub-estrutura adequada):

```c
struct myfirst_softc {
        /* ... existing fields ... */
        int      cmd_timeout_ms;        /* max wait for command completion  */
        int      rdy_timeout_ms;        /* max wait for device ready         */
};
```

Inicialize-os no attach:

```c
sc->cmd_timeout_ms = 2000;              /* 2 s default                      */
sc->rdy_timeout_ms = 100;               /* 100 ms default                   */
```

Exponha-os por sysctl:

```c
SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "cmd_timeout_ms", CTLFLAG_RW, &sc->cmd_timeout_ms, 0,
    "Command completion timeout in milliseconds");

SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "rdy_timeout_ms", CTLFLAG_RW, &sc->rdy_timeout_ms, 0,
    "Device-ready polling timeout in milliseconds");
```

E altere `myfirst_write_cmd` para usá-los:

```c
error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
/* ... */
error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
```

O conjunto de testes pode agora ajustar os timeouts para corresponder à latência esperada do teste. Um teste de simulação rápida os define como baixos; um teste de simulação lenta os define como altos. Um teste patológico os define como muito baixos e espera erros de timeout.

### Integração do Caminho de Leitura

O `myfirst_read` do Capítulo 10 puxa do ring buffer. O ring buffer contém o que quer que as escritas anteriores tenham empurrado. O Capítulo 17 pode estender esse padrão: quando o ring está vazio e o leitor está disposto a esperar, emite um comando "gerar um byte" para o dispositivo simulado e empurra o resultado para o ring.

Nem todo driver funciona assim. Um dispositivo real tem semântica de produção de dados específica ao dispositivo; uma placa de rede produz bytes à medida que os pacotes chegam, não sob demanda. Para um dispositivo simulado do tipo sensor, um comando de "amostragem" que produz uma leitura por invocação é um padrão natural. Nosso driver o adota.

Adicione uma função `myfirst_sample_cmd` que emite um comando e empurra o resultado para o ring:

```c
static int
myfirst_sample_cmd(struct myfirst_softc *sc)
{
        uint32_t data_out;
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
        if (error != 0)
                return (error);

        /* DATA_IN does not matter for a sample; write a marker. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, 0xCAFE);

        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
        if (error != 0)
                return (error);

        data_out = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);

        /* Push one byte of the sample into the ring. */
        error = cbuf_put(sc->cbuf, (uint8_t)(data_out & 0xFF));
        return (error);
}
```

O caminho de leitura pode agora chamar `myfirst_sample_cmd` quando o ring está vazio. Integre-o ao `myfirst_read`:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t byte;
        int error;

        MYFIRST_LOCK(sc);
        while (uio->uio_resid > 0) {
                while (cbuf_empty(sc->cbuf)) {
                        if (ioflag & O_NONBLOCK) {
                                MYFIRST_UNLOCK(sc);
                                return (EWOULDBLOCK);
                        }

                        /* Chapter 17: trigger a sample when ring is empty. */
                        error = myfirst_sample_cmd(sc);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }
                }
                error = cbuf_get(sc->cbuf, &byte);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
                MYFIRST_UNLOCK(sc);
                error = uiomove(&byte, 1, uio);
                MYFIRST_LOCK(sc);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

Agora uma leitura de `/dev/myfirst0` dispara um comando simulado se o ring estiver vazio, efetivamente puxando um byte por comando da simulação. Combinado com o caminho de escrita da Seção 4 (estilo push), o driver agora exercita ambas as direções.

### Um Primeiro Teste de Carga

Com ambos os caminhos integrados, o driver está pronto para teste de carga. Um script de teste simples:

```sh
#!/bin/sh
# cmd_load.sh: exercise the command path under load.

set -e

# Fast simulation for load testing.
sysctl dev.myfirst.0.reg_delay_ms_set=10

# Spawn 4 writers and 4 readers in parallel.
for i in 1 2 3 4; do
        (for j in $(seq 1 100); do echo -n "X" > /dev/myfirst0; done) &
done
for i in 1 2 3 4; do
        (dd if=/dev/myfirst0 of=/dev/null bs=1 count=100 2>/dev/null) &
done
wait

# Show the resulting counter.
sysctl dev.myfirst.0.reg_op_counter

# Reset to default.
sysctl dev.myfirst.0.reg_delay_ms_set=500
```

Com `DELAY_MS=10` e 4 escritores enviando 100 bytes cada mais 4 leitores recebendo 100 bytes cada, o teste deve completar em cerca de 10 segundos (800 comandos a 10 ms cada, serializados pela simulação). `OP_COUNTER` deve atingir pelo menos 800.

Duas coisas merecem atenção neste teste.

Primeiro, os comandos são serializados. A simulação rejeita comandos sobrepostos (verificação de `command_pending` da Seção 3). O driver aguarda `STATUS.BUSY` limpar antes de emitir o próximo comando. Com múltiplos processos do espaço do usuário disputando o driver, o lock em `sc->mtx` os serializa. A taxa efetiva é de um comando por `DELAY_MS` milissegundos, independentemente de quantos escritores estejam rodando.

Segundo, o driver não mudou para acomodar a concorrência. O mutex do Capítulo 11, as tasks do Capítulo 14, os acessadores do Capítulo 16 e o código de ciclo de comando da Seção 4 se compõem naturalmente. A disciplina que a Parte 3 construiu se paga aqui: nenhuma sincronização adicional é necessária para adicionar hardware simulado a um driver que já se coordena adequadamente.

### Refinamento da Recuperação de Erros

O código de ciclo de comando da Seção 4 limpa `STATUS.ERROR` e retorna `EIO`. A Seção 5 refina isso para distinguir vários casos de erro.

```c
static int
myfirst_write_cmd(struct myfirst_softc *sc, uint8_t byte)
{
        uint32_t status;
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
        if (error != 0) {
                sc->stats.cmd_rdy_timeouts++;
                return (error);
        }

        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)byte);
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
        if (error != 0) {
                sc->stats.cmd_data_timeouts++;
                /* Clear any partial state in the simulation. */
                myfirst_recover_from_stuck(sc);
                return (error);
        }

        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        if (status & MYFIRST_STATUS_ERROR) {
                CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                    MYFIRST_STATUS_ERROR, 0);
                CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                    MYFIRST_STATUS_DATA_AV, 0);
                sc->stats.cmd_errors++;
                return (EIO);
        }

        (void)CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);
        sc->stats.cmd_successes++;
        return (0);
}
```

Três mudanças em relação à Seção 4.

Primeiro, contadores por tipo de erro. `sc->stats` é uma estrutura de contadores `uint64_t` que o driver atualiza em cada resultado. `cmd_successes`, `cmd_rdy_timeouts`, `cmd_data_timeouts` e `cmd_errors` cada um tem seu próprio contador. Um sysctl os expõe. Sob carga, os contadores tornam-se o diagnóstico principal: "o driver emitiu 800 comandos, 5 expiraram, 0 tiveram erros, 795 foram bem-sucedidos" é um resumo muito mais útil do que "o driver rodou".

Segundo, `myfirst_recover_from_stuck`. Quando um comando expira, a simulação pode ainda estar no meio do processamento. Limpar o estado obsoleto importa:

```c
static void
myfirst_recover_from_stuck(struct myfirst_softc *sc)
{
        uint32_t status;

        MYFIRST_LOCK_ASSERT(sc);

        /* Clear any pending command flag in the simulation. The next
         * CTRL.GO will be accepted regardless of any stale state. */
        if (sc->sim != NULL) {
                sc->sim->command_pending = false;
                callout_stop(&sc->sim->command_callout);
        }

        /* Force-clear BUSY and DATA_AV. */
        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~(MYFIRST_STATUS_BUSY | MYFIRST_STATUS_DATA_AV);
        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
}
```

Note que `myfirst_recover_from_stuck` acessa `sc->sim->command_pending` diretamente. Esse é um caminho legítimo exclusivo da simulação: em hardware real, não haveria tal variável; em vez disso, o driver emitiria um comando de reset para o dispositivo. A função é um caminho de recuperação específico da simulação, e um comentário no código deve dizer isso. Quando o Capítulo 18 substituir a simulação por hardware real, essa função muda completamente.

Terceiro, limpeza de status em caso de erro. O caminho de erro limpa tanto `ERROR` quanto `DATA_AV`. Deixar qualquer um deles definido causaria que o `wait_for_data` do próximo comando retornasse imediatamente (se `DATA_AV` ainda estiver definido) ou observasse um erro inesperado (se `ERROR` ainda estiver definido). A limpeza defensiva garante que a simulação esteja em um estado limpo para o próximo comando.

### Infraestrutura de Estatísticas

A estrutura `sc->stats` merece ser definida corretamente. Coloque-a em `myfirst.h`:

```c
struct myfirst_stats {
        uint64_t        cmd_successes;
        uint64_t        cmd_rdy_timeouts;
        uint64_t        cmd_data_timeouts;
        uint64_t        cmd_errors;
        uint64_t        cmd_rejected;
        uint64_t        cmd_recoveries;
        uint64_t        samples_taken;
        uint64_t        fault_injected;
};
```

Adicione-a ao softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_stats stats;
};
```

Exponha cada contador por sysctl:

```c
static void
myfirst_add_stats_sysctls(struct myfirst_softc *sc)
{
        struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
        struct sysctl_oid *tree = sc->sysctl_tree;

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_successes", CTLFLAG_RD,
            &sc->stats.cmd_successes, 0,
            "Successfully completed commands");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_rdy_timeouts", CTLFLAG_RD,
            &sc->stats.cmd_rdy_timeouts, 0,
            "Commands that timed out waiting for READY");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_data_timeouts", CTLFLAG_RD,
            &sc->stats.cmd_data_timeouts, 0,
            "Commands that timed out waiting for DATA_AV");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_errors", CTLFLAG_RD,
            &sc->stats.cmd_errors, 0,
            "Commands that completed with STATUS.ERROR set");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_recoveries", CTLFLAG_RD,
            &sc->stats.cmd_recoveries, 0,
            "Recovery operations triggered");

        /* ... add the remaining counters ... */
}
```

Sob carga, `sysctl dev.myfirst.0 | grep -E 'cmd_|samples_|fault_'` fornece um resumo em uma única tela do comportamento do caminho de comandos do driver. Após uma execução de teste, comparar os valores antes e depois é a forma de responder à pergunta: "o driver se comportou corretamente?".

### Observando o Driver Sob Carga

Um padrão de depuração útil: observe as estatísticas enquanto um teste de carga está em execução. Em um terminal:

```text
# sysctl dev.myfirst.0 | grep cmd_ > before.txt
```

Em outro terminal, inicie o teste de carga. Aguarde até ele terminar. Em seguida:

```text
# sysctl dev.myfirst.0 | grep cmd_ > after.txt
# diff before.txt after.txt
```

O diff mostra exatamente quantos eventos de cada tipo ocorreram durante a janela de teste. Execuções ideais mostram muitos sucessos e zero timeouts ou erros. Execuções com falhas injetadas (Seção 7) mostram contagens diferentes de zero para os tipos de erro esperados; contagens inesperadas indicam um bug.

Para observação contínua, use `watch`:

```text
# watch -n 1 'sysctl dev.myfirst.0 | grep cmd_'
```

A cada segundo, os contadores atuais são exibidos. Enquanto o teste de carga está em execução, o contador de sucessos sobe, enquanto os contadores de timeouts e erros (idealmente) permanecem estáveis.

### Teste de Comportamento Prático

Um teste concreto para a Seção 5. Configure:

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_delay_ms_set=50    # fast commands
# sysctl dev.myfirst.0.access_log_enabled=1
```

Execute:

```text
# echo -n "Hello, World!" > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=13 of=/dev/null 2>/dev/null
# sysctl dev.myfirst.0 | grep -E 'cmd_|op_counter|samples_'
```

Saída esperada:

```text
dev.myfirst.0.cmd_successes: 26
dev.myfirst.0.cmd_rdy_timeouts: 0
dev.myfirst.0.cmd_data_timeouts: 0
dev.myfirst.0.cmd_errors: 0
dev.myfirst.0.samples_taken: 13
dev.myfirst.0.reg_op_counter: 26
```

Treze escritas (uma por byte de "Hello, World!"), treze leituras (uma por byte de volta). Cada escrita e cada leitura é um comando de simulação, totalizando 26. Todos bem-sucedidos. Todos dentro do orçamento de latência.

Agora inspecione o log de acesso:

```text
# sysctl dev.myfirst.0.access_log | head -20
```

O log mostra o ciclo de comandos em detalhes. Para cada comando de escrita:

1. Leitura de STATUS (polling para BUSY ser limpo)
2. Escrita em DATA_IN (o byte)
3. Leitura/Escrita em CTRL (ativando GO)
4. Leitura de STATUS (polling para DATA_AV ser ativado)
5. Leitura de DATA_OUT (o resultado)
6. Leitura/Escrita em STATUS (limpando DATA_AV)

Para cada comando de amostragem, a mesma sequência mais um `cbuf_put`. O log é denso mas legível, e é onde você procuraria se o driver se comportasse de forma inesperada.

### O Que o Stage 2 Realizou

O Stage 2 integra a simulação ao caminho de dados do driver. Tanto o caminho de escrita quanto o de leitura agora exercitam comandos simulados. Timeouts configuráveis permitem que os testes visem comportamentos específicos. Contadores de estatísticas por tipo de erro expõem a interação do driver com a simulação. Um caminho de recuperação limpa o estado travado quando um comando atinge o timeout. Testes de carga exercitam o driver em volume e confirmam a composição da sincronização da Parte 3 com a simulação do Capítulo 17.

A tag de versão passa para `1.0-sim-stage2`. O driver agora interage com um bloco de registradores dinâmico de uma forma que se aproxima do comportamento em hardware real.

### Encerrando a Seção 5

Testar o driver contra a simulação não é uma atividade separada; é uma extensão natural do caminho de comandos do driver. Adicione timeouts configuráveis, integre comandos simulados tanto na leitura quanto na escrita, monitore estatísticas por resultado e execute carga suficiente para expor os modos de falha mais comuns.

O teste prático valida o ciclo completo: a entrada do espaço do usuário passa pelo driver, pela simulação, e retorna ao espaço do usuário intacta. Os contadores confirmam a ausência de erros. O log de acesso mostra cada acesso a registrador em ordem.

A Seção 6 aprofunda a discussão sobre temporização. O `pause_sbt` de 1 ms em `myfirst_wait_for_bit` é um padrão razoável, mas não é a única escolha. `DELAY(9)` é mais adequado para esperas muito curtas; `callout_reset_sbt` é mais adequado para agendamento do tipo fire-and-forget. Entender quando cada um é apropriado é o tema da Seção 6.

---

## Seção 6: Adicionando Temporização e Atrasos

A temporização é onde o hardware real se torna interessante. Um dispositivo que processa um comando em 5 microssegundos requer um padrão de driver muito diferente de um que processa um comando em 500 milissegundos. Um driver que usa a primitiva de temporização errada para a escala de tempo errada desperdiça CPU em esperas curtas ou acumula latência em esperas longas e, na pior das hipóteses, entra em deadlock sob contenção.

O FreeBSD oferece três primitivas de temporização principais para código de driver: `DELAY(9)`, `pause_sbt(9)` e `callout_reset_sbt(9)`. Cada uma é adequada a uma escala de tempo específica, cada uma tem um custo específico e cada uma tem um requisito de corretude específico. A Seção 6 as apresenta em ordem e mostra como escolher a mais adequada para cada situação na simulação do Capítulo 17.

### As Três Primitivas de Temporização

As primitivas diferem em três eixos: se fazem espera ativa ou dormem, se são canceláveis e qual é o custo.

**`DELAY(n)`** faz espera ativa por `n` microssegundos. Não dorme, não cede a CPU e não libera nenhum lock. É seguro chamá-la a partir de qualquer contexto, inclusive de um spin mutex, de um filtro de interrupção ou de um callout. Seu custo é o tempo de CPU que consome: 100 microssegundos de `DELAY` são 100 microssegundos em que a CPU não pode fazer mais nada. Sua precisão costuma ser bastante boa (poucos microssegundos), limitada pela resolução do contador de timestamp da CPU. É a escolha certa para esperas muito curtas em que dormir seria impraticável.

**`pause_sbt(wmesg, sbt, pr, flags)`** coloca a thread chamante para dormir por `sbt` unidades de tempo em formato signed-binary-time. Enquanto a thread está dormindo, a CPU fica livre para outros trabalhos, e os locks que a thread não mantém ficam disponíveis para outros contextos. Se a thread mantém um lock, `pause_sbt` não o libera; é responsabilidade do chamador liberar os locks que não deveriam ser mantidos durante o sono. A precisão é da ordem do tick do timer do kernel (1 ms em um sistema típico). É a escolha certa para esperas de curta a média duração em que a thread não tem mais nada a fazer. `pause_sbt` não é cancelável por nenhum mecanismo mais curto do que o próprio tempo de sono; uma vez que a thread a chamou, ela dormirá pelo tempo solicitado no mínimo (salvo eventos excepcionais).

**`callout_reset_sbt(c, sbt, pr, fn, arg, flags)`** agenda um callback para ser executado `sbt` unidades de signed-binary-time no futuro. A thread chamante não espera; ela retorna imediatamente. O callback é executado em uma thread de callout quando o tempo expira. O callout pode ser cancelado com `callout_stop` ou `callout_drain`. A precisão é semelhante à de `pause_sbt`. É a escolha certa para agendamento do tipo fire-and-forget, para timeouts que podem ser cancelados antecipadamente e para trabalhos periódicos.

As três primitivas se compõem. Um driver pode usar `DELAY(10)` para aguardar 10 microssegundos para um bit de registrador se estabilizar, `pause_sbt(..., SBT_1MS * 50, ...)` para dormir 50 ms aguardando a conclusão de um comando e `callout_reset_sbt(..., SBT_1S * 5, ...)` para agendar um watchdog 5 segundos no futuro. Cada escolha reflete a escala de tempo e o contexto.

### Tabela de Decisão por Escala de Tempo

Uma referência rápida para escolher entre as três primitivas:

| Duração da Espera | Contexto                                 | Primitiva Preferida                |
|-------------------|------------------------------------------|------------------------------------|
| < 10 us           | Qualquer                                 | `DELAY(9)`                         |
| 10-100 us         | Sem interrupção, sem spin                | `DELAY(9)` ou `pause_sbt(9)`       |
| 100 us - 10 ms    | Sem interrupção, sem spin                | `pause_sbt(9)`                     |
| > 10 ms           | Sem interrupção, sem spin                | `pause_sbt(9)` ou callout          |
| Fire-and-forget   | Qualquer (máquina de estados de callout) | `callout_reset_sbt(9)`             |
| Interrupção/spin  | Qualquer duração                         | Apenas `DELAY(9)`                  |

A tabela é um ponto de partida, não uma regra rígida. A pergunta real é: o chamador pode se dar ao luxo de dormir? Se sim, use pause ou callout. Se não (lock de spin mantido, handler de interrupção, filtro de interrupção, seção crítica), use DELAY.

### Onde o Driver do Capítulo 17 Já Usa Cada Primitiva

O laço de polling da Seção 4 usa `pause_sbt` com atrasos de 1 ms. Isso é adequado para a escala de tempo: o `DELAY_MS` padrão da simulação é 500, então esperamos que o driver durma muitas vezes antes de o comando ser concluído. Dormir permite que outras threads usem a CPU enquanto aguardamos. `DELAY(1000)` também funcionaria, mas desperdiçaria CPU; o driver atualmente faz cerca de 500 polls por comando sem nenhum trabalho útil de CPU entre eles, e `DELAY` ficaria em espera ativa todas as 500 vezes.

Os callouts de simulação da Seção 3 usam `callout_reset_sbt` com intervalos variáveis. Isso é adequado porque a simulação precisa realizar trabalho no futuro sem que nenhuma thread fique aguardando ativamente. `pause_sbt` exigiria uma thread dedicada do kernel para cada callout, o que seria custoso. `DELAY` exigiria que a thread permanecesse em modo kernel por todo o intervalo, o que é inaceitável.

Ainda não há `DELAY` no driver. A Seção 6 o apresenta onde for apropriado.

### Um Bom Uso de `DELAY`

Considere o caminho de recuperação da Seção 5. Após um timeout, o driver chama `myfirst_recover_from_stuck`, que limpa algum estado e retorna. O código funciona, mas há uma condição de corrida sutil: o callout de comando da simulação pode ser disparado entre a detecção do timeout e a chamada de recuperação, alterando `STATUS` de formas inesperadas. Uma pequena correção: use `DELAY(10)` para aguardar 10 microssegundos para que qualquer callout em andamento se conclua antes de limpar o estado.

Exceto que isso está errado. `DELAY(10)` não faz nada útil aqui. O callout não pode ser executado enquanto mantemos `sc->mtx` (pois foi registrado com `callout_init_mtx`), portanto não há condição de corrida. Adicionar um `DELAY` aqui seria cargo cult: adicionar um atraso porque algo pode dar errado, sem um modelo claro do que o atraso está impedindo.

Um exemplo melhor. Alguns hardwares reais exigem um pequeno atraso entre duas escritas em registradores para deixar a lógica interna do dispositivo se estabilizar. Um datasheet pode dizer: "Após escrever em `CTRL.RESET`, aguarde pelo menos 5 microssegundos antes de escrever em qualquer outro registrador." O bit `CTRL.RESET` da simulação do Capítulo 17 não está realmente implementado, mas podemos imaginá-lo. Se estivesse, o código de reset usaria `DELAY(5)`:

```c
static void
myfirst_reset_device(struct myfirst_softc *sc)
{
        MYFIRST_LOCK_ASSERT(sc);

        /* Assert reset. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_RESET);

        /* Datasheet: wait 5 us for internal state to clear. */
        DELAY(5);

        /* Deassert reset. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_RESET, 0);
}
```

Cinco microssegundos é curto demais para `pause_sbt` ser eficiente (a resolução do timer do kernel é tipicamente 1 ms, então `pause_sbt` por 5 us na verdade dormiria até o próximo tick do timer, desperdiçando a maior parte de um milissegundo). `DELAY(5)` é a primitiva correta.

### Um Bom Uso de `pause_sbt` (Com o Lock Liberado)

O laço de polling da Seção 4:

```c
for (i = 0; i < timeout_ms; i++) {
        uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        if ((status & mask) == target)
                return (0);
        MYFIRST_UNLOCK(sc);
        pause_sbt("mfwait", SBT_1MS, 0, 0);
        MYFIRST_LOCK(sc);
}
```

O lock é liberado antes de `pause_sbt` e readquirido depois. Esse padrão é essencial e merece atenção cuidadosa.

Se o lock fosse mantido durante `pause_sbt`, o callout da simulação (que precisa do lock) ficaria bloqueado pela duração do sono. Como o callout é exatamente o que o driver está aguardando, isso causaria um deadlock. Liberar o lock permite que o callout seja executado, que a simulação atualize `STATUS` e que a próxima iteração do laço observe o novo valor.

O requisito de corretude é que nada que o driver se preocupa possa mudar de forma irrecuperável enquanto o lock está liberado. No nosso caso, as variáveis do laço do driver são locais (`i`, `status`, `mask`, `target`); elas não residem em memória que outros contextos tocam. O estado que outros contextos tocam (o bloco de registradores) é exatamente o que queremos que eles atualizem. Portanto, liberar o lock é seguro.

Uma variação desse padrão usa uma variável de condição dormível em vez de polling:

```c
MYFIRST_LOCK_ASSERT(sc);

while ((CSR_READ_4(sc, MYFIRST_REG_STATUS) & mask) != target) {
        int error = cv_timedwait_sbt(&sc->data_cv, &sc->mtx,
            SBT_1MS * timeout_ms, 0, 0);
        if (error == EWOULDBLOCK)
                return (ETIMEDOUT);
}
return (0);
```

A variável de condição aguarda até que o bit alvo mude (sinalizado por algum outro contexto) ou o timeout expire. `cv_timedwait_sbt` libera o mutex automaticamente, dorme e readquire o mutex antes de retornar. Não é necessário envolver o código com `UNLOCK`/`LOCK` explícitos.

Para isso funcionar, o callout da simulação que altera `STATUS` deve chamar `cv_signal` na variável de condição após a alteração. O código em `myfirst_sim_command_cb` passa a ser:

```c
/* After setting DATA_AV: */
cv_broadcast(&sc->data_cv);
```

O laço de espera do driver e a atualização da simulação cooperam por meio da variável de condição: o driver dorme até que algo interessante aconteça, e a simulação o acorda quando isso ocorre. Esse é um padrão muito mais eficiente do que o polling com `pause_sbt`. O driver dorme por toda a espera; apenas um wakeup é necessário; sem thrashing.

O Capítulo 17 usa `pause_sbt` no laço principal de polling por simplicidade didática (você pode ver exatamente quando o sono acontece), mas o padrão com variável de condição é mencionado aqui por ser mais próximo do que os drivers em produção fazem, especialmente para esperas longas.

### Um Bom Uso de `callout_reset_sbt`

Os callouts de simulação da Seção 3 são o exemplo canônico. O callout do sensor dispara a cada 100 ms, atualiza o registrador do sensor e se rearma. O callout de comando dispara uma única vez após `DELAY_MS` milissegundos para concluir um comando. Ambos utilizam `callout_reset_sbt` com:

- `sbt` = o intervalo em unidades SBT
- `pr` = 0 (sem dica de precisão; o kernel pode realizar coalescência)
- `flags` = 0 (sem comportamento especial)

Para uma programação mais precisa, `pr` pode ser definido para limitar a coalescência:

```c
callout_reset_sbt(&sim->sensor_callout,
    interval_ms * SBT_1MS,
    SBT_1MS,                   /* precision: within 1 ms */
    myfirst_sim_sensor_cb, sc, 0);
```

Com `pr = SBT_1MS`, o callout dispara no máximo 1 ms após o tempo solicitado. Para a nossa simulação, a precisão não é crítica; o valor padrão de 0 é suficiente.

Para um callout que precise ser executado em uma CPU específica, o flag `C_DIRECT_EXEC` faz com que o callback seja executado diretamente a partir da interrupção hardclock, em vez de ser despachado para uma thread de callout. Isso oferece melhor desempenho, mas é mais restritivo: o callback não pode dormir nem adquirir sleep locks. A simulação do Capítulo 17 não precisa disso; ela utiliza callouts simples baseados em mutex.

Para um callout alinhado ao limite do hardclock, o flag `C_HARDCLOCK` é o adequado. Nossa simulação também não precisa de alinhamento; o padrão é suficiente.

### Um Exemplo Realista: Simulando um Dispositivo Lento

Vamos combinar as primitivas. Suponha que a simulação esteja configurada com `DELAY_MS=2000` (um dispositivo lento, dois segundos por comando). O driver emite um comando de escrita. O que cada primitiva faz?

1. O driver escreve `DATA_IN`, define `CTRL.GO` e chama `myfirst_wait_for_data(sc, 3000)` (timeout de 3 s).
2. `myfirst_wait_for_data` lê `STATUS`, constata que `DATA_AV` não está definido, libera o lock, chama `pause_sbt("mfwait", SBT_1MS, 0, 0)` e dorme por ~1 ms.
3. Enquanto isso, o `myfirst_sim_command_cb` da simulação está agendado para disparar em ~2000 ms. Ele ainda não está em execução.
4. O loop do driver acorda, readquire o lock, lê `STATUS` novamente, constata que `DATA_AV` ainda não está definido, libera o lock e dorme novamente.
5. Os passos 2 a 4 se repetem cerca de 2000 vezes ao longo dos dois segundos.
6. O callout da simulação dispara. Ele lê `STATUS`, define `DATA_AV` e escreve os valores de conclusão do comando.
7. Na próxima iteração do loop do driver, o driver vê `DATA_AV` definido e retorna.

O tempo total de CPU consumido pelo driver em espera é mínimo. Cada `pause_sbt` é um sleep, não um spin. A CPU fica livre para outros trabalhos durante cada um dos 2000 sleeps.

Agora suponha que o driver tivesse usado `DELAY(1000)` em vez de `pause_sbt(..., SBT_1MS, ...)`. A mesma escala de tempo por iteração, mas um comportamento de CPU completamente diferente. Cada `DELAY(1000)` consome 1000 microssegundos de CPU. 2000 iterações equivalem a 2 segundos de CPU pura. O driver funciona, mas monopoliza um núcleo inteiro durante toda a duração do comando. Se outros processos precisarem de CPU, eles esperam.

Essa diferença ilustra por que `pause_sbt` é a escolha certa para esperas na escala de milissegundos: o sleep é gratuito; o spin custa CPU.

Agora suponha que o driver tivesse usado `callout_reset_sbt` em vez de polling. Isso é mais complexo: `callout_reset_sbt` não faz a thread atual esperar. A thread retornaria imediatamente ao seu chamador. O chamador precisaria saber que deve dormir até o callout disparar. É exatamente isso que as variáveis de condição fazem. O código se tornaria:

```c
MYFIRST_LOCK_ASSERT(sc);

while (!(CSR_READ_4(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_DATA_AV)) {
        int error = cv_timedwait_sbt(&sc->data_cv, &sc->mtx,
            SBT_1MS * timeout_ms, 0, 0);
        if (error == EWOULDBLOCK)
                return (ETIMEDOUT);
}
```

E o callout da simulação chama `cv_broadcast(&sc->data_cv)` após definir `DATA_AV`. A thread do driver dorme uma única vez (não 2000 vezes); o overhead de CPU é mínimo; o tempo de resposta é limitado pelo próprio timing da simulação, não pela taxa de polling do driver.

No Capítulo 17, o padrão de polling é ensinado primeiro porque é mais fácil de ler: o loop `for` é explícito, o `pause_sbt` é explícito, o `CSR_READ_4` é explícito. O padrão com variáveis de condição é mais eficiente, mas exige compreensão das três primitivas do Capítulo 12. Usamos o padrão de polling como ponto de partida e indicamos o padrão com variáveis de condição como a preferência para produção.

### Timing da Própria Simulação

As escolhas de timing da própria simulação merecem atenção.

O callout do sensor dispara em um intervalo especificado pelo registrador `SENSOR_CONFIG`. O padrão é 100 ms. O usuário pode alterá-lo. O kernel respeita o intervalo solicitado com precisão de alguns milissegundos. Isso é suficiente para um sensor didático; um dispositivo real pode exigir precisão abaixo de um microssegundo, o que exigiria callbacks `C_DIRECT_EXEC` ou até fontes de timing não convencionais.

O callout de comando dispara `DELAY_MS` milissegundos após o comando ser emitido. Aqui também, a precisão em milissegundos é suficiente. Os timeouts do driver são calibrados em relação a isso: o timeout padrão de comando é 2000 ms, bem acima do `DELAY_MS` padrão de 500 ms.

O intervalo de polling do driver é de 1 ms. Para um comando que leva 500 ms, isso significa ~500 polls, o que é tolerável. Para um comando que leva 5 ms, são ~5 polls, rápido o suficiente. Para um comando que leva 5 segundos, são 5000 polls, o que é um desperdício. O intervalo de polling poderia ser adaptativo (polls mais longos para comandos mais demorados), mas a complexidade de implementação não compensa para um driver didático.

### Restrições Realistas: Pronto Após 500 ms

Para tornar a questão do timing mais concreta, configure a simulação com um atraso de 500 ms por comando e execute um teste de estresse:

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=500
# time echo -n "test" > /dev/myfirst0
```

Saída esperada:

```text
real    0m2.012s
user    0m0.001s
sys     0m0.005s
```

Quatro bytes, 500 ms cada, 2 segundos no total. Quase todo o tempo é tempo de sleep do kernel; o uso de CPU é desprezível. O driver é eficiente mesmo que o comando seja lento.

Agora aplique estresse com escritores concorrentes:

```text
# for i in 1 2 3 4; do
    (echo -n "ABCD" > /dev/myfirst0) &
  done
# time wait
```

Quatro processos, 4 bytes cada, 500 ms por byte. Total de comandos: 16. A um comando a cada 500 ms (serializados pelo lock do driver), o tempo total é de 8 segundos. Esperado:

```text
real    0m8.092s
```

O lock do driver serializa os comandos, e a própria verificação de `command_pending` da simulação impede sobreposições. O throughput efetivo é de um comando por `DELAY_MS` milissegundos, independentemente do número de escritores.

### Tratando Timeouts de Forma Adequada

O comportamento do driver quando um comando atinge o timeout merece um acompanhamento detalhado. O caminho do comando:

1. O driver escreve `DATA_IN` e define `CTRL.GO`.
2. O driver aguarda até `cmd_timeout_ms` por `STATUS.DATA_AV`.
3. Se a espera atingir o timeout:
   a. Incrementa o contador `cmd_data_timeouts`.
   b. Chama `myfirst_recover_from_stuck` para limpar o estado obsoleto da simulação.
   c. Retorna `ETIMEDOUT` ao chamador.
4. Se a espera for bem-sucedida, continua pelo caminho normal.

A recuperação é importante. Uma simulação que está de fato lenta (e não travada) acabará definindo `DATA_AV`, mas o driver já desistiu. Se o driver não limpar o estado pendente da simulação, um comando subsequente poderá ver o `DATA_AV` residual do comando anterior e achar que o novo comando foi concluído imediatamente. A função de recuperação para o callout pendente, limpa o flag `command_pending` e limpa os bits relevantes de `STATUS`.

Em hardware real, a recuperação equivalente é um reset do dispositivo. O driver escreve `CTRL.RESET`, aguarda brevemente e reinicia. Alguns dispositivos têm um mecanismo de reset separado, acessível pelo espaço de configuração PCI. O padrão é o mesmo: após um timeout, coloque o dispositivo em um estado conhecido antes de fazer qualquer outra coisa.

### Uma Técnica de Depuração: Desaceleração Artificial

Uma técnica de depuração útil para código sensível a timing: desacelerar artificialmente a simulação para ampliar as janelas de condição de corrida.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=3000
# sysctl dev.myfirst.0.cmd_timeout_ms=1000
```

Agora cada comando leva 3 segundos, mas o driver aguarda apenas 1 segundo. Todo comando atinge o timeout. O caminho de recuperação do driver é executado em cada comando. Bugs no caminho de recuperação aparecem imediatamente:

- Se o caminho de recuperação travar, o kernel entra em pânico no primeiro comando.
- Se o caminho de recuperação deixar estado obsoleto, o segundo comando falha de alguma forma incomum.
- Se o caminho de recuperação vazar memória, `vmstat -m | grep myfirst` cresce ao longo do tempo.

Essa é uma técnica de depuração legítima. É difícil estressar drivers reais dessa forma porque é difícil tornar o hardware real lento sob demanda. A simulação, por outro lado, está a apenas um registrador de distância de ser muito lenta. Use essa capacidade para exercitar caminhos de código que a operação normal raramente alcança.

### Erros Comuns de Timing

Um breve catálogo de erros de timing a evitar.

**Erro 1: Usar `DELAY` para milissegundos.** `DELAY(1000000)` faz busy-wait por um segundo inteiro, consumindo um núcleo de CPU durante esse tempo. Use `pause_sbt(..., SBT_1S, ...)` no lugar.

**Erro 2: Manter um lock durante `pause_sbt`.** Isso bloqueia todo contexto que precise do lock. Libere o lock, durma, readquira.

**Erro 3: Fazer spin sem ceder o processador em uma condição que depende de outro contexto.** Se o seu código está aguardando que um callout defina um bit, o callout não pode executar enquanto você faz spin. Ceda com `pause_sbt` (liberando o lock) ou use uma variável de condição.

**Erro 4: Usar `callout_reset_sbt` para uma ação fire-and-forget que deveria ser concluída imediatamente.** Se a ação é rápida, execute-a diretamente. Callouts têm custo de configuração e overhead de escalonamento; eles valem a pena quando o atraso é significativo.

**Erro 5: Definir um timeout menor que o tempo esperado da operação.** Todo comando atinge o timeout. O driver não realiza nenhum trabalho útil. Defina timeouts generosos, mas finitos.

**Erro 6: Definir um timeout muito maior que o tempo esperado da operação.** Uma operação legitimamente lenta mantém a chamada do espaço do usuário suspensa por todo o timeout. Os usuários perdem a paciência. Defina timeouts razoáveis para a operação específica.

**Erro 7: Não drenar os callouts antes de liberar a memória que eles acessam.** O callout dispara após a memória ser liberada, e o callback desreferencia memória já liberada. Drene antes de liberar.

**Erro 8: Rearmar um callout a partir de seu próprio callback sem verificar o cancelamento.** O caminho de detach pede que o callout pare; o callback se rearma mesmo assim. Loop infinito; o detach nunca é concluído. Verifique o flag `running` (como o `myfirst_sim_sensor_cb` da Seção 3 faz) antes de rearmar.

Cada um desses erros é comum em código de driver escrito por iniciantes. Os estágios do Capítulo 17 evitam todos eles seguindo os padrões que o Capítulo 13 ensinou para callouts, o Capítulo 12 ensinou para variáveis de condição, e que a Seção 6 agora torna explícitos.

### Encerrando a Seção 6

Três primitivas de timing, três escalas de tempo, três perfis de custo. `DELAY(9)` para esperas muito curtas (< 100 us). `pause_sbt(9)` para esperas médias (de 100 us a alguns segundos) quando a thread não tem mais nada a fazer. `callout_reset_sbt(9)` para escalonamento fire-and-forget e para trabalho periódico. A escolha correta é um julgamento informado pela escala de tempo, pelo contexto e pelo que a thread estaria fazendo de outra forma.

O driver do Capítulo 17 usa `pause_sbt` em seu loop de polling e `callout_reset_sbt` para os próprios timers da simulação. Um caminho de reset usa `DELAY(5)` onde apropriado. A história de timing do driver é coerente: ele nunca desperdiça CPU, nunca mantém locks durante sleeps e nunca bloqueia outros contextos desnecessariamente.

A Seção 7 adiciona a última peça principal da simulação do Capítulo 17: um framework de injeção de falhas. Com o timing agora confiável, o driver pode ser exercitado sob diversas condições de falha e os caminhos de recuperação de erros podem ser validados.



## Seção 7: Simulando Erros e Condições de Falha

Um driver que passa em todos os testes do caminho feliz e nunca exercita seus caminhos de erro é um driver que eventualmente falhará em produção. Os caminhos de erro são o código que executa quando algo inesperado acontece, e "inesperado" é a palavra mais honesta para descrever o que o hardware real faz em um dia ruim. Uma placa de rede cujo PHY perdeu o sinal de link. Um controlador de disco cuja fila de comandos transbordou. Um sensor cuja calibração saiu do intervalo. Um dispositivo cujo firmware entrou em deadlock após uma sequência específica de comandos.

O hardware real produz essas condições raramente, e geralmente no pior momento possível. Um autor de driver que espera o hardware real produzi-las é um autor cujos clientes encontrarão os bugs. Um autor de driver que usa simulação para produzi-las sob demanda pode corrigir os bugs antes de eles chegarem ao usuário final.

A Seção 7 incorpora um framework de injeção de falhas à simulação do Capítulo 17. O framework permite que o leitor peça à simulação que se comporte mal de formas específicas e controladas: um timeout, uma corrupção de dados, um estado stuck-busy, uma falha aleatória. Cada modo exercita um caminho de código diferente do driver. Ao final da Seção 7, o driver terá enfrentado cada erro que foi projetado para tratar, e o desenvolvedor terá confiança de que os caminhos de erro funcionam.

### A Filosofia de Injeção de Falhas

Antes de escrever código, uma breve pausa filosófica. Como deve ser um framework de injeção de falhas, e como ele não deve ser?

Um bom framework de injeção de falhas:

- Tem como alvo modos de falha específicos, não caos aleatório. Uma falha que diz "a próxima leitura retorna 0xFFFFFFFF" é útil; uma falha que diz "algo ruim pode acontecer" não é.
- É controlável com granularidade fina. Ative falhas, desative-as ou aplique-as parcialmente. Injete uma, injete várias, injete probabilisticamente.
- É observável. Quando uma falha dispara, o framework a registra no log (ou fornece um contador). O testador pode ver o que aconteceu e correlacionar com o comportamento do driver.
- É ortogonal a outros testes. Um teste de carga não deve se importar se as falhas estão habilitadas; um teste de falhas não deve exigir que a carga esteja desabilitada.
- É determinístico quando configurado para isso. Uma falha configurada com probabilidade 100% dispara todas as vezes. Uma falha configurada com probabilidade 50% dispara metade das vezes, e a decisão pseudoaleatória é reproduzível com uma semente.

Um framework de injeção de falhas ruim é inútil (nunca aciona os caminhos que você precisa) ou perigoso (produz falhas das quais o driver não consegue se recuperar nem em princípio). O objetivo é uma não confiabilidade útil, não um caos destrutivo.

O framework do Capítulo 17 atinge esses objetivos com um pequeno conjunto de flags em `FAULT_MASK`, uma probabilidade em `FAULT_PROB`, e um contador que rastreia quantas falhas foram disparadas. A simulação consulta esses valores sempre que uma operação começa e, a seguir, ou prossegue normalmente ou injeta uma falha de acordo com a configuração.

### Os Modos de Falha

Os quatro modos de falha implementados pelo Capítulo 17:

**`MYFIRST_FAULT_TIMEOUT`** (bit 0). O próximo comando nunca é concluído. O callout do comando não é agendado; `STATUS.DATA_AV` nunca é afirmado. O `myfirst_wait_for_data` do driver excede o tempo limite, ativando o caminho de recuperação.

**`MYFIRST_FAULT_READ_1S`** (bit 1). A próxima leitura de `DATA_OUT` retorna `0xFFFFFFFF` em vez do valor real. Isso simula uma leitura de barramento que falha da forma como muitos barramentos de hardware reais falham (uma leitura de um dispositivo desconectado ou com energia cortada comumente retorna todos os bits em 1).

**`MYFIRST_FAULT_ERROR`** (bit 2). O próximo comando é concluído, mas com `STATUS.ERROR` definido. Espera-se que o driver detecte o erro, limpe o latch e reporte um erro ao chamador.

**`MYFIRST_FAULT_STUCK_BUSY`** (bit 3). `STATUS.BUSY` é travado em nível alto e nunca limpa. O `myfirst_wait_for_ready` do driver excede o tempo limite antes que qualquer comando possa ser emitido. Isso simula um dispositivo que travou e precisa de reset.

Cada falha exercita um caminho diferente. A falha de timeout exercita `cmd_data_timeouts` e a recuperação. A falha de leitura-1s testa se o driver percebe dados corrompidos. A falha de erro exercita `cmd_errors` e a lógica de limpeza de erros. A falha de busy travado exercita `cmd_rdy_timeouts`.

Múltiplas falhas podem estar ativas simultaneamente: defina vários bits em `FAULT_MASK`. Cada falha é aplicada de forma independente no ponto da simulação em que é relevante.

### O Campo de Probabilidade de Falha

`FAULT_MASK` seleciona quais falhas são candidatas; `FAULT_PROB` controla com que frequência uma falha candidata realmente dispara. As probabilidades são expressas como inteiros de 0 a 10000, onde 10000 significa 100% (falha sempre) e 0 significa nunca. Isso oferece quatro casas decimais de precisão sem aritmética fracionária.

O teste para verificar se uma falha dispara:

```c
static bool
myfirst_sim_should_fault(struct myfirst_softc *sc)
{
        uint32_t prob, r;

        MYFIRST_LOCK_ASSERT(sc);

        prob = CSR_READ_4(sc, MYFIRST_REG_FAULT_PROB);
        if (prob == 0)
                return (false);
        if (prob >= 10000)
                return (true);

        r = arc4random_uniform(10000);
        return (r < prob);
}
```

`arc4random_uniform(10000)` retorna um inteiro pseudoaleatório em `[0, 10000)`. Se for menor que `prob`, a falha dispara. Com `prob = 5000` (50%), a falha dispara cerca de metade das vezes. Com `prob = 100` (1%), cerca de uma em cada cem operações. Com `prob = 10000`, em todas as operações.

A função é chamada no início de cada operação. Se retornar true, a operação aplica uma falha de `FAULT_MASK` (a lógica da simulação decide qual falha ou quais falhas aplicar, tipicamente a primeira correspondente).

### Implementando a Falha de Timeout

A falha de timeout é a mais simples. Em `myfirst_sim_start_command`, verifique a máscara de falhas antes de agendar o callout do comando:

```c
void
myfirst_sim_start_command(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t data_in, delay_ms, fault_mask;
        bool fault;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        if (sim->command_pending) {
                sc->stats.cmd_rejected++;
                device_printf(sc->dev,
                    "sim: overlapping command; ignored\n");
                return;
        }

        data_in = CSR_READ_4(sc, MYFIRST_REG_DATA_IN);
        sim->pending_data = data_in;

        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                status |= MYFIRST_STATUS_BUSY;
                status &= ~MYFIRST_STATUS_DATA_AV;
                CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
        }

        delay_ms = CSR_READ_4(sc, MYFIRST_REG_DELAY_MS);
        if (delay_ms == 0)
                delay_ms = 1;

        sim->command_pending = true;

        /* Fault-injection check. */
        fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        fault = (fault_mask != 0) && myfirst_sim_should_fault(sc);

        if (fault && (fault_mask & MYFIRST_FAULT_TIMEOUT)) {
                /* Do not schedule the completion callout. The command
                 * will never complete; the driver will time out.
                 * (Simulation does not clear command_pending; the
                 * driver's recovery path is responsible for that.) */
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting TIMEOUT fault\n");
                return;
        }

        /* Save the fault state for the callout to honour. */
        sim->pending_fault = fault ? fault_mask : 0;

        callout_reset_sbt(&sim->command_callout,
            delay_ms * SBT_1MS, 0,
            myfirst_sim_command_cb, sc, 0);
}
```

Quatro mudanças em relação à versão da Seção 3.

Primeiro, um contador de estatísticas para `cmd_rejected` substitui o printf simples.

Segundo, a função lê `FAULT_MASK` e chama `myfirst_sim_should_fault` para decidir se esta operação injeta uma falha.

Terceiro, se a falha de timeout for selecionada, a função retorna sem agendar o callout de conclusão. `command_pending` é definido como true, portanto comandos sobrepostos ainda são rejeitados, mas nenhum callout dispara para concluir o comando.

Quarto, a simulação armazena o estado da falha em `sim->pending_fault` para que o callout (quando disparar, para falhas que não são de timeout) possa honrar a falha.

`pending_fault` é adicionado à estrutura de estado da simulação:

```c
struct myfirst_sim {
        /* ... existing fields ... */
        uint32_t   pending_fault;   /* FAULT_MASK bits to apply at completion */
};
```

### Implementando a Falha de Erro

A falha de erro dispara na conclusão do comando. Em `myfirst_sim_command_cb`:

```c
static void
myfirst_sim_command_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t status, fault;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running || !sim->command_pending)
                return;

        fault = sim->pending_fault;

        /* Always clear BUSY. */
        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~MYFIRST_STATUS_BUSY;

        if (fault & MYFIRST_FAULT_ERROR) {
                /* Set ERROR, do not set DATA_AV. The driver should
                 * detect the error on the next STATUS read. */
                status |= MYFIRST_STATUS_ERROR;
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting ERROR fault\n");
        } else if (fault & MYFIRST_FAULT_READ_1S) {
                /* Normal completion, but DATA_OUT is corrupted. */
                CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, 0xFFFFFFFF);
                status |= MYFIRST_STATUS_DATA_AV;
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting READ_1S fault\n");
        } else {
                /* Normal completion. */
                CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, sim->pending_data);
                status |= MYFIRST_STATUS_DATA_AV;
        }

        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

        sim->op_counter++;
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, sim->op_counter);

        sim->command_pending = false;
        sim->pending_fault = 0;
}
```

Três ramificações. A falha de erro define `STATUS.ERROR` em vez de `STATUS.DATA_AV`, deixando `DATA_OUT` intocado. A falha de leitura-1s escreve `0xFFFFFFFF` em `DATA_OUT` e define `DATA_AV` normalmente. A ramificação normal escreve os dados reais e define `DATA_AV`.

A simulação incrementa `op_counter` e `fault_injected` de forma adequada. O driver verá os efeitos por meio dos valores dos registradores; os contadores permitem que o teste valide que as falhas dispararam.

### Implementando a Falha de Busy Travado

A falha de busy travado é ortogonal ao ciclo de comandos: ela mantém `STATUS.BUSY` definido independentemente de qualquer comando. Adicione um callout que monitora `FAULT_MASK` e mantém `STATUS.BUSY` travado quando o bit está definido:

```c
static void
myfirst_sim_busy_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t fault_mask, status;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        if (fault_mask & MYFIRST_FAULT_STUCK_BUSY) {
                status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (!(status & MYFIRST_STATUS_BUSY)) {
                        status |= MYFIRST_STATUS_BUSY;
                        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
                }
        }

        /* Re-arm every 50 ms. */
        callout_reset_sbt(&sim->busy_callout,
            50 * SBT_1MS, 0,
            myfirst_sim_busy_cb, sc, 0);
}
```

Inicialize e inicie o callout junto com o callout do sensor em `myfirst_sim_attach` e `myfirst_sim_enable`:

```c
callout_init_mtx(&sim->busy_callout, &sc->mtx, 0);
/* ... in enable: */
callout_reset_sbt(&sim->busy_callout, 50 * SBT_1MS, 0,
    myfirst_sim_busy_cb, sc, 0);
/* ... in disable: */
callout_stop(&sim->busy_callout);
/* ... in detach: */
callout_drain(&sim->busy_callout);
```

Agora, quando `FAULT_STUCK_BUSY` está definido, o callout de busy reafirma continuamente `STATUS.BUSY` a cada 50 ms. Qualquer comando que o driver tente emitir encontrará `BUSY` definido, aguardará a limpeza e excederá o tempo limite em `wait_for_ready`. Limpar o bit `FAULT_STUCK_BUSY` interrompe a reafirmação, e `BUSY` retorna ao seu estado natural (aquele que o caminho de comandos deixou).

### Os Caminhos de Tratamento de Erros do Driver

Com as falhas conectadas, os caminhos de tratamento de erros do driver são exercitados. Revise-os agora para confirmar que funcionam corretamente.

**Timeout em `wait_for_ready`** (da Seção 5):

```c
error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
if (error != 0) {
        sc->stats.cmd_rdy_timeouts++;
        return (error);
}
```

Quando `FAULT_STUCK_BUSY` está definido, este caminho é ativado. O chamador vê `ETIMEDOUT`. O driver não tenta emitir o comando porque não consegue. O contador reflete o evento.

**Timeout em `wait_for_data`**:

```c
error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
if (error != 0) {
        sc->stats.cmd_data_timeouts++;
        myfirst_recover_from_stuck(sc);
        return (error);
}
```

Quando `FAULT_TIMEOUT` está definido, este caminho é ativado. O driver chama `myfirst_recover_from_stuck`, que limpa o flag `command_pending` da simulação e `STATUS.BUSY`. O próximo comando vê um estado limpo. O contador reflete o evento.

**Detecção de `STATUS.ERROR`**:

```c
status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
if (status & MYFIRST_STATUS_ERROR) {
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_ERROR, 0);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_DATA_AV, 0);
        sc->stats.cmd_errors++;
        return (EIO);
}
```

Quando `FAULT_ERROR` está definido, este caminho é ativado. O driver limpa `STATUS.ERROR` e `STATUS.DATA_AV`, incrementa o contador e retorna `EIO`.

O **tratamento de `DATA_OUT` corrompido** requer uma adição. Quando `FAULT_READ_1S` está definido, `DATA_OUT` é `0xFFFFFFFF`, mas o driver atualmente não verifica isso. Se o driver deve verificar depende do protocolo: para um sensor, `0xFFFFFFFF` é frequentemente um marcador legítimo de "erro de leitura"; para outros dispositivos, pode ser um valor de dados plausível. O driver do Capítulo 17 o trata como um erro potencial para fins de ilustração:

```c
if (data_out == 0xFFFFFFFF) {
        /* Likely a bus read error. Real devices rarely produce this
         * value as legitimate data; treat as an error. */
        sc->stats.cmd_errors++;
        return (EIO);
}
```

Este comportamento é uma decisão subjetiva. Um driver real para um dispositivo real verificaria o datasheet do dispositivo para os valores inválidos documentados e responderia de forma adequada.

### Testando as Falhas Uma por Uma

Com a infraestrutura no lugar, cada falha pode ser testada isoladamente:

**Teste 1: Falha de timeout a 100%.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x1    # MYFIRST_FAULT_TIMEOUT
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # 100%
# echo -n "test" > /dev/myfirst0
write: Operation timed out
# sysctl dev.myfirst.0.cmd_data_timeouts
dev.myfirst.0.cmd_data_timeouts: 1
# sysctl dev.myfirst.0.fault_injected
dev.myfirst.0.fault_injected: 1
```

O primeiro byte aciona um timeout. O driver se recupera. Os bytes subsequentes também teriam timeout até que a falha seja limpa. Os contadores refletem o evento.

Desative a falha:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# echo -n "test" > /dev/myfirst0
# echo "write succeeded"
write succeeded
# sysctl dev.myfirst.0.cmd_successes
dev.myfirst.0.cmd_successes: 4   # or more, depending on history
```

Os comandos voltam a ter sucesso.

**Teste 2: Falha de erro a 25%.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x4    # MYFIRST_FAULT_ERROR
# sysctl dev.myfirst.0.reg_fault_prob_set=2500   # 25%
# sysctl dev.myfirst.0.cmd_errors
dev.myfirst.0.cmd_errors: 0
# for i in $(seq 1 40); do echo -n "X" > /dev/myfirst0; done
write: Input/output error     (occurs roughly 10 times)
# sysctl dev.myfirst.0.cmd_errors
dev.myfirst.0.cmd_errors: 9    # approximately 25% of 40
```

Cerca de um em cada quatro comandos resulta em erro. O driver detecta o erro, limpa o estado e retorna `EIO`. O contador reflete o evento.

**Teste 3: Falha de busy travado.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x8    # MYFIRST_FAULT_STUCK_BUSY
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # (prob doesn't matter; latch is always on)
# echo -n "X" > /dev/myfirst0
write: Operation timed out
# sysctl dev.myfirst.0.cmd_rdy_timeouts
dev.myfirst.0.cmd_rdy_timeouts: 1
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3    # READY|BUSY
```

O driver não consegue emitir um comando porque `BUSY` está travado em nível alto. Limpe a falha:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sleep 1     # wait for the busy callout to stop re-asserting
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1    # READY only
# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

### Integração: Falhas Aleatórias Sob Carga

O teste mais realista configura falhas aleatórias em baixa probabilidade e executa o driver sob carga:

```sh
#!/bin/sh
# fault_stress.sh: random faults under load.

# 1% probability of TIMEOUT or ERROR (bits 0 and 2 both set).
sysctl dev.myfirst.0.reg_fault_mask_set=0x5
sysctl dev.myfirst.0.reg_fault_prob_set=100

# Fast commands so the test runs in reasonable time.
sysctl dev.myfirst.0.reg_delay_ms_set=10
sysctl dev.myfirst.0.cmd_timeout_ms=50    # very short, to spot timeouts fast

# Load: 8 parallel workers, each doing 200 round trips.
for i in $(seq 1 8); do
    (for j in $(seq 1 200); do
        echo -n "X" > /dev/myfirst0 2>/dev/null
        dd if=/dev/myfirst0 bs=1 count=1 of=/dev/null 2>/dev/null
    done) &
done
wait

# Report.
sysctl dev.myfirst.0 | grep -E 'cmd_|fault_|op_counter'

# Clean up.
sysctl dev.myfirst.0.reg_fault_mask_set=0
sysctl dev.myfirst.0.reg_fault_prob_set=0
sysctl dev.myfirst.0.reg_delay_ms_set=500
sysctl dev.myfirst.0.cmd_timeout_ms=2000
```

Contadores esperados após a execução:

- `cmd_successes`: cerca de 3168 (de ~3200 round trips esperados)
- `cmd_data_timeouts`: cerca de 16 (1% da metade das operações)
- `cmd_errors`: cerca de 16 (1% da metade das operações)
- `fault_injected`: cerca de 32 (soma dos dois)
- `cmd_recoveries`: cerca de 16 (um por timeout)

Os números exatos variam a cada execução (as falhas são aleatórias), mas as proporções devem ser aproximadamente 1% para cada tipo de falha. Se as proporções estiverem muito fora, há um bug na injeção de falhas ou um bug na contagem de erros do driver. De qualquer forma, o teste está fazendo seu trabalho ao revelar anomalias.

### Uma Armadilha Comum: Recuperação que Nunca Limpa Tudo

Um bug que aparece durante os testes de falha é a lógica de recuperação que não limpa tudo. Suponha que `myfirst_recover_from_stuck` tenha esquecido de limpar `STATUS.BUSY`:

```c
/* BUGGY version: */
static void
myfirst_recover_from_stuck(struct myfirst_softc *sc)
{
        MYFIRST_LOCK_ASSERT(sc);
        if (sc->sim != NULL) {
                sc->sim->command_pending = false;
                callout_stop(&sc->sim->command_callout);
        }
        /* Missing: the STATUS cleanup. */
}
```

Executar o teste de estresse mostraria:

- Primeiro timeout: a recuperação é executada, `command_pending` é limpo.
- Segunda tentativa de comando: `STATUS.BUSY` ainda está definido da primeira tentativa. O comando não pode ser emitido. `cmd_rdy_timeouts` sobe.
- Terceira tentativa: o mesmo.
- ... indefinidamente.

O contador `cmd_successes` para de incrementar. `cmd_rdy_timeouts` sobe sem limite. Um teste cuidadoso percebe isso e sinaliza. Um teste descuidado apenas vê "muitos erros" e ignora.

A lição é que a injeção de falhas expõe recuperação incompleta. Um driver que funciona em condições sem falhas pode ainda ter bugs que só aparecem quando falhas ocorrem. O teste de estresse é o que os revela.

### Combinando Falhas

O framework do Capítulo 17 permite que múltiplas falhas estejam ativas simultaneamente:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0xf    # all four faults
# sysctl dev.myfirst.0.reg_fault_prob_set=1000   # 10%
```

Cada operação tem 10% de chance de acionar uma falha. Se acionar, a simulação escolhe uma das falhas habilitadas (na prática, o código as verifica em ordem: TIMEOUT, ERROR, READ_1S, STUCK_BUSY). A resposta do driver deve ser robusta contra todas elas.

Um teste ainda mais severo:

```text
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # 100%
```

Toda operação falha. O driver nunca conclui um comando com sucesso. Todo caminho de erro é executado. Execute o driver por um minuto com esta configuração. Se o driver travar, há um bug em um caminho de erro. Se o driver vazar memória (verifique com `vmstat -m | grep myfirst`), há um vazamento em um caminho de erro. Se o driver entrar em deadlock (verifique com `procstat -kk`), há um deadlock em um caminho de erro.

Executar com 100% de falhas por um período prolongado é um teste padrão para frameworks de injeção de falhas. É intencionalmente hostil; um driver que passa é robusto a condições de falha do mundo real.

### Observabilidade para Testes de Falha

A observabilidade existente do driver (log de acessos, contadores de estatísticas) cobre a maior parte do que os testes de falha precisam. Três adições valem a pena.

**Entradas de log de falhas injetadas.** O log de acessos registra cada acesso a registrador; ele deve registrar que uma falha foi injetada. Adicione um novo tipo de entrada no log de acessos:

```c
#define MYFIRST_CTX_FAULT   0x10   /* fault injected */
```

E registre quando uma falha disparar:

```c
myfirst_access_log_push(sc, offset, 0, 4, true, MYFIRST_CTX_FAULT);
```

Agora o log de acessos mostra, intercaladas com os acessos normais, entradas que indicam "a falha X foi injetada aqui". O testador pode correlacionar a injeção de falhas com o comportamento subsequente do driver.

**Contadores por tipo de falha.** Em vez de um único contador `fault_injected`, rastreie cada tipo:

```c
struct myfirst_stats {
        /* ... existing counters ... */
        uint64_t        fault_timeout;
        uint64_t        fault_read_1s;
        uint64_t        fault_error;
        uint64_t        fault_stuck_busy;
};
```

E incremente o correto com base na falha:

```c
if (fault & MYFIRST_FAULT_TIMEOUT)
        sc->stats.fault_timeout++;
else if (fault & MYFIRST_FAULT_ERROR)
        sc->stats.fault_error++;
else if (fault & MYFIRST_FAULT_READ_1S)
        sc->stats.fault_read_1s++;
```

O sysctl expõe cada contador. O testador pode ver, de relance, quais tipos de falha dispararam e quais não dispararam.

**Um sysctl de resumo de injeção de falhas.** Um único sysctl que reporta "a máscara de falha é X, a probabilidade é Y, o total injetado é Z". Útil para verificações rápidas durante a depuração interativa:

```c
static int
myfirst_sysctl_fault_summary(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        char buf[128];
        uint32_t mask, prob;
        uint64_t injected;

        MYFIRST_LOCK(sc);
        mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        prob = CSR_READ_4(sc, MYFIRST_REG_FAULT_PROB);
        injected = sc->stats.fault_injected;
        MYFIRST_UNLOCK(sc);

        snprintf(buf, sizeof(buf),
            "mask=0x%x prob=%u/10000 injected=%ju",
            mask, prob, (uintmax_t)injected);
        return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}
```

Invocação:

```text
# sysctl dev.myfirst.0.fault_summary
dev.myfirst.0.fault_summary: mask=0x5 prob=100/10000 injected=47
```

Este é o tipo de resumo que cabe em uma linha e conta toda a história da configuração de falha atual.

### Injeção de Falhas Segura

A palavra "segura" precisa de qualificação. A injeção de falhas na simulação do Capítulo 17 é segura porque a simulação roda no mesmo kernel que o driver, e as falhas são locais: um `DATA_OUT` corrompido é um valor de quatro bytes corrompido na memória alocada com `malloc`, não um descritor DMA corrompido que escreve sobre endereços aleatórios de RAM. Um bit `BUSY` travado é um bit travado em um registrador simulado, não uma fila de hardware que acumula até o sistema travar.

A injeção de falhas em hardware real é uma história diferente. Um dispositivo real com timeout deliberado pode manter um lock de barramento indefinidamente, impedindo que outros dispositivos progridam. Uma transação DMA deliberadamente corrompida pode escrever em endereços físicos arbitrários. A injeção de falhas em sistemas de produção requer configuração cuidadosa, hardware de teste dedicado e geralmente um hypervisor ou emulação de dispositivo virtual.

A simulação do Capítulo 17 contorna tudo isso ao rodar em memória. O pior caso de uma falha com comportamento incorreto é um kernel panic (se um KASSERT disparar) ou um deadlock (se o código de recuperação estiver incorreto). Ambos são recuperáveis: um kernel de depuração captura panics de forma limpa, e um driver em deadlock pode ser eliminado com a reinicialização. Nenhum hardware físico corre risco.

Essa segurança é o motivo pelo qual a injeção de falhas pertence à simulação, e não apenas aos testes em produção. Os testes em produção capturam as falhas que o hardware real efetivamente produz; os testes em simulação capturam as falhas que o hardware real às vezes produz e das quais sempre sofre as consequências.

### Hands-On: A Lista de Verificação para Injeção de Falhas

Um procedimento para usar a injeção de falhas de forma eficaz durante o desenvolvimento:

1. Escreva primeiro o caminho de erro do driver. Antes de ativar a falha, leia seu código e identifique como será a recuperação.
2. Ative a falha com 100% de probabilidade brevemente. Verifique se o caminho de erro do driver é executado. Confira os contadores.
3. Ative a falha com baixa probabilidade (1 a 10%). Execute um teste de estresse. Verifique se o driver ainda progride de forma geral.
4. Confira `vmstat -m | grep myfirst` antes e depois do teste de estresse. O uso de memória não deve crescer.
5. Verifique a saída do `WITNESS` em busca de avisos de ordem de lock. Uma falha que causa um caminho incomum pode expor um bug latente de ordem de lock.
6. Verifique a saída do `INVARIANTS` em busca de falhas de asserção. Uma falha que corrompe o estado pode disparar uma invariante.
7. Desative a falha. Execute o caminho feliz. Verifique se tudo retorna ao normal.
8. Documente a falha em `HARDWARE.md` ou `SIMULATION.md`.

Esse procedimento transforma a injeção de falhas em uma atividade disciplinada, não em um experimento caótico. Cada etapa tem um objetivo claro, e cada etapa valida uma propriedade específica do driver.

### Uma Reflexão: O Que a Injeção de Falhas Nos Ensinou

A injeção de falhas é um microcosmo da disciplina mais ampla de desenvolvimento de drivers. O driver precisa lidar com erros que não pode evitar, originados de fontes que não controla, em momentos que não pode prever. A injeção de falhas fornece ao desenvolvedor uma ferramenta para exercitar as três propriedades sob demanda.

Mais do que isso, a injeção de falhas revela a estrutura da filosofia de tratamento de erros do driver. Um driver que trata cada erro como igualmente recuperável lida mal com algumas falhas do mundo real. Um driver que distingue "transitório" (tente novamente) de "catastrófico" (reinicie o dispositivo) tem um modelo mais claro do que o dispositivo está fazendo. Um driver que registra cada erro com contexto suficiente para diagnóstico posterior é um driver cujos bugs podem ser encontrados por análise de logs.

O driver do Capítulo 17 não vai tão longe nesse nível de sofisticação; ele tem um único caminho de tratamento de erros que trata todos os erros como transitórios e tenta novamente apenas se o chamador o fizer. Mas a base está no lugar. Um exercício desafio ao final do capítulo convida o leitor a estender o driver com tratamento por tipo de erro, e a simulação tem os ganchos para exercitar cada tipo.

### Encerrando a Seção 7

A injeção de falhas é como os autores de drivers exercitam o código de tratamento de erros antes que falhas reais ocorram. O framework do Capítulo 17 fornece quatro modos de falha (timeout, read-1s, error, stuck-busy), um campo de probabilidade e observabilidade suficiente para correlacionar falhas injetadas com o comportamento do driver.

Os caminhos de erro do driver são validados ativando cada falha isoladamente e confirmando os incrementos esperados nos contadores e a recuperação esperada. Combinar falhas e executar sob carga em baixas probabilidades exercita a composição dos caminhos de erro. Executar com 100% de probabilidade estressa todos os caminhos de erro simultaneamente.

A tag de versão torna-se `1.0-sim-stage4`. O driver agora passou por todos os erros que foi projetado para tratar, e o desenvolvedor observou cada caminho de erro funcionando corretamente. O trabalho restante é organizacional: refatorar, versionar, documentar e fazer a ponte para o Capítulo 18.



## Seção 8: Refatorando e Versionando Seu Driver de Hardware Simulado

O Stage 4 produziu um driver que funciona corretamente tanto nas condições de caminho feliz quanto de injeção de falhas. O Stage 5 (Seção 8) torna o driver manutenível. As mudanças são organizacionais: fronteiras de arquivo limpas, documentação atualizada, um incremento de versão e a verificação de regressão que confirma que nada quebrou no processo.

Um driver que funciona é valioso. Um driver que funciona e é legível para o próximo colaborador é muito mais valioso. A Seção 8 trata justamente desse segundo passo.

### O Layout Final dos Arquivos

Ao longo do Capítulo 16, o driver era composto por `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sync.h`, `cbuf.c` e `cbuf.h`. O Capítulo 17 adicionou `myfirst_sim.c` e `myfirst_sim.h`. A árvore de arquivos final:

- `myfirst.c`: o ciclo de vida do driver, os handlers de syscall, os primitivos de sincronização dos Capítulos 11 a 15.
- `myfirst_hw.c`: a camada de acesso ao hardware do Capítulo 16. Macros `CSR_*`, acessores de registradores, log de acesso, sysctls de visualização de registradores.
- `myfirst_hw.h`: o mapa de registradores, máscaras de bits, valores fixos e protótipos de API para a camada de hardware.
- `myfirst_sim.c`: o backend de simulação do Capítulo 17. Callout do sensor, callout de comando, callout de busy, lógica de injeção de falhas, API de simulação.
- `myfirst_sim.h`: os protótipos de API da simulação e a estrutura de estado.
- `myfirst_sync.h`: o header de primitivos de sincronização do Capítulo 15.
- `cbuf.c`, `cbuf.h`: o buffer circular do Capítulo 10.

Sete arquivos de código-fonte, três headers, um Makefile e três arquivos de documentação (`HARDWARE.md`, `LOCKING.md` e o novo `SIMULATION.md`). A divisão espelha como os drivers FreeBSD de produção se organizam: um arquivo por responsabilidade, com interfaces nomeadas entre eles.

### Os Campos Específicos da Simulação Migram para a Struct Sim

O Capítulo 17 introduziu o estado da simulação gradualmente. Ao final da Seção 7, alguns campos relacionados à simulação estavam no softc em vez de estarem em `struct myfirst_sim`. O Stage 5 organiza isso.

Campos que devem residir em `struct myfirst_sim`:

- Os três callouts (`sensor_callout`, `command_callout`, `busy_callout`).
- O estado interno da simulação (`pending_data`, `pending_fault`, `command_pending`, `running`, `sensor_baseline`, `sensor_tick`, `op_counter`).

Campos que devem permanecer no softc ou em `struct myfirst_hw`:

- O bloco de registradores e o estado do `bus_space` (`regs_buf`, `regs_tag`, `regs_handle`): `myfirst_hw`.
- Os timeouts do próprio driver (`cmd_timeout_ms`, `rdy_timeout_ms`): o softc.
- As estatísticas (`sc->stats`): o softc (compartilhadas pelos caminhos hw e sim).

O raciocínio: o estado do hardware pertence à camada de hardware; o estado da simulação pertence à camada de simulação; o comportamento visível pelo driver pertence ao driver. O Capítulo 18 substituirá a simulação por um backend PCI real, e a substituição mais limpa é aquela em que `myfirst_sim.c` e `myfirst_sim.h` desaparecem completamente, sendo substituídos por `myfirst_pci.c` e (opcionalmente) `myfirst_pci.h`. Os campos da struct de simulação não precisam sobreviver a essa transição; os campos do driver, sim.

### O Documento `SIMULATION.md`

Um novo arquivo markdown, `SIMULATION.md`, captura a interface da simulação. As seções:

1. **Versão e escopo.** "SIMULATION.md versão 1.0. Capítulo 17 completo."
2. **O que é a simulação.** Um parágrafo: "Um dispositivo simulado baseado em memória do kernel que imita um protocolo de hardware mínimo de comando-resposta, usado para ensinar desenvolvimento de drivers sem exigir hardware real."
3. **O que a simulação faz.** Comportamentos enumerados: autonomia do sensor, ciclo de comando, injeção de falhas, atrasos configuráveis.
4. **O que a simulação não faz.** Limitações enumeradas: sem DMA, sem interrupções, sem PCI, sem precisão de temporização real.
5. **Mapa de callouts.** Tabela de cada callout, seu propósito, seu intervalo padrão e sua relação com o lock do driver.
6. **Modos de falha.** Tabela de cada tipo de falha, seu gatilho, seu efeito e sua recuperação.
7. **Referência de sysctl.** Tabela de cada sysctl exposto pela camada de simulação, seu tipo, seu valor padrão e seu propósito.
8. **Orientação para desenvolvimento.** Como adicionar um novo comportamento à simulação (onde colocar o código, qual disciplina de locking se aplica, como documentar).
9. **Relação com hardware real.** Como os padrões na simulação se mapeiam para padrões em drivers FreeBSD reais.

O documento tem aproximadamente 150 a 200 linhas de markdown. É a fonte única da verdade sobre o que a simulação promete. Um desenvolvedor que o lê deve ser capaz de raciocinar sobre a simulação sem ler o código.

Um exemplo de entrada da tabela de modos de falha:

```text
| Fault              | Bit  | Trigger                          | Effect                        | Recovery            |
|--------------------|------|----------------------------------|-------------------------------|---------------------|
| MYFIRST_FAULT_TIMEOUT | 0  | should_fault() returns true      | Command callout not scheduled | driver timeout path |
| MYFIRST_FAULT_READ_1S | 1  | should_fault() returns true      | DATA_OUT = 0xFFFFFFFF          | driver error check  |
| MYFIRST_FAULT_ERROR   | 2  | should_fault() returns true      | STATUS.ERROR set instead of DATA_AV | driver error path |
| MYFIRST_FAULT_STUCK_BUSY | 3 | FAULT_MASK bit set             | STATUS.BUSY continuously latched | clear FAULT_MASK  |
```

Tabelas como essa tornam a leitura do código muito mais fácil: um leitor vê `MYFIRST_FAULT_READ_1S` no código e pode consultar a história completa em um único lugar.

### O `HARDWARE.md` Atualizado

O `HARDWARE.md` do Capítulo 16 descrevia um bloco de registradores estático. O Capítulo 17 o estende com os registradores dinâmicos e os comportamentos que os acionam. As seções que mudam:

**Mapa de Registradores.** A tabela agora inclui os registradores do Capítulo 17: `SENSOR` (0x28), `SENSOR_CONFIG` (0x2c), `DELAY_MS` (0x30), `FAULT_MASK` (0x34), `FAULT_PROB` (0x38), `OP_COUNTER` (0x3c). Cada um com seu tipo de acesso, valor padrão e uma descrição em uma linha.

**Campos do registrador CTRL.** Um novo bit: `MYFIRST_CTRL_GO` (bit 9). A tabela adiciona uma linha: "bit 9: `GO`, escreva 1 para disparar um comando; o hardware faz o auto-clear".

**Campos do registrador STATUS.** A tabela ganha notas sobre quais bits são dinâmicos no Capítulo 17: `READY` é definido no attach e permanece ativo; `BUSY` é definido por `start_command` e limpo por `command_cb`; `DATA_AV` é definido por `command_cb` e limpo pelo driver após a leitura de `DATA_OUT`; `ERROR` é definido pela injeção de falhas e limpo pelo driver.

**Campos de SENSOR_CONFIG.** Uma nova subseção explicando o layout de dois campos (16 bits baixos para intervalo, 16 bits altos para amplitude).

**Campos de FAULT_MASK.** Uma nova subseção listando cada bit de falha e seu efeito.

**Resumo do comportamento dinâmico.** Uma nova subseção explicando o que muda de forma autônoma: `SENSOR` (a cada 100 ms por padrão), `STATUS.BUSY` (definido/limpo pelo ciclo de comando ou pelo callout `FAULT_STUCK_BUSY`), `STATUS.DATA_AV` (definido por `command_cb`), `OP_COUNTER` (incrementado a cada comando).

**Disciplina de locking para comportamento dinâmico.** Uma frase: "Todas as atualizações dinâmicas acontecem sob `sc->mtx`, que os callouts adquirem automaticamente via `callout_init_mtx`. O caminho de comando do driver adquire o mutex e o libera apenas durante chamadas a `pause_sbt`, período em que os callouts da simulação podem executar."

O documento cresce de aproximadamente 80 linhas (versão do Capítulo 16) para aproximadamente 140 linhas. Ele continua sendo de fácil leitura. As novas seções são organizadas de forma que um leitor em busca de informações sobre o Capítulo 17 as encontre rapidamente.

### O `LOCKING.md` Atualizado

O `LOCKING.md` do Capítulo 15 descrevia uma ordem de lock de `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` e uma ordem de detach que drenava vários primitivos. O Capítulo 16 adicionou o detach da camada de hardware à lista. O Capítulo 17 adiciona o detach da camada de simulação.

A ordem de detach atualizada, do mais externo para o mais interno:

1. Destrua `sc->cdev` e aguarde o fechamento de todos os descritores de arquivo abertos.
2. Pare e drene todos os callouts de nível do driver (heartbeat, watchdog, tick_source).
3. Desative e drene os callouts da simulação (sensor, command, busy).
4. Libere o estado da simulação.
5. Remova a camada de hardware (libere `regs_buf`, libere `hw`).
6. Destrua os primitivos de sincronização do driver (mutex, sx, cv, sema).

O passo 3 é novo no Capítulo 17. A simulação é parada antes de a camada de hardware ser desmontada, porque os callouts da simulação acessam o bloco de registradores por meio dos acessores da camada de hardware. Liberar o bloco de registradores enquanto um callout ainda está em execução produziria um use-after-free.

A ordem entre o passo 3 e o passo 2 importa. Os callouts de nível do driver (heartbeat) podem ler registradores, portanto precisam ser drenados antes dos callouts da simulação. Uma ordem deliberada: callouts do driver primeiro (eles são os consumidores mais externos), depois os callouts da simulação (os mais internos), e por último os próprios registradores.

### O Incremento de Versão

Em `myfirst.c`:

```c
#define MYFIRST_VERSION "1.0-simulated"
```

A versão vai para `1.0` porque o Capítulo 17 marca um marco real: o driver agora se comporta, de ponta a ponta, como um driver contra um dispositivo funcional. O `0.9-mmio` do Capítulo 16 era um capítulo de acesso a registradores; o `1.0-simulated` do Capítulo 17 é um capítulo de driver totalmente funcional. O salto de 0.9 para 1.0 reflete isso.

O comentário no topo do arquivo é atualizado:

```c
/*
 * myfirst: a beginner-friendly device driver tutorial vehicle.
 *
 * Version 1.0-simulated (Chapter 17): adds dynamic simulation of the
 * Chapter 16 register block.  Includes autonomous sensor updates,
 * command-triggered delayed events, read-to-clear semantics, and a
 * fault-injection framework.  Simulation code lives in myfirst_sim.c;
 * the driver sees it only through register accesses.
 *
 * ... (previous version notes preserved) ...
 */
```

O comentário do Capítulo 17 tem duas frases. Ele aponta para o novo arquivo e nomeia as novas capacidades. Um colaborador futuro que o lê tem contexto suficiente para entender o que a versão representa.

### A Verificação de Regressão

O Capítulo 15 estabeleceu a disciplina de regressão: a cada incremento de versão, execute o conjunto completo de testes de stress de todos os capítulos anteriores, confirme que o `WITNESS` está silencioso, confirme que o `INVARIANTS` está silencioso e confirme que o `kldunload` conclui de forma limpa.

Para o Estágio 5 isso significa:

- Os testes de concorrência do Capítulo 11 (múltiplos escritores, múltiplos leitores) passam.
- Os testes de bloqueio do Capítulo 12 (leitor aguarda dados, escritor aguarda espaço) passam.
- Os testes de callout do Capítulo 13 passam.
- Os testes de taskqueue do Capítulo 14 passam.
- Os testes de coordenação do Capítulo 15 passam.
- Os testes de acesso a registradores do Capítulo 16 passam.
- Os testes de simulação do Capítulo 17 (injeção de falhas, temporização, comportamento) passam.
- `kldunload myfirst` retorna de forma limpa após o conjunto completo.

Nenhum teste é ignorado. Uma regressão em qualquer teste de capítulo anterior é um bug, não uma questão adiada. A disciplina é a mesma que tem sido adotada ao longo de toda a Parte 3.

As adições de testes do Capítulo 17 incluem:

- `sim_sensor_oscillates.sh`: confirma que `SENSOR` muda ao longo do tempo.
- `sim_command_cycle.sh`: executa uma série de comandos de escrita e verifica que `OP_COUNTER` é incrementado.
- `sim_timeout_fault.sh`: habilita a falha de timeout e verifica que o driver se recupera.
- `sim_error_fault.sh`: habilita a falha de erro e verifica que o driver reporta `EIO`.
- `sim_stuck_busy_fault.sh`: habilita a falha de ocupado travado e verifica que o driver expira ao aguardar a sinalização de pronto.
- `sim_mixed_faults_under_load.sh`: 10% de probabilidade de falha, 8 workers paralelos, 30 segundos.

Cada script tem algumas dezenas de linhas. Juntos, acrescentam cerca de 300 linhas de infraestrutura de testes, o que é pequeno em comparação com o driver em si, mas significativo na confiança que proporciona.

### Executando o Estágio Final

```text
# cd examples/part-04/ch17-simulating-hardware/stage5-final
# make clean && make
# kldstat | grep myfirst
# kldload ./myfirst.ko
# kldstat -v | grep -i myfirst

myfirst: version 1.0-simulated

# dmesg | tail -5
# sysctl dev.myfirst.0 | head -40
```

A saída de `kldstat -v` mostra `myfirst` na versão `1.0-simulated`. O `dmesg` exibe o probe e o attach do dispositivo sem erros. A saída do `sysctl` lista todos os sysctls dos Capítulos 11 a 17, incluindo os controles de simulação.

Execute a suíte de stress:

```text
# ../labs/full_regression.sh
```

Se todos os testes passarem, o Capítulo 17 está completo.

### Uma Pequena Regra para o Refactor do Capítulo 17

O refactor do Capítulo 16 tratava de separar a "lógica de negócio do driver" da "mecânica dos registradores de hardware". O refactor do Capítulo 17 trata de separar a "simulação" do "acesso ao hardware". A regra se generaliza: quando um subsistema adquire uma nova responsabilidade, dê a ele seu próprio arquivo; quando um arquivo adquire múltiplas responsabilidades, divida-o.

Uma regra prática: um arquivo com mais de cerca de 800 a 1000 linhas costuma ter mais de uma responsabilidade. Um header que exporta mais de cerca de dez funções geralmente merece uma divisão. Um arquivo que importa um header de um subsistema não relacionado ao seu propósito principal frequentemente sinaliza um vazamento de responsabilidade.

O `myfirst_sim.c` do Capítulo 17 tem cerca de 300 linhas no Estágio 5. O `myfirst_hw.c` tem cerca de 400 linhas. O `myfirst.c` tem cerca de 800 linhas. Cada arquivo contém uma responsabilidade. Nenhum arquivo cresceu fora de controle. A divisão escala: o Capítulo 18 adicionará `myfirst_pci.c` (cerca de 200 a 300 linhas), o Capítulo 19 adicionará `myfirst_intr.c`, o Capítulo 20 adicionará `myfirst_dma.c`. Cada subsistema vive em seu próprio arquivo. O `myfirst.c` principal permanece aproximadamente constante em tamanho; os subsistemas crescem ao redor dele.

### O que o Estágio 5 Realizou

O driver está agora na versão `1.0-simulated`. Comparado ao `0.9-mmio`, ele possui:

- Um backend de simulação em `myfirst_sim.c` e `myfirst_sim.h`.
- Um bloco de registradores dinâmico com atualizações autônomas de sensor, eventos atrasados disparados por comandos e injeção de falhas.
- Timeouts configuráveis e contadores por tipo de erro.
- Um documento `SIMULATION.md` descrevendo a interface e os limites da simulação.
- Um `HARDWARE.md` atualizado refletindo os novos registradores e o comportamento dinâmico.
- Um `LOCKING.md` atualizado refletindo a nova ordem de detach.
- Testes de regressão que exercitam todos os comportamentos do Capítulo 17.

O código do driver é reconhecivelmente FreeBSD. O layout é o layout que drivers reais usam quando possuem responsabilidades distintas de simulação, hardware e driver. O vocabulário é o vocabulário que drivers reais compartilham. Um colaborador que abre o driver pela primeira vez encontra uma estrutura familiar, lê a documentação e consegue navegar pelo código por subsistema.

### Drivers FreeBSD Reais que Usam os Mesmos Padrões

Os padrões que este capítulo exercita não estão confinados a hardware simulado. Três lugares na árvore do FreeBSD merecem ser abertos junto ao Capítulo 17, porque cada um usa uma forma semelhante em um subsistema real, e lê-los transforma as técnicas de "como escrevi a simulação" em "como o kernel realmente opera".

O primeiro é o subsistema **`watchdog(4)`** em `/usr/src/sys/dev/watchdog/watchdog.c`. A rotina central `wdog_kern_pat()` no início desse arquivo é uma pequena máquina de estados dirigida por um "pat" periódico vindo do espaço do usuário ou de outro subsistema do kernel; se o pat não chegar dentro do timeout configurado, o subsistema dispara um handler de pré-timeout e, por fim, um reset do sistema. O paralelo com a simulação do Capítulo 17 é direto: um valor de timeout em ticks, um callout que avança o estado em segundo plano, uma interface via ioctl (`WDIOC_SETTIMEOUT`) que altera o intervalo a partir do espaço do usuário, e uma interface via sysctl que expõe o último timeout configurado para observação. O arquivo também é curto o suficiente para ser lido do início ao fim, o que é raro em um subsistema de produção.

O segundo é o **`random_harvestq`**, o caminho de coleta de entropia, em `/usr/src/sys/dev/random/random_harvestq.c`. A função `random_harvestq_fast_process_event()` e a disciplina de fila ao seu redor são a versão do kernel do padrão "aceitar eventos de muitas fontes e processá-los em segundo plano" que este capítulo exercitou com um sensor simulado. A fila de harvest usa um buffer circular, uma thread de trabalho e backpressure explícito quando os consumidores ficam para trás, sendo um dos exemplos mais limpos na árvore de um subsistema semelhante a um driver que nunca deve bloquear os caminhos de código que o alimentam. Ler esse arquivo após o Capítulo 17 mostra como o padrão de atualização autônoma fica quando o "sensor" é toda fonte de entropia do sistema de uma vez.

O terceiro, que merece uma menção breve, é o **dispositivo pseudo-aleatório** em `/usr/src/sys/dev/random/randomdev.c`. Ele usa uma interface cdev, sysctls configuráveis e uma separação cuidadosa entre o lado de harvest e o lado de saída. Essa separação é a mesma divisão que o Capítulo 17 introduziu entre `myfirst_sim.c` e `myfirst.c`, e observar como `randomdev.c` organiza seus arquivos é um segundo exemplo útil da disciplina que esta seção acabou de aplicar ao driver simulado.

Nenhum desses é um subsistema "de brinquedo". São código de kernel em produção que foi entregue por anos. O objetivo das referências não é pedir que você os domine agora, mas marcar onde as técnicas que você acabou de praticar em simulação vivem no código de produção, para que, quando você abrir `/usr/src/sys` mais tarde e um arquivo parecer familiar, você já tenha visto o padrão.

### Encerrando a Seção 8

O refactor é, novamente, pequeno em código mas significativo em organização. Uma nova divisão de arquivo, um novo arquivo de documentação, atualizações em dois arquivos de documentação existentes, um incremento de versão e uma passagem de regressão. Cada etapa é barata; juntas, transformam um driver funcional em um driver manutenível.

O driver do Capítulo 17 está pronto. O capítulo encerra com laboratórios, desafios, solução de problemas e uma ponte para o Capítulo 18, onde o bloco de registradores simulado é substituído por um BAR PCI real.



## Laboratórios Práticos

Os laboratórios do Capítulo 17 focam em exercitar a simulação de múltiplos ângulos: observar o comportamento autônomo, executar comandos sob carga, injetar falhas e observar as reações do driver. Cada laboratório leva de 20 a 60 minutos.

### Laboratório 1: Observe o Sensor Respirar

Carregue o driver e observe o valor do sensor mudar sem nenhuma atividade do driver.

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.sim_running
dev.myfirst.0.sim_running: 1

# while true; do
    sysctl dev.myfirst.0.reg_sensor
    sleep 0.5
  done
```

Você deve ver o valor oscilando. A linha de base padrão é `0x1000 = 4096`, a amplitude é 64, então o valor varia de cerca de 4096 a cerca de 4160 e volta, ao longo de vários segundos.

Altere a configuração para acelerar a oscilação:

```text
# sysctl dev.myfirst.0.reg_sensor_config_set=0x01000020
```

Isso define o intervalo para `0x0100 = 256` ms e a amplitude para `0x0020 = 32`. O sensor é atualizado a cada 256 ms com um intervalo menor. Observe a saída mudar.

Altere novamente para desacelerá-lo drasticamente:

```text
# sysctl dev.myfirst.0.reg_sensor_config_set=0x03e80100
```

Isso define o intervalo para 1000 ms e a amplitude para 256. O sensor muda uma vez por segundo, com um intervalo maior.

Pare a simulação:

```text
# sysctl dev.myfirst.0.sim_running=0   # (requires a writeable sysctl, added in the lab examples)
```

O valor do sensor congela. Reative:

```text
# sysctl dev.myfirst.0.sim_running=1
```

Ele recomeça. Este laboratório exercita o caminho de atualização autônoma de ponta a ponta.

### Laboratório 2: Execute um Único Comando

Emita um comando manualmente e observe cada mudança de registrador.

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sysctl dev.myfirst.0.reg_delay_ms_set=200

# echo -n "A" > /dev/myfirst0
# sysctl dev.myfirst.0.access_log | head -20
# sysctl dev.myfirst.0.access_log_enabled=0
```

O log deve mostrar:

1. Leituras de `STATUS` (polling aguardando BUSY limpar no início do comando).
2. Uma escrita em `DATA_IN` (o byte 'A' = 0x41).
3. Uma leitura de `CTRL` seguida de uma escrita em `CTRL` (configurando o bit GO).
4. Uma escrita em `CTRL` (limpando o bit GO; a lógica de auto-limpeza).
5. Leituras de `STATUS` (polling aguardando DATA_AV).
6. Uma transição eventual onde `DATA_AV` é definido (pelo callout da simulação).
7. Uma leitura de `DATA_OUT` (retornando 0x41).
8. Uma escrita em `STATUS` (limpando DATA_AV).

Tente associar cada entrada ao código-fonte em `myfirst_sim.c` e `myfirst_write_cmd`. Se alguma entrada não fizer sentido, você tem uma lacuna a preencher.

### Laboratório 3: Estresse o Caminho de Comandos

Execute muitos comandos concorrentes e verifique a correção.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=20
# sysctl dev.myfirst.0.cmd_successes     # note value

# for i in 1 2 3 4 5 6 7 8; do
    (for j in $(seq 1 50); do
        echo -n "X" > /dev/myfirst0
     done) &
  done
# wait

# sysctl dev.myfirst.0.cmd_successes     # should have grown by 400
# sysctl dev.myfirst.0.cmd_errors        # should still be 0
# sysctl dev.myfirst.0.reg_op_counter    # should match the growth in cmd_successes
```

Oito escritores, 50 comandos cada, 400 no total. Com 20 ms por comando, o teste roda em cerca de 8 segundos (serializado no lock do driver). `cmd_successes` deve crescer 400. `cmd_errors` deve permanecer em zero (nenhuma falha está habilitada). `reg_op_counter` deve corresponder ao incremento em `cmd_successes`.

### Laboratório 4: Injete uma Falha de Timeout

Habilite a falha de timeout e observe a recuperação do driver.

```text
# sysctl dev.myfirst.0.cmd_data_timeouts          # note value
# sysctl dev.myfirst.0.cmd_recoveries              # note value

# sysctl dev.myfirst.0.reg_fault_mask_set=0x1     # TIMEOUT bit
# sysctl dev.myfirst.0.reg_fault_prob_set=10000   # 100%

# echo -n "X" > /dev/myfirst0
write: Operation timed out

# sysctl dev.myfirst.0.cmd_data_timeouts          # should have grown by 1
# sysctl dev.myfirst.0.cmd_recoveries              # should have grown by 1

# sysctl dev.myfirst.0.reg_status                 # should be 1 (READY, BUSY cleared by recovery)

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

A primeira escrita expira; o driver se recupera; uma escrita subsequente tem sucesso. Os contadores confirmam a sequência.

### Laboratório 5: Injete uma Falha de Erro

Execute um lote pequeno com uma falha de erro com 25% de probabilidade.

```text
# sysctl dev.myfirst.0.cmd_errors                 # note value

# sysctl dev.myfirst.0.reg_fault_mask_set=0x4     # ERROR bit
# sysctl dev.myfirst.0.reg_fault_prob_set=2500    # 25%

# for i in $(seq 1 40); do
    echo -n "X" > /dev/myfirst0 || echo "error on iteration $i"
  done

# sysctl dev.myfirst.0.cmd_errors                 # should have grown by ~10

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
```

Cerca de 10 das 40 iterações devem reportar um erro. O contador de erros do driver deve refletir a contagem exatamente. O driver deve permanecer utilizável após o teste (comandos subsequentes têm sucesso).

### Laboratório 6: Injete Stuck-Busy e Observe o Driver Aguardar

Habilite a falha stuck-busy, tente um comando, observe o timeout, limpe a falha e confirme a recuperação.

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x8     # STUCK_BUSY bit
# sleep 0.1                                         # let the busy callout assert

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3                         # READY|BUSY

# sysctl dev.myfirst.0.cmd_rdy_timeouts            # note value

# echo -n "X" > /dev/myfirst0
write: Operation timed out                           # after sc->rdy_timeout_ms

# sysctl dev.myfirst.0.cmd_rdy_timeouts            # should have grown by 1

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sleep 0.2                                          # let the busy callout stop

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1                         # READY only

# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

A falha trava `BUSY`; o driver não consegue emitir comandos; o driver expira em `wait_for_ready`. Limpar a falha e aguardar um breve momento permite que `BUSY` seja limpo (via o caminho de comando, se algum comando passou, ou via o estado natural sem nenhum comando pendente). O driver se recupera.

### Laboratório 7: Falhas Mistas Sob Carga

Habilite múltiplas falhas com baixas probabilidades, execute um teste de stress longo e analise os resultados.

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x5     # TIMEOUT | ERROR
# sysctl dev.myfirst.0.reg_fault_prob_set=200     # 2%
# sysctl dev.myfirst.0.reg_delay_ms_set=10
# sysctl dev.myfirst.0.cmd_timeout_ms=50

# # Record starting counters.
# sysctl dev.myfirst.0 | grep -E 'cmd_|fault_' > /tmp/before.txt

# # 8 workers, 100 commands each.
# for i in 1 2 3 4 5 6 7 8; do
    (for j in $(seq 1 100); do
        echo -n "X" > /dev/myfirst0 2>/dev/null
    done) &
  done
# wait

# # Record ending counters.
# sysctl dev.myfirst.0 | grep -E 'cmd_|fault_' > /tmp/after.txt
# diff /tmp/before.txt /tmp/after.txt

# # Clean up.
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# sysctl dev.myfirst.0.reg_delay_ms_set=500
# sysctl dev.myfirst.0.cmd_timeout_ms=2000
```

O diff deve mostrar `cmd_successes` crescendo cerca de 784 (800 comandos menos cerca de 16 falhas). `fault_injected` deve crescer cerca de 16. `cmd_data_timeouts` e `cmd_errors` devem crescer cada um cerca de 8. Os números exatos variam a cada execução (por causa do `arc4random`), mas as proporções são estáveis.

Verifique também: `vmstat -m | grep myfirst` não deve mostrar crescimento no uso de memória entre antes e depois; o teste não deveria ter vazado memória.

### Laboratório 8: Observe as Atualizações do Sensor Durante Carga Pesada de Comandos

O callout do sensor e o callout de comando compartilham o mesmo mutex. Um teste de carga de longa duração pode privar o sensor de atualizações se o lock for mantido constantemente. Verifique isso.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=5          # very fast commands
# # Start a long load test in the background.
# (for i in $(seq 1 5000); do
    echo -n "X" > /dev/myfirst0 2>/dev/null
  done) &

# # Meanwhile, poll the sensor.
# while kill -0 $! 2>/dev/null; do
    sysctl dev.myfirst.0.reg_sensor
    sleep 0.2
  done
```

Cada valor de sensor lido deve ser diferente do anterior (o callout do sensor está rodando). Se os valores congelarem durante a carga (todos idênticos por muitas leituras), o lock está sendo mantido por tempo demais e o sensor está morrendo de fome. Um driver correto vê valores de sensor sendo atualizados suavemente mesmo sob carga pesada de comandos, porque o caminho de comando libera o lock durante o `pause_sbt`.

### Laboratório 9: Construa e Execute o Módulo hwsim2

Uma versão autônoma da simulação do Capítulo 17, `hwsim2`, está disponível nas fontes complementares. Construa e carregue-a.

```text
# cd examples/part-04/ch17-simulating-hardware/hwsim2-standalone
# make clean && make
# kldload ./hwsim2.ko

# # The hwsim2 module exposes a single register block with sensor
# # updates and command cycle; no cdev, just sysctls.
# sysctl dev.hwsim2.sensor
# sleep 1
# sysctl dev.hwsim2.sensor                         # different value
# sysctl dev.hwsim2.do_command=0x12345678
# sleep 0.6
# sysctl dev.hwsim2.result                         # 0x12345678

# kldunload hwsim2
```

O módulo `hwsim2` tem cerca de 150 linhas de C. Lê-lo em uma única sessão é uma consolidação útil do material do Capítulo 17.

### Laboratório 10: Injete um Ataque de Corrupção de Memória (Kernel de Debug)

Um exercício deliberado de quebrar e observar. Modifique o callback do sensor da simulação para escrever um byte além do final do bloco de registradores. Isso deve disparar o `KASSERT` em um kernel de debug.

Em `myfirst_sim.c`, adicione temporariamente em `myfirst_sim_sensor_cb`:

```c
/* DELIBERATE BUG for Lab 10. Remove after testing. */
CSR_WRITE_4(sc, 0x80, 0xdead);   /* 0x80 is past the 64-byte region */
```

Reconstrua e carregue. O kernel deve entrar em panic dentro de 100 ms (o callback do sensor dispara, o `KASSERT` detecta o acesso fora dos limites, a string de panic nomeia o offset `0x80`).

Remova o bug. Reconstrua. Verifique que o driver roda sem erros novamente.

Este laboratório mostra o valor das asserções de limites nos acessores de hardware: um acesso fora dos limites dispara imediatamente em vez de corromper silenciosamente memória não relacionada. O código de produção nunca deve remover essas asserções.



## Exercícios Desafio

Os desafios estendem o material do Capítulo 17 com trabalho de profundidade opcional. Cada um leva de uma a quatro horas e exercita julgamento, não apenas digitação.

### Desafio 1: `INTR_STATUS` com Limpeza na Leitura

O `INTR_STATUS` do Capítulo 17 ainda é RW, não RC (read-to-clear). Implemente a semântica RC: quando o driver lê `INTR_STATUS`, retorne o valor atual e limpe-o em seguida. Adicione um hook no caminho do acessor que reconheça registradores RC e os trate de forma especial. Atualize o `HARDWARE.md`.

Pense em: como o driver garante que a leitura acontece sob o lock? O que acontece se um sysctl lê `INTR_STATUS` (limpando inadvertidamente um estado de que o driver precisava)?

### Desafio 2: Adicione uma Fila de Comandos

A simulação atual rejeita comandos sobrepostos. Dispositivos reais frequentemente os colocam em queue. Implemente uma pequena command queue: se um comando chegar enquanto outro estiver pendente, adicione-o à queue; quando o comando atual for concluído, inicie o próximo. Limite a queue a 4 entradas.

Pense em: como o driver aguarda comandos em queue? O driver deve ter sua própria visão da queue, ou confiar na visão da simulação?

### Desafio 3: Simule uma Deriva na Taxa de Amostragem

O callout do sensor dispara em um intervalo fixo. Sensores reais frequentemente derivam: o intervalo muda ligeiramente ao longo do tempo por causa de variações de temperatura ou tensão. Modifique o callback do sensor para adicionar uma pequena perturbação aleatória a cada intervalo (por exemplo, +/- 10%). Observe como isso afeta o driver.

Pense em: a deriva importa para um driver que apenas lê o valor do sensor? Importa para um driver que se preocupa com o tempo exato de cada amostra? (O primeiro driver não se preocupa; um driver posterior pode se preocupar.)

### Desafio 4: Um `INTR_STATUS` Write-One-to-Clear

A semântica W1C é uma variante comum do RC. Implemente W1C para `INTR_STATUS`: escrever 1 em um bit limpa esse bit; escrever 0 não tem efeito; leituras retornam o valor atual sem efeitos colaterais. Compare com a implementação RC do Desafio 1.

Pense em: qual é mais conveniente para o driver (limpeza por leitura ou limpeza por escrita)? Qual é mais defensivo contra leituras de debug acidentais?

### Desafio 5: Recuperação de Erros Sem Reset

O caminho de recuperação do driver do Capítulo 17 limpa o estado específico da simulação diretamente. Hardware real não consegue fazer isso; o driver deve usar apenas operações em nível de registrador. Reescreva `myfirst_recover_from_stuck` para usar apenas escritas em registrador (por exemplo, um bit `CTRL.RESET`). Ajuste a simulação para respeitar `CTRL.RESET`.

Pense em: como o driver aguarda a conclusão do reset? O reset afeta outros registradores (por exemplo, limpa o fault mask)?

### Desafio 6: Um Depurador em Espaço do Usuário

Escreva um pequeno programa em espaço do usuário, `myfirstctl`, que abre `/dev/myfirst0`, emite uma série de ioctls para controlar a simulação e exibe o estado dos registradores. Torne-o interativo: o usuário pode digitar comandos como `status`, `go X`, `delay 100`, `fault timeout 50%`.

Pense em: como o programa em espaço do usuário se comunica com o driver (ioctl, sysctl, read/write)? Qual é uma sintaxe de comandos razoável?

### Desafio 7: Injeção de Falhas Orientada por DTrace

O DTrace pode fornecer sondas dinâmicas. Adicione uma sonda DTrace no início de `myfirst_sim_start_command` que recebe um ponteiro para `sc` como argumento. Escreva um script D que, com base em alguma condição especificada pelo usuário (por exemplo, "a cada 100 chamadas"), defina `sc->sim->pending_fault = MYFIRST_FAULT_ERROR`. O script injeta falhas a partir do espaço do usuário sem o framework de injeção de falhas do kernel.

Pense em: o DTrace pode modificar com segurança o estado do kernel dessa forma? Quais são os limites do que o DTrace pode fazer?

### Desafio 8: Simule Dois Dispositivos

O driver atual tem uma única instância de dispositivo. Modifique-o para que dois arquivos `/dev/myfirstN` existam, cada um com sua própria simulação. Verifique que as operações em um dispositivo não afetam o outro.

Pense em: o que muda no attach? Como funciona a árvore sysctl por dispositivo? A simulação precisa de estado aleatório por dispositivo?



## Referência de Troubleshooting

Uma referência rápida para os problemas que a simulação do Capítulo 17 tem mais probabilidade de revelar.

### O sensor não muda

- `sim_running` é 0. Alterne-o com o sysctl gravável.
- O campo de intervalo de `SENSOR_CONFIG` é zero. Defina-o com um valor positivo.
- O callout do sensor não está agendado. Confirme que `myfirst_sim_enable` foi chamado. Verifique o `dmesg` em busca de mensagens de erro.
- `sc->sim` é NULL. Confirme que `myfirst_sim_attach` foi executado com sucesso.

### Comandos sempre esgotam o tempo

- `DELAY_MS` está definido com valor muito alto. Reduza-o ou aumente o timeout do comando.
- O bit `FAULT_TIMEOUT` está definido em `FAULT_MASK`. Limpe-o.
- O bit `FAULT_STUCK_BUSY` está definido. Limpe-o e aguarde aproximadamente 100 ms até que o callout de ocupado pare de se reassertar.
- A simulação está desabilitada (`sim_running=0`). Habilite-a.
- Um comando anterior travou e não foi recuperado. Verifique `command_pending` (através do sysctl da simulação) e reinicie manualmente se necessário.

### `WITNESS` avisa sobre ordem de lock

- O caminho de detach está adquirindo locks na ordem errada. Compare o stack trace com `LOCKING.md`.
- Um novo caminho de código está adquirindo `sc->mtx` após um sleep lock. A ordenação estabelecida no Capítulo 15 deve ser preservada.

### Kernel panic no `kldunload`

- Os callouts da simulação não foram drenados antes do `free`. Verifique que `callout_drain` é executado antes de `free(sim)`.
- O estado da simulação foi liberado enquanto um callout estava em andamento. A drenagem é a solução.
- A camada de hardware foi liberada antes da camada de simulação. A ordenação do Capítulo 17 (detach da simulação, depois detach do hardware) deve ser seguida.

### O uso de memória cresce ao longo do tempo

- `vmstat -m | grep myfirst` mostra que o tamanho de alocação do driver está aumentando. Isso é um vazamento. Verifique os caminhos de erro de injeção de falhas; eles frequentemente se esquecem de liberar algo.
- O log de acesso é alocado dinamicamente em algumas configurações. Confirme que ele é liberado no detach.

### O log de acesso mostra entradas inesperadas

- Um callout está sendo executado em sua própria cadência; todos os callouts do Capítulo 17 são visíveis no log. Se o log mostrar callouts disparando mais rápido que o esperado, verifique o intervalo.
- Uma ferramenta em espaço do usuário (`watch -n 0.1 sysctl ...`) está fazendo polling muito rapidamente. Cada acesso sysctl passa pelos accessors.

### A probabilidade de injeção de falhas não corresponde às observações

- `FAULT_PROB` é dimensionado de 0 a 10000. Um valor de 50 é 0,5%, não 50%. Para 50%, use 5000.
- A falha só se aplica a operações que acionam `myfirst_sim_should_fault`. Nem toda operação é candidata; atualizações do sensor, por exemplo, não passam por esse caminho.

### Alternâncias de `sim_running` ignoradas

- O handler do sysctl pode não estar chamando `myfirst_sim_enable` ou `myfirst_sim_disable` com o lock adquirido. Confirme que o handler adquire `sc->mtx` antes de delegar.

### Comandos têm sucesso, mas demoram muito mais que o esperado

- `cmd_timeout_ms` está definido com valor muito alto; qualquer comando que trave torna o orçamento de timeout enorme.
- O fault mask está definido com `STUCK_BUSY` e um comando bem-sucedido inesperadamente se torna uma paralisação de `wait_for_ready` até a próxima limpeza de falha.

### Testes de carga são mais lentos que o esperado

- Os comandos são serializados pelo mutex do driver. N escritores com M comandos cada levam N\*M \* DELAY_MS no total.
- Reduzir `DELAY_MS` acelera a simulação; reduzir `cmd_timeout_ms` não acelera nada, a menos que timeouts estejam realmente ocorrendo.



## Encerrando

O Capítulo 17 começou com um bloco de registradores estático vindo do Capítulo 16 e termina com um driver que se comporta, de ponta a ponta, como um driver conversando com um dispositivo funcional. A simulação é dinâmica: um registrador de sensor se atualiza por conta própria, comandos agendam conclusões com atraso, bits de `STATUS` mudam ao longo do tempo e a injeção de falhas produz falhas controladas que exercitam os caminhos de erro do driver. O driver lida com tudo isso por meio do vocabulário de registradores que o Capítulo 16 ensinou, com a disciplina de sincronização que a Parte 3 construiu e com as primitivas de temporização que a Seção 6 apresentou.

O que o Capítulo 17 deliberadamente não fez: PCI real (Capítulo 18), interrupções reais (Capítulo 19), DMA real (Capítulos 20 e 21). Cada um desses tópicos merece seu próprio capítulo; cada um estenderá o driver de maneiras específicas enquanto reutiliza o framework de simulação onde for apropriado.

A versão é `1.0-simulated`. O layout de arquivos cresceu: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`. A documentação cresceu: `LOCKING.md`, `HARDWARE.md` e o novo `SIMULATION.md`. A suíte de testes agora exercita todos os comportamentos da simulação, todos os modos de falha e todos os caminhos de erro.

### Uma Reflexão Antes do Capítulo 18

Uma pausa antes do próximo capítulo. O Capítulo 17 ensinou a simulação como uma técnica que vai além do aprendizado. Os padrões que você praticou aqui (mudanças de estado orientadas por callout, protocolos de comando-resposta, injeção de falhas, caminhos de recuperação) são padrões que você usará ao longo de toda a sua vida escrevendo drivers. Eles se aplicam tanto ao harness de testes de um driver de armazenamento em produção quanto a este driver didático. A habilidade de simulação é permanente.

O Capítulo 17 também ensinou a disciplina de pensar como hardware. Projetar o mapa de registradores, escolher a semântica de acesso, temporizar as respostas, decidir o que travar e o que limpar: essas são decisões que a equipe de hardware toma para dispositivos reais, e um autor de driver que as compreende lê datasheets de forma diferente. Da próxima vez que você encontrar o datasheet de um dispositivo real, vai notar as decisões que os projetistas tomaram e terá uma estrutura para avaliar se essas decisões foram acertadas.

O Capítulo 18 muda completamente o cenário. A simulação desaparece (temporariamente); um dispositivo PCI real toma seu lugar. O bloco de registradores migra da memória do `malloc(9)` para um PCI BAR. Os accessors mudam de `X86_BUS_SPACE_MEM` para `rman_get_bustag` e `rman_get_bushandle`. O código de alto nível do driver não muda em nada, pois a abstração do Capítulo 16 é portável ao longo dessa mudança. O que muda é o comportamento do dispositivo: um dispositivo virtio real em uma VM tem seu próprio protocolo, sua própria temporização, suas próprias peculiaridades. Os padrões que o Capítulo 17 ensinou são o que permite lidar com tudo isso.

### O Que Fazer Se Você Estiver Travado

Algumas sugestões caso o material do Capítulo 17 pareça denso.

Primeiro, releia a Seção 3. O padrão de callout é a base de todo comportamento dinâmico do capítulo. Se a interação entre `callout_init_mtx`, `callout_reset_sbt`, `callout_stop` e `callout_drain` parecer opaca, tudo o que vem depois também será opaco. O Capítulo 13 é a outra boa referência.

Segundo, execute o Laboratório 1 e o Laboratório 2 manualmente, um passo de cada vez. Observar o sensor respirar e rastrear um único comando pelo log de acesso é o momento em que o comportamento da simulação se torna concreto.

Terceiro, pule os desafios na primeira leitura. Os laboratórios são calibrados para o Capítulo 17; os desafios pressupõem que o material do capítulo já está sólido. Volte a eles depois do Capítulo 18 se parecerem inacessíveis agora.

Quarto, abra `myfirst_sim.c` e leia os três callouts (`sensor`, `command`, `busy`) em ordem. Cada um é um trecho de código autocontido que ilustra um aspecto do design da simulação. Se você conseguir explicar cada um para um colega (ou para um patinho de borracha), você internalizou o núcleo do Capítulo 17.

O objetivo do Capítulo 17 era dar vida ao bloco de registradores. Se isso aconteceu, o restante da Parte 4 parecerá uma progressão natural: o Capítulo 18 troca a simulação por hardware real, o Capítulo 19 adiciona interrupções reais, os Capítulos 20 e 21 adicionam DMA. Cada capítulo se baseia no que o Capítulo 17 estabeleceu.



## Ponte para o Capítulo 18

O Capítulo 18 tem o título *Escrevendo um Driver PCI*. Seu escopo é o caminho de hardware real que o Capítulo 17 deliberadamente não tomou: um driver que sonda um barramento PCI, corresponde a um dispositivo real pelo ID de fornecedor e ID de dispositivo, reivindica o BAR do dispositivo como um recurso de memória, mapeia-o por meio de `bus_alloc_resource_any` e se comunica com a região mapeada pelos mesmos macros `CSR_*` que os drivers dos Capítulos 16 e 17 já utilizam.

O Capítulo 17 preparou o terreno de quatro maneiras específicas.

Primeiro, **você tem um driver completo**. O driver do Capítulo 17 na versão `1.0-simulated` exercita todos os padrões de protocolo comuns: comando-resposta, conclusão com atraso, polling de status, recuperação de erros e tratamento de timeout. O Capítulo 18 substituirá o backend de registradores sem alterar a lógica de alto nível do driver. A substituição é uma mudança de uma única função em `myfirst_hw_attach`; todo o resto permanece intacto.

Segundo, **você tem um modelo de falhas**. O framework de injeção de falhas do Capítulo 17 ensinou o driver a lidar com erros. Dispositivos PCI reais produzem erros reais: timeouts de barramento, erros ECC não corrigíveis, transições de estado de energia, quedas de link. A disciplina de lidar com erros simulados é o que permitirá ao driver do Capítulo 18 lidar com os reais.

Terceiro, **você tem um modelo de temporização**. O Capítulo 17 ensinou quando usar `DELAY(9)`, `pause_sbt(9)` e `callout_reset_sbt(9)`. Dispositivos PCI reais têm seus próprios requisitos de temporização, frequentemente documentados em seus datasheets. A disciplina que o Capítulo 17 construiu é o que permitirá ao driver respeitar essas temporizações no Capítulo 18.

Quarto, **você tem o hábito de documentar**. `HARDWARE.md`, `LOCKING.md` e `SIMULATION.md` são três documentos vivos que os contribuidores do driver mantêm. O Capítulo 18 adicionará um quarto: `PCI.md` ou um documento similar que descreva o dispositivo PCI específico que o driver atende, seu vendor ID e device ID, o layout dos BARs e quaisquer particularidades. O hábito de documentar está consolidado; o Capítulo 18 o expande.

Tópicos específicos que o Capítulo 18 abordará:

- O subsistema PCI no FreeBSD: `pci(4)`, a tupla bus-device-function, `pciconf -lv`.
- O ciclo de vida de probe e attach: `DRIVER_MODULE`, correspondência por vendor ID e device ID, os métodos `probe` e `attach`.
- Alocação de recursos: `bus_alloc_resource_any`, a especificação de recurso, `SYS_RES_MEMORY`, `RF_ACTIVE`.
- Mapeamento de BAR: como o BAR PCI se torna uma região `bus_space`; `rman_get_bustag` e `rman_get_bushandle`.
- Habilitação de bus mastering: `pci_enable_busmaster` e quando é necessário.
- Inicialização no momento do attach e limpeza no detach.
- Testes com um dispositivo virtio em uma VM: o guest FreeBSD no `qemu` ou `bhyve`, como passar um dispositivo sintético, como verificar que o driver realiza o attach e funciona corretamente.

Não é necessário ler adiante. O Capítulo 17 é preparação suficiente. Traga o driver `myfirst` na versão `1.0-simulated`, o seu `LOCKING.md`, o seu `HARDWARE.md`, o seu `SIMULATION.md`, o kernel com `WITNESS` habilitado e o seu kit de testes. O Capítulo 18 começa onde o Capítulo 17 terminou.

Uma breve reflexão de encerramento. A Parte 3 ensinou o vocabulário de sincronização e produziu um driver que se coordenava internamente. O Capítulo 16 deu ao driver um vocabulário de registradores. O Capítulo 17 deu ao bloco de registradores um comportamento dinâmico e ao driver um modelo de falhas. O Capítulo 18 aposentará a simulação e conectará o driver ao silício real. Cada etapa construiu sobre a anterior; o driver que entra no Capítulo 18 é um driver quase pronto para produção, precisando apenas da cola de barramento real que o Capítulo 18 fornece.

A conversa com o hardware está se aprofundando. O vocabulário é seu; o protocolo é seu; a disciplina é sua. O Capítulo 18 adiciona a última peça que falta.

## Referência: Guia Rápido da Simulação do Capítulo 17

Um resumo de uma página da API de simulação do Capítulo 17 e dos sysctls que ela expõe, para consulta rápida durante a codificação.

### API de Simulação (em `myfirst_sim.h`)

| Função                              | Finalidade                                                           |
|-------------------------------------|----------------------------------------------------------------------|
| `myfirst_sim_attach(sc)`            | Aloca o estado da simulação e registra os callouts (sem iniciá-los). |
| `myfirst_sim_detach(sc)`            | Drena os callouts e libera o estado da simulação.                    |
| `myfirst_sim_enable(sc)`            | Inicia os callouts. Requer `sc->mtx` adquirido.                      |
| `myfirst_sim_disable(sc)`           | Para os callouts (sem drená-los).                                    |
| `myfirst_sim_start_command(sc)`     | Acionado pela escrita em `CTRL.GO`; agenda a conclusão do comando.   |
| `myfirst_sim_add_sysctls(sc)`       | Registra os sysctls específicos da simulação.                        |

### Adições de Registradores

| Offset | Registrador      | Acesso | Finalidade                                                          |
|--------|------------------|--------|---------------------------------------------------------------------|
| 0x28   | `SENSOR`         | RO     | Valor do sensor simulado, atualizado pelo callout.                  |
| 0x2c   | `SENSOR_CONFIG`  | RW     | Intervalo (16 bits inferiores) e amplitude (16 bits superiores).    |
| 0x30   | `DELAY_MS`       | RW     | Atraso no processamento de comandos, em milissegundos.              |
| 0x34   | `FAULT_MASK`     | RW     | Bitmask dos tipos de falha habilitados.                             |
| 0x38   | `FAULT_PROB`     | RW     | Probabilidade de falha, de 0 a 10000 (10000 = 100%).                |
| 0x3c   | `OP_COUNTER`     | RO     | Contagem de comandos processados.                                   |

### Adições ao CTRL

| Bit | Nome                     | Finalidade                                      |
|-----|--------------------------|--------------------------------------------------|
| 9   | `MYFIRST_CTRL_GO`        | Inicia um comando. Limpa-se automaticamente.    |

### Modos de Falha

| Bit | Nome                       | Efeito                                                       |
|-----|----------------------------|--------------------------------------------------------------|
| 0   | `MYFIRST_FAULT_TIMEOUT`    | O comando nunca é concluído.                                 |
| 1   | `MYFIRST_FAULT_READ_1S`    | `DATA_OUT` retorna `0xFFFFFFFF`.                             |
| 2   | `MYFIRST_FAULT_ERROR`      | `STATUS.ERROR` é definido em vez de `DATA_AV`.               |
| 3   | `MYFIRST_FAULT_STUCK_BUSY` | `STATUS.BUSY` permanece ativo continuamente.                 |

### Novos Sysctls

| Sysctl                                      | Tipo | Finalidade                                              |
|---------------------------------------------|------|---------------------------------------------------------|
| `dev.myfirst.0.sim_running`                 | RW   | Habilita/desabilita a simulação.                        |
| `dev.myfirst.0.sim_sensor_baseline`         | RW   | Valor de linha de base do sensor.                       |
| `dev.myfirst.0.sim_op_counter_mirror`       | RO   | Espelho de `OP_COUNTER`.                                |
| `dev.myfirst.0.reg_delay_ms_set`            | RW   | `DELAY_MS` gravável.                                    |
| `dev.myfirst.0.reg_sensor_config_set`       | RW   | `SENSOR_CONFIG` gravável.                               |
| `dev.myfirst.0.reg_fault_mask_set`          | RW   | `FAULT_MASK` gravável.                                  |
| `dev.myfirst.0.reg_fault_prob_set`          | RW   | `FAULT_PROB` gravável.                                  |
| `dev.myfirst.0.cmd_timeout_ms`              | RW   | Timeout de conclusão de comando.                        |
| `dev.myfirst.0.rdy_timeout_ms`              | RW   | Timeout de polling de prontidão do dispositivo.         |
| `dev.myfirst.0.cmd_successes`               | RO   | Comandos bem-sucedidos.                                 |
| `dev.myfirst.0.cmd_rdy_timeouts`            | RO   | Timeouts de espera por prontidão.                       |
| `dev.myfirst.0.cmd_data_timeouts`           | RO   | Timeouts de espera por dados.                           |
| `dev.myfirst.0.cmd_errors`                  | RO   | Comandos que reportaram erro.                           |
| `dev.myfirst.0.cmd_recoveries`              | RO   | Invocações de recuperação.                              |
| `dev.myfirst.0.fault_injected`              | RO   | Total de falhas injetadas.                              |



## Referência: Guia Rápido de Primitivas de Temporização

| Primitiva                  | Custo                         | Cancelável                          | Faixa Adequada                        |
|----------------------------|-------------------------------|-------------------------------------|---------------------------------------|
| `DELAY(us)`                | Busy-wait, consome toda a CPU | Não                                 | < 100 us                              |
| `pause_sbt(..., sbt, ...)` | Suspende, libera a CPU        | Não (não interrompível nessa forma) | 100 us até segundos                   |
| `callout_reset_sbt(...)`   | Callback agendado             | Sim                                 | fire-and-forget, periódico            |
| `cv_timedwait_sbt(...)`    | Suspende em condição          | Sim (via cv_signal)                 | esperas que podem ser encurtadas      |

Contextos em que cada primitiva é permitida:

- `DELAY`: qualquer contexto (incluindo interrupções de filtro e spin mutexes).
- `pause_sbt`: contexto de processo, sem spin locks adquiridos.
- `callout_reset_sbt`: qualquer contexto (o callback executa em contexto de callout).
- `cv_timedwait_sbt`: contexto de processo, com mutex específico adquirido.



## Referência: Anatomia de um Callout de Simulação

Um modelo para adicionar um novo comportamento de simulação, baseado no callout de sensor do Capítulo 17.

### Passo 1: Declare o callout no estado da simulação

```c
struct myfirst_sim {
        /* ... existing fields ... */
        struct callout   my_new_callout;
        int              my_new_interval_ms;
};
```

### Passo 2: Inicialize o callout no attach

```c
/* In myfirst_sim_attach: */
callout_init_mtx(&sim->my_new_callout, &sc->mtx, 0);
sim->my_new_interval_ms = 200;   /* default */
```

### Passo 3: Escreva o callback

```c
static void
myfirst_sim_my_new_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        /* Do the work: update a register, signal a condition, ...  */
        CSR_UPDATE_4(sc, MYFIRST_REG_SENSOR, 0, 0x100);

        /* Re-arm. */
        callout_reset_sbt(&sim->my_new_callout,
            sim->my_new_interval_ms * SBT_1MS, 0,
            myfirst_sim_my_new_cb, sc, 0);
}
```

### Passo 4: Inicie o callout no enable

```c
/* In myfirst_sim_enable: */
callout_reset_sbt(&sim->my_new_callout,
    sim->my_new_interval_ms * SBT_1MS, 0,
    myfirst_sim_my_new_cb, sc, 0);
```

### Passo 5: Pare-o no disable e drene no detach

```c
/* In myfirst_sim_disable: */
callout_stop(&sim->my_new_callout);

/* In myfirst_sim_detach, after releasing the lock: */
callout_drain(&sim->my_new_callout);
```

### Passo 6: Documente em SIMULATION.md

Adicione uma entrada na tabela de mapeamento de callouts:

```text
| Callout           | Interval         | Purpose                       |
|-------------------|------------------|-------------------------------|
| my_new_callout    | my_new_interval_ms | ... explain ...              |
```

São seis passos. Cada um é mecânico. O padrão é o mesmo para todo comportamento de simulação; uma vez que você internalize o modelo, adicionar novos comportamentos se torna rápido e confiável.



## Referência: Inventário de Scripts de Teste

Um breve catálogo dos scripts de teste que acompanham o diretório `examples/part-04/ch17-simulating-hardware/labs/`.

| Script                            | Finalidade                                                                | Tempo Típico de Execução |
|-----------------------------------|---------------------------------------------------------------------------|--------------------------|
| `sim_sensor_oscillates.sh`        | Confirma que o valor de `SENSOR` muda ao longo do tempo.                  | ~10 s                    |
| `sim_command_cycle.sh`            | Executa 50 comandos e verifica se `OP_COUNTER` corresponde.               | ~30 s                    |
| `sim_timeout_fault.sh`            | Habilita a falha de timeout; verifica a recuperação.                      | ~5 s                     |
| `sim_error_fault.sh`              | Habilita a falha de erro; verifica se `cmd_errors` aumenta.               | ~10 s                    |
| `sim_stuck_busy_fault.sh`         | Habilita a falha de stuck-busy; verifica o timeout ao aguardar prontidão. | ~10 s                    |
| `sim_mixed_faults_under_load.sh`  | 2% de falhas aleatórias, 8 workers, 100 comandos cada.                    | ~30 s                    |
| `sim_sensor_during_load.sh`       | Verifica atualizações do sensor sob carga pesada de comandos.             | ~30 s                    |
| `full_regression_ch17.sh`         | Todos os itens acima mais a regressão dos Capítulos 11 a 16.              | ~3 minutos               |

Cada script encerra com status diferente de zero se algo inesperado acontecer. Uma execução bem-sucedida de `full_regression_ch17.sh` significa que todos os comportamentos do Capítulo 17 foram validados e nenhuma regressão foi introduzida.



## Referência: Um Balanço Honesto das Simplificações do Capítulo 17

Um capítulo que ensina uma fatia de um tópico extenso inevitavelmente simplifica. Por honestidade com o leitor, segue um catálogo do que o Capítulo 17 simplificou e como é a história completa.

### A Interação do Callout com o Mutex

O Capítulo 17 usa `callout_init_mtx` para que todo callback de simulação seja executado com `sc->mtx` adquirido. Esse é o padrão mais simples e seguro, mas não é o único. Drivers reais às vezes usam `callout_init` (sem mutex associado) e adquirem o lock dentro do callback; isso oferece controle mais fino sobre o locking, mas é mais propenso a erros. Alguns drivers reais usam `callout_init_rm` (para reader-writer locks) ou até `callout_init_mtx` com um spin mutex para casos em que o callback precisa ser executado em contexto de hardirq.

A história completa: a API de callout do FreeBSD oferece suporte a vários estilos de lock, cada um adequado a um modelo de concorrência diferente. A escolha de `callout_init_mtx` no Capítulo 17 é pedagogicamente clara; drivers de produção escolhem o estilo que corresponde ao seu grafo de locking específico. O arquivo `/usr/src/sys/dev/e1000/if_em.c` usa `callout_init_mtx` para seu callout de tick e `callout_init` para o caminho de polling de DMA protegido por spinlock, ilustrando essa diferença.

### O Framework de Injeção de Falhas

A injeção de falhas do Capítulo 17 é limitada a quatro modos, uma probabilidade e uma verificação simples com `should_fault` no início de cada operação. Frameworks reais de injeção de falhas (como o próprio `fail(9)` do FreeBSD) oferecem suporte a um vocabulário mais rico: injeção específica por ponto de chamada, curvas de probabilidade que variam ao longo do tempo, injeção determinística para a N-ésima chamada e caminhos de injeção que podem ser acionados do espaço do usuário por nome.

A história completa: o framework `fail(9)` em `/usr/src/sys/kern/kern_fail.c` implementa um sistema de injeção de falhas de qualidade para produção. O framework do Capítulo 17 é uma versão simplificada que se concentra nas necessidades específicas da simulação. Um exercício desafio ao final do capítulo convida o leitor a substituir o framework ad-hoc do capítulo por um baseado em `fail(9)`.

### A Fonte de Números Aleatórios

O Capítulo 17 usa `arc4random_uniform(10000)` como fonte de decisão para a probabilidade de falha. Isso é não determinístico: duas execuções do mesmo teste produzirão padrões de falha diferentes. Para testes reproduzíveis, o autor de um driver usaria uma fonte determinística (um LCG simples semeado com um valor fixo, ou um contador) e exporia a semente por meio de um sysctl.

A história completa: a injeção de falhas reproduzível é importante para testes de regressão. Um driver que passa por todas as falhas com a semente `0x12345` em um dia e falha com a mesma semente na semana seguinte tem um bug que não existia antes. O não determinismo do Capítulo 17 é aceitável para fins didáticos, mas insuficiente para suítes de regressão sérias.

### A Fila de Comandos

O Capítulo 17 permite apenas um comando em execução por vez. Dispositivos reais frequentemente enfileiram comandos; um driver de rede pode ter milhares de descritores em voo. A restrição de um único comando simplifica a máquina de estados da simulação e o modelo de locking do driver, mas limita o throughput do driver tutorial.

A história completa: um modelo de fila de comandos exige estado por comando (status, tempo de chegada, resultado), uma estrutura de dados de fila com sua própria sincronização e um padrão de driver que lide com coalescência de comandos, conclusão em lote e conclusão fora de ordem. Os anéis de descritores DMA do Capítulo 20 introduzem o padrão para dispositivos baseados em DMA; drivers reais com fila de comandos estendem esse padrão para contextos sem DMA também.

### O Modelo de Sensor

O sensor do Capítulo 17 produz um valor em onda triangular usando uma fórmula aritmética simples. Sensores reais produzem valores influenciados por ruído físico, deriva de temperatura, jitter de amostragem e não linearidades específicas do dispositivo. Uma simulação mais realista incluiria ruído gaussiano, um termo de deriva lenta e valores discrepantes ocasionais.

A história completa: a modelagem de sensores é uma disciplina por si só, e os testes reais de drivers para dispositivos de sensor frequentemente usam dados reais capturados e reproduzidos por meio de uma camada de simulação. O sensor aritmético do Capítulo 17 é pedagogicamente suficiente; uma simulação de produção seria substancialmente mais rica.

### A Taxonomia de Erros

O Capítulo 17 distingue quatro tipos de falha: timeout, read-1s, error e stuck-busy. O hardware real produz uma gama muito mais ampla de modos de falha, incluindo escritas parciais, bits de interrupção travados, campos invertidos, valores de registrador com erro de um (off-by-one) e ruído elétrico transitório. As quatro falhas do capítulo são um ponto de partida.

A história completa: autores de drivers FreeBSD que trabalham com famílias de dispositivos específicas desenvolvem taxonomias de falhas muito mais elaboradas com o tempo. O código de tratamento de erros do driver e1000 tem algumas centenas de linhas próprias, cobrindo dezenas de modos de falha distintos. O Capítulo 17 ensina o padrão; aplicá-lo a hardware real produz, por necessidade, uma taxonomia mais rica.

### A Ausência de Interrupções

O driver do Capítulo 17 faz polling de `STATUS` lendo o registrador em um loop (com `pause_sbt` entre as leituras). Drivers reais usam interrupções: o dispositivo gera uma interrupção quando `STATUS` muda, o handler de interrupção do driver acorda a thread em espera, e a thread lê o novo estado. O polling é custoso em comparação com interrupções.

A história completa: o Capítulo 19 apresenta as interrupções. O driver do Capítulo 17 receberá um caminho de wake-up baseado em `INTR_STATUS` no Capítulo 19, e os loops de polling se tornarão muito mais curtos (acordar uma vez em vez de acordar a cada milissegundo). O polling do Capítulo 17 é um degrau pedagógico.

### A Precisão de Temporização

Os callouts do Capítulo 17 executam em aproximadamente o intervalo configurado, com precisão da ordem de 1 ms (a resolução de timer padrão do kernel). Dispositivos reais às vezes exigem temporização de sub-microsegundo. Um driver que depende dessa temporização deve usar `DELAY(9)` para esperas curtas e não pode depender da precisão dos callouts.

A história completa: temporização de alta precisão em drivers requer mecanismos específicos de plataforma (o TSC, o timer ACPI-PM, o HPET, ou timers específicos de hardware em plataformas embarcadas). A simulação em escala de milissegundos do Capítulo 17 não exercita caminhos de sub-milissegundo; um driver para um dispositivo real com esses requisitos usaria `DELAY` ou um mecanismo assistido por hardware.

### Resumo

O Capítulo 17 é uma simulação didática. Cada simplificação que ele faz é deliberada, nomeada e retomada por um capítulo posterior ou por uma extensão para drivers reais. Os padrões que o Capítulo 17 ensina são os padrões que cada capítulo posterior estende; a disciplina que o Capítulo 17 constrói é a disciplina da qual cada capítulo posterior depende. O capítulo deixa de contar a história completa da simulação de propósito. Os capítulos subsequentes e a prática no mundo real preenchem o restante.



## Referência: Glossário dos Termos do Capítulo 17

Um glossário breve dos termos introduzidos ou usados extensamente no Capítulo 17.

**Atualização autônoma.** Uma mudança em um registrador que ocorre sem que o driver a inicie. No Capítulo 17, o callout do sensor produz atualizações autônomas.

**Ciclo de comando.** A sequência completa de eventos desde "o driver decide emitir um comando" até "o comando é concluído e o driver observa o resultado". No Capítulo 17: escrever `DATA_IN`, definir `CTRL.GO`, aguardar `STATUS.DATA_AV`, ler `DATA_OUT`.

**Callout.** Uma primitiva do FreeBSD que agenda um callback para ser executado em um momento futuro. O callback é executado em uma thread de callout. Criado com `callout_init_mtx`, armado com `callout_reset_sbt`, cancelado com `callout_stop`, drenado com `callout_drain`.

**Injeção de falhas.** A introdução deliberada de uma condição de falha no sistema para fins de teste. O framework do Capítulo 17 suporta quatro modos de falha configuráveis por meio de `FAULT_MASK` e `FAULT_PROB`.

**Travado (latched).** Um bit de registrador que, uma vez definido, permanece definido até ser explicitamente limpo. Bits de erro em dispositivos reais são tipicamente travados. O `STATUS.ERROR` do Capítulo 17 é travado.

**Polling.** Leitura repetida de um registrador para detectar uma mudança. O `myfirst_wait_for_bit` do Capítulo 17 faz polling de `STATUS` com pausa de 1 ms entre as leituras.

**Comando pendente.** Um comando que foi iniciado mas ainda não foi concluído. A simulação do Capítulo 17 rastreia isso com `sim->command_pending`.

**Protocolo.** As regras que o driver deve seguir ao se comunicar com o dispositivo. No Capítulo 17: escrever `DATA_IN` antes de `CTRL.GO`; aguardar `STATUS.DATA_AV` antes de ler `DATA_OUT`; limpar `DATA_AV` após a leitura.

**Leitura-para-limpar (RC).** Uma semântica de registrador em que a leitura retorna o valor atual e em seguida o limpa. `INTR_STATUS` em muitos dispositivos reais é RC.

**Backend de simulação.** O código do kernel que fornece o comportamento do dispositivo simulado. No Capítulo 17, esse código é `myfirst_sim.c`.

**Efeito colateral.** Comportamento que um acesso a registrador dispara além da leitura ou escrita óbvia. Escritas em `CTRL.RESET` têm um efeito colateral (elas reiniciam o dispositivo). Leituras de `INTR_STATUS` têm um efeito colateral (elas limpam bits pendentes, sob a semântica RC).

**Persistente (sticky).** Similar a travado. Um bit que persiste até ser deliberadamente limpo.

**Timeout.** Uma espera delimitada que retorna um erro se o evento esperado não ocorrer a tempo. O `wait_for_bit` do Capítulo 17 expira após o número de milissegundos configurado.



## Referência: Resumo das Diferenças do Driver do Capítulo 17

Um resumo compacto do que o Capítulo 17 modifica no driver, para leitores que queiram ver de relance como o driver evoluiu de `0.9-mmio` para `1.0-simulated`.

### Novos arquivos

- `myfirst_sim.c` (cerca de 300 linhas).
- `myfirst_sim.h` (cerca de 50 linhas).
- `SIMULATION.md` (cerca de 200 linhas).

### Arquivos modificados

- `myfirst.c`: adicionadas as chamadas `myfirst_sim_attach` e `myfirst_sim_detach`; adicionados os helpers `myfirst_write_cmd`, `myfirst_sample_cmd`, `myfirst_wait_for_bit`; adicionados campos de timeout ao softc; adicionados contadores de estatísticas.
- `myfirst_hw.h`: adicionados novos offsets de registradores (0x28 a 0x3c) e máscaras de bits; adicionado `MYFIRST_CTRL_GO`; adicionadas constantes de falha.
- `myfirst_hw.c`: sem alterações no código de acesso; `myfirst_ctrl_update` estendido para interceptar `GO`.
- `HARDWARE.md`: adicionados os novos registradores, o novo bit `CTRL.GO`, as notas de comportamento dinâmico.
- `LOCKING.md`: adicionado o passo de detach da simulação à sequência de detach ordenada.
- `Makefile`: adicionado `myfirst_sim.c` a `SRCS`.

### Evolução do número de linhas

Os tamanhos dos arquivos em cada estágio (aproximados):

| Estágio                           | myfirst.c | myfirst_hw.c | myfirst_sim.c | Total |
|-----------------------------------|-----------|--------------|---------------|-------|
| Capítulo 16, Estágio 4 (início)   | 650       | 380          | (ainda não)   | 1030  |
| Capítulo 17, Estágio 1            | 680       | 400          | 120           | 1200  |
| Capítulo 17, Estágio 2            | 800       | 400          | 150           | 1350  |
| Capítulo 17, Estágio 3            | 820       | 410          | 180           | 1410  |
| Capítulo 17, Estágio 4            | 840       | 410          | 250           | 1500  |
| Capítulo 17, Estágio 5 (final)    | 800       | 400          | 300           | 1500  |

O driver cresceu de aproximadamente 1030 linhas para 1500 linhas ao longo do capítulo. Cerca de 470 linhas de código novo, divididas aproximadamente em 150 em `myfirst.c`, 20 em `myfirst_hw.c` e 300 no novo `myfirst_sim.c`. O refactor do Estágio 5 reduziu ligeiramente `myfirst.c` ao mover alguns helpers para `myfirst_sim.c`.

### Adições de comportamento

- O sensor atualiza autonomamente a cada 100 ms.
- `CTRL.GO` dispara um comando que é concluído após `DELAY_MS` milissegundos.
- `OP_COUNTER` é incrementado a cada comando.
- `FAULT_MASK` e `FAULT_PROB` controlam as falhas injetadas.
- Quatro modos de falha (timeout, read-1s, error, stuck-busy).
- Contadores de estatísticas por resultado.
- Timeouts configuráveis de comando e de prontidão.
- Ciclo de comando integrado nos caminhos de escrita e leitura.

### Adições de testes

- Oito novos scripts de teste em `labs/`.
- Um `full_regression.sh` atualizado que inclui os novos scripts.
- Tempo de execução esperado da regressão completa: cerca de 3 minutos.



## Referência: Lendo um Driver Orientado a Callout

Um guia de leitura por um driver real do FreeBSD cujo design é semelhante ao da simulação do Capítulo 17: um driver cujo trabalho periódico é agendado por meio de callouts, e cujo principal trabalho é responder a mudanças de estado. O driver é `/usr/src/sys/dev/led/led.c`, o pseudo-driver de dispositivo LED.

Abra-o em um terminal. Acompanhe o texto.

### A Estrutura do Driver LED

`led.c` tem cerca de 400 linhas. Sua função é expor uma interface para que outros drivers anunciem um dispositivo LED por meio de `/dev/led/NAME`, e para que o espaço do usuário faça o LED piscar escrevendo padrões. O trabalho real de hardware é delegado ao driver que registrou o LED; `led.c` cuida do agendamento e da interpretação do padrão.

As principais estruturas de dados:

- `struct led_softc`: estado por LED, incluindo um `struct callout led_ch`, um `struct sbuf *spec` com o padrão de piscar, e um ponteiro de função para o callback "definir estado" do driver.
- `struct mtx led_mtx`: um mutex global que protege a lista de todos os LEDs.
- Uma lista encadeada de todos os LEDs.

### O Callback do Callout

O interpretador do padrão de piscar é executado no callback do callout:

```c
static void
led_timeout(void *p)
{
        struct ledsc *sc = p;
        char c;
        int count;

        if (sc->spec == NULL || sc->ptr == NULL || sc->count == 0) {
                /* no pattern; stop blinking */
                sc->func(sc->private, sc->on);
                return;
        }

        c = *(sc->ptr)++;
        if (c == '.') {
                /* Pattern complete, restart. */
                sc->ptr = sbuf_data(sc->spec);
                c = *(sc->ptr)++;
        }

        if (c >= 'a' && c <= 'j') {
                sc->func(sc->private, 0);   /* LED off */
                count = (c - 'a') + 1;
        } else if (c >= 'A' && c <= 'J') {
                sc->func(sc->private, 1);   /* LED on */
                count = (c - 'A') + 1;
        } else {
                count = 1;
        }

        callout_reset(&sc->led_ch, count * hz / 10, led_timeout, sc);
}
```

(O código foi ligeiramente abreviado em relação à fonte real para fins de apresentação.)

### O Padrão

Observe a estrutura do callback.

Primeiro, ele verifica se há algo a fazer. Se `spec == NULL`, o padrão foi limpo; a função apaga o LED (deixando-o em um estado conhecido) e retorna sem rearmar o callout. Esse é exatamente o padrão `if (!sim->running) return;` dos callouts do Capítulo 17.

Segundo, ele avança pelo buffer de padrão um caractere de cada vez. A linguagem de padrão usa letras minúsculas de 'a' a 'j' para significar "LED apagado por 100 ms a 1000 ms" e maiúsculas de 'A' a 'J' para "LED aceso". Essa é uma pequena máquina de estados conduzida pelo callout.

Terceiro, ele chama `sc->func(sc->private, state)` para de fato alternar o estado do LED. Esse é o trabalho de hardware, delegado ao driver que registrou o LED. O callback `led_timeout` não sabe se o LED é um pino GPIO, um controlador de LED conectado via I2C, ou uma mensagem para um dispositivo remoto; ele simplesmente chama o ponteiro de função.

Quarto, ele reinsere o callout com um intervalo determinado pelo caractere de padrão atual. Esse é o padrão `callout_reset_sbt` do Capítulo 17, embora `led.c` use o mais antigo `callout_reset` com intervalos baseados em ticks.

### Lições para o Capítulo 17

O driver `led.c` ilustra vários padrões do Capítulo 17:

- Um callout avança uma máquina de estados um passo a cada invocação.
- O callout se reinsere com um intervalo variável baseado no estado atual.
- Um ramo "sem trabalho a fazer" retorna sem rearmar; o caminho de detach depende disso para eventualmente drenar o callout.
- O callback usa um ponteiro de função para o trabalho delegado, mantendo a lógica do callout separada do trabalho específico de hardware.

A simulação do Capítulo 17 usa os mesmos padrões com roupagem ligeiramente diferente. O callback do sensor atualiza um registrador; o callback de comando dispara uma conclusão; o callback de ocupado reaserta um bit de status. Cada um é uma pequena máquina de estados avançada pelo callout.

### Um Exercício

Leia `led.c` do início ao fim. Ele é um dos drivers mais curtos em `/usr/src/sys/dev/` e ilustra um design limpo orientado a callout. Após a leitura, escreva um resumo de um parágrafo explicando como `led_timeout` interage com `led_drvinit`, `led_destroy` e `led_write`. Se você conseguir articular essa relação, terá internalizado o padrão de driver orientado a callout.



## Referência: A Diferença Entre "Na Simulação" e "Na Realidade"

O Capítulo 17 simula um dispositivo. Uma pergunta natural é: quão diferente é a simulação de um dispositivo real e de que formas específicas o driver precisaria mudar quando os dois fossem trocados?

Uma breve comparação.

### O que a simulação e a realidade têm em comum

- O mapa de registradores: os mesmos offsets, as mesmas larguras, os mesmos tipos de acesso.
- As macros CSR: `CSR_READ_4`, `CSR_WRITE_4`, `CSR_UPDATE_4` expandem para as mesmas chamadas `bus_space_*`.
- A lógica do ciclo de comando do driver: escrever `DATA_IN`, definir `CTRL.GO`, aguardar `DATA_AV`, ler `DATA_OUT`.
- A disciplina de locking: `sc->mtx` protege o acesso aos registradores em ambos os casos.
- As estatísticas: `cmd_successes`, `cmd_errors`, e assim por diante rastreiam eventos reais de qualquer forma.

### O que difere

- A tag e o handle: a simulação usa `X86_BUS_SPACE_MEM` e um endereço de `malloc(9)`; a realidade usa a tag e o handle que `rman_get_bustag` e `rman_get_bushandle` retornam para um PCI BAR alocado.
- O tempo de vida do bloco de registradores: o da simulação vai de `myfirst_sim_attach` até `myfirst_sim_detach`; o da realidade vai de `bus_alloc_resource_any` até `bus_release_resource`.
- O comportamento autônomo: a simulação o produz por meio de callouts; a realidade o produz por meio da lógica interna do dispositivo. O driver não percebe a diferença.
- Os modos de falha: as falhas da simulação são deliberadas e controladas; as falhas da realidade acontecem quando acontecem.
- O timing: o `DELAY_MS` da simulação é um registrador; o timing da realidade é determinado pelo design do dispositivo e não pode ser alterado.
- O self-clear do `CTRL.GO`: o da simulação é implementado pela interceptação de escrita; o da realidade é implementado em silício. O driver espera o mesmo comportamento.
- A recuperação de erros: a função `myfirst_recover_from_stuck` da simulação manipula diretamente o estado da simulação; o caminho de recuperação da realidade usa apenas operações de registrador (tipicamente um reset).

A lista é curta. Esse é o ponto central da abstração: a maior parte do driver é idêntica, e as partes que diferem estão bem localizadas.

### A Mudança do Capítulo 18

Quando o Capítulo 18 substitui a simulação por PCI real, as alterações no driver são:

1. `myfirst_hw_attach` é modificado: em vez de usar `malloc` para o bloco de registradores, passa a chamar `bus_alloc_resource_any` para alocar o BAR do PCI.
2. `myfirst_hw_detach` é modificado: em vez de liberar o bloco de registradores com `free`, passa a chamar `bus_release_resource` para liberar o BAR.
3. Os arquivos de simulação (`myfirst_sim.c`, `myfirst_sim.h`) deixam de ser compilados; o Makefile os remove.
4. A lógica de probe do driver ganha um método `probe` que identifica o dispositivo real pelo vendor ID e device ID.
5. O registro com `DRIVER_MODULE` muda de um estilo de pseudo-dispositivo para um estilo de anexação PCI.

Todo o restante (o ciclo de comandos, as estatísticas, o locking, a documentação) permanece igual. A disciplina que o Capítulo 17 construiu é o que torna isso possível.



## Referência: Quando a Simulação Não É Suficiente

Uma visão equilibrada dos limites da simulação. O Capítulo 17 ensina a simulação como técnica principal, mas ela não substitui todos os tipos de teste. Alguns cenários exigem hardware real ou um dispositivo virtual com respaldo de hypervisor.

### Quando o bug está no timing do silício real

Uma condição de corrida que se manifesta apenas em determinadas proporções de clock do barramento, em padrões específicos de tráfego de memória ou em revisões específicas de firmware do dispositivo não pode ser reproduzida em simulação. A simulação roda na velocidade de uma thread do kernel, não na velocidade do hardware, e seu timing é ditado pelo escalonador do kernel, não pelo tecido do barramento.

### Quando o bug está no código específico da plataforma

Um driver que precisa lidar com remapeamento de IOMMU, atributos específicos de cache ou barreiras de memória específicas da arquitetura não pode ser totalmente validado em simulação. A memória do kernel na simulação tem comportamento de cache diferente da memória de um dispositivo real, e os caminhos específicos da arquitetura só são exercitados quando há memória de dispositivo real envolvida.

### Quando o bug depende de recursos reais de hardware

Um driver que precisa configurar a largura de link PCIe, gerenciar estados de energia ou interagir com o firmware do dispositivo não pode ser validado em simulação. A simulação não tem link PCIe, estados de energia nem firmware.

### Quando o bug está no próprio teste

Um teste que exercita a simulação de forma que não corresponde ao uso real do dispositivo gera falsa confiança. A simulação pode passar no teste; o dispositivo real ainda pode falhar em condições de uso reais. Testar contra a simulação deve ser combinado com testes contra o hardware real sempre que este estiver disponível.

### O que fazer a respeito

A abordagem correta é o teste em camadas. A simulação captura bugs de protocolo, bugs de ordem de lock, bugs de tratamento de erros e a maioria dos erros de lógica. Dispositivos virtuais com respaldo de hypervisor (virtio no bhyve ou no QEMU) capturam bugs específicos de PCI, problemas de bus mastering e alguns problemas de timing. Hardware real em um sistema de laboratório captura os bugs de última milha que nada mais consegue reproduzir.

No Capítulo 17, o foco é a simulação. No Capítulo 18, o foco é o PCI com respaldo de hypervisor. Capítulos posteriores sobre DMA e interrupções introduzirão superfícies de teste adicionais. O teste em hardware real em um sistema de laboratório dedicado é uma disciplina que o autor de drivers desenvolve ao longo do tempo; este livro pode fornecer o vocabulário, mas não o hardware.



## Referência: Leituras Complementares

Se você quiser aprofundar nos temas abordados pelo Capítulo 17, os recursos a seguir são bons próximos passos.

### Páginas do manual do FreeBSD

- `callout(9)`: a API de callout completa.
- `pause(9)` e `pause_sbt(9)`: as primitivas de sleep em detalhes.
- `arc4random(9)`: o gerador de números pseudo-aleatórios.
- `bus_space(9)`: revisite sob a perspectiva da simulação.
- `fail(9)`: o framework de injeção de falhas em produção.

### Arquivos-fonte que valem a leitura

- `/usr/src/sys/dev/led/led.c`: um driver de pseudo-dispositivo orientado a callout. Curto, legível e ilustrativo.
- `/usr/src/sys/dev/random/random_harvestq.c`: uma fila de coleta orientada a callout. Mais complexa que `led.c`, mas com um padrão semelhante.
- `/usr/src/sys/kern/kern_fail.c`: o framework `fail(9)`. Cerca de 1500 linhas, mas altamente modular.
- `/usr/src/sys/kern/kern_timeout.c`: a implementação do subsistema de callout. Leia quando quiser entender como `callout_reset_sbt` funciona de verdade.

### Drivers reais com callouts semelhantes aos do Capítulo 17

- `/usr/src/sys/dev/ale/if_ale.c`: `ale_tick` é um callout que verifica periodicamente o estado do link. Padrão semelhante ao callout de sensor do Capítulo 17.
- `/usr/src/sys/dev/e1000/if_em.c`: `em_local_timer` é um callout que atualiza estatísticas e trata eventos de watchdog. Ligeiramente mais elaborado que o callout de sensor.
- `/usr/src/sys/dev/iwm/if_iwm.c`: usa múltiplos callouts para diferentes máquinas de estado de protocolo. Um exemplo avançado, mas educativo.

### Leituras relacionadas à simulação de hardware

- O capítulo do FreeBSD Handbook sobre emulação (para entender como o virtio e o bhyve se relacionam com a simulação no kernel).
- A página de manual `bhyve(8)`, para compreender os dispositivos virtuais com respaldo de hypervisor que serão relevantes no Capítulo 18.



## Referência: Um Exemplo Completo: O `myfirst_sim.h` na Íntegra

O `myfirst_sim.h` completo no estado em que se encontra ao final do Capítulo 17, para consulta rápida. O código-fonte também está disponível em `examples/part-04/ch17-simulating-hardware/stage5-final/myfirst_sim.h`.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sim.h -- Chapter 17 simulation API.
 *
 * The simulation layer turns the Chapter 16 static register block into
 * a dynamic device: autonomous sensor updates, command-triggered
 * delayed completions, read-to-clear semantics for INTR_STATUS, and a
 * fault-injection framework. The API is small; most of the simulation
 * is in myfirst_sim.c.
 */

#ifndef _MYFIRST_SIM_H_
#define _MYFIRST_SIM_H_

#include <sys/callout.h>
#include <sys/stdbool.h>

struct myfirst_softc;

/*
 * Simulation state. One per driver instance. Allocated in
 * myfirst_sim_attach, freed in myfirst_sim_detach.
 */
struct myfirst_sim {
        /* The three simulation callouts. */
        struct callout       sensor_callout;
        struct callout       command_callout;
        struct callout       busy_callout;

        /* Last scheduled command's data. Saved so command_cb can
         * latch DATA_OUT when it fires. */
        uint32_t             pending_data;

        /* Saved fault state for this command. Set in start_command,
         * consumed by command_cb. */
        uint32_t             pending_fault;

        /* Whether a command is currently in flight. */
        bool                 command_pending;

        /* Baseline sensor value; the sensor callout oscillates
         * around this. */
        uint32_t             sensor_baseline;

        /* Counter used by the sensor oscillation algorithm. */
        uint32_t             sensor_tick;

        /* Local operation counter; mirrors OP_COUNTER register. */
        uint32_t             op_counter;

        /* Whether the simulation callouts are running. Checked by
         * every callout before doing work. */
        bool                 running;
};

/*
 * API. All functions assume sc->sim is valid (that is, 
 * myfirst_sim_attach has been called successfully) unless noted
 * otherwise.
 */

/*
 * Allocate and initialise the simulation state. Registers callouts
 * with sc->mtx. Does not start the callouts; call _enable for that.
 * Returns 0 on success, an errno on failure.
 */
int  myfirst_sim_attach(struct myfirst_softc *sc);

/*
 * Drain all simulation callouts, free the simulation state. Safe to
 * call with sc->sim == NULL. The caller must not hold sc->mtx (this
 * function sleeps in callout_drain).
 */
void myfirst_sim_detach(struct myfirst_softc *sc);

/*
 * Start the simulation callouts. Requires sc->mtx held.
 */
void myfirst_sim_enable(struct myfirst_softc *sc);

/*
 * Stop the simulation callouts. Does not drain (that is _detach's
 * job). Requires sc->mtx held.
 */
void myfirst_sim_disable(struct myfirst_softc *sc);

/*
 * Start a command. Called from the driver when CTRL.GO is written.
 * Reads DATA_IN and DELAY_MS, schedules the command completion
 * callout. Rejects overlapping commands. Requires sc->mtx held.
 */
void myfirst_sim_start_command(struct myfirst_softc *sc);

/*
 * Register simulation-specific sysctls on the driver's sysctl tree.
 * Called from myfirst_attach after sc->sysctl_tree is established.
 */
void myfirst_sim_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_SIM_H_ */
```



## Referência: Uma Comparação com os Padrões do Capítulo 16

Uma comparação lado a lado de onde o Capítulo 17 estende o Capítulo 16 e onde ele introduz material genuinamente novo.

| Padrão                               | Capítulo 16                          | Capítulo 17                                         |
|--------------------------------------|--------------------------------------|-----------------------------------------------------|
| Acesso a registradores               | `CSR_READ_4`, etc.                   | Mesma API, sem alterações                           |
| Log de acesso                        | Introduzido                          | Reutilizado, estendido com entradas de injeção de falhas |
| Disciplina de lock                   | `sc->mtx` em cada acesso             | O mesmo, mais callouts via `callout_init_mtx`       |
| Layout de arquivos                   | `myfirst_hw.c` adicionado            | `myfirst_sim.c` adicionado                          |
| Mapa de registradores                | 10 registradores, 40 bytes           | 16 registradores, 60 bytes (todos na mesma alocação de 64 bytes) |
| Bits de CTRL                         | ENABLE, RESET, MODE, LOOPBACK        | O mesmo, mais GO (bit 9)                            |
| Bits de STATUS                       | READY, BUSY, ERROR, DATA_AV          | O mesmo, mas alterados dinamicamente pelos callouts |
| Callouts                             | Um (reg_ticker_task como tarefa)     | Três (sensor, command, busy)                        |
| Timeouts                             | Não aplicável                        | Introduzidos (cmd_timeout_ms, rdy_timeout_ms)       |
| Recuperação de erros                 | Mínima                               | Caminho completo de recuperação                     |
| Estatísticas                         | Nenhuma                              | Contadores por resultado                            |
| Injeção de falhas                    | Nenhuma                              | Quatro modos de falha                               |
| HARDWARE.md                          | Introduzido                          | Estendido                                           |
| LOCKING.md                           | Estendido a partir do Cap15          | Estendido                                           |
| SIMULATION.md                        | Não presente                         | Introduzido                                         |

O Capítulo 17 expande o Capítulo 16 sem quebrar nada. Toda capacidade do Capítulo 16 é preservada; toda nova capacidade é acrescentada. O driver em `1.0-simulated` é um superconjunto estrito do driver em `0.9-mmio`.



## Referência: Uma Nota Final sobre a Filosofia de Simulação

Um parágrafo para encerrar o capítulo, que vale a pena reler depois de concluir os laboratórios.

A simulação é, em sua essência, um ato de modelagem. Você pega um sistema sobre o qual não tem controle total (um dispositivo real) e constrói um sistema menor sobre o qual tem controle (uma simulação) que se comporta de forma semelhante. A simulação nunca é perfeita. Seu valor não vem de replicar o sistema real em cada detalhe, mas de preservar as propriedades que importam para a pergunta que você está fazendo.

No Capítulo 17, a propriedade relevante é a correção do protocolo: o driver lida corretamente com o ciclo de comandos, o timing, os casos de erro e a recuperação? Uma simulação que preserva a correção do protocolo é uma simulação que justifica sua existência, mesmo que acerte cada detalhe de timing apenas aproximadamente.

Para os capítulos posteriores e para o seu trabalho futuro, as propriedades relevantes podem ser diferentes. Um teste de desempenho precisa de uma simulação que preserve o comportamento de throughput. Um teste de energia precisa de uma simulação que preserve os estados ocioso e ativo. Um teste de segurança precisa de uma simulação capaz de produzir entradas adversariais.

A habilidade que o Capítulo 17 ensina não é "como simular este dispositivo em particular". É "como identificar o que uma simulação deve preservar, e como construir uma que preserve isso". Essa habilidade é transferível, e é ela que servirá a você em cada driver que você escrever.
