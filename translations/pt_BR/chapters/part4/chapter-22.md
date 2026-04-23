---
title: "Gerenciamento de Energia"
description: "O Capítulo 22 encerra a Parte 4 ensinando como o driver myfirst sobrevive a suspend, resume e shutdown. Ele explica o que é gerenciamento de energia do ponto de vista de um driver de dispositivo; como os estados de suspensão ACPI (S0-S5) e os estados de energia de dispositivo PCI (D0-D3hot/D3cold) se combinam para formar uma transição completa; o que os métodos DEVICE_SUSPEND, DEVICE_RESUME, DEVICE_SHUTDOWN e DEVICE_QUIESCE fazem e em que ordem o kernel os entrega; como silenciar interrupções, DMA, timers e trabalhos diferidos com segurança; como restaurar o estado no resume sem perder dados; como o gerenciamento de energia em tempo de execução difere do suspend de sistema completo; como testar transições de energia a partir do espaço do usuário com acpiconf, zzz e devctl; como depurar dispositivos travados, interrupções perdidas e falhas de DMA após o resume; e como refatorar o código de gerenciamento de energia em seu próprio arquivo. O driver evolui da versão 1.4-dma para a 1.5-power, ganha os arquivos myfirst_power.c e myfirst_power.h, ganha um documento POWER.md e encerra a Parte 4 com um driver que lida com ciclos de suspend-resume com a mesma elegância com que lida com attach-detach."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 22
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 210
language: "pt-BR"
---
# Gerenciamento de Energia

## Orientação ao Leitor e Resultados Esperados

O Capítulo 21 encerrou com o driver na versão `1.4-dma`. Esse driver se conecta a um dispositivo PCI, aloca vetores MSI-X, processa interrupções por meio de um pipeline de filtro e tarefa, move dados por um buffer `bus_dma(9)` e libera seus recursos ao ser solicitado a fazer o detach. Para um driver que faz boot, executa e eventualmente é descarregado, essa mecânica está completa. O que o driver ainda não trata é o terceiro tipo de evento que um sistema moderno lhe impõe: o momento em que a própria energia está prestes a mudar.

Mudanças de energia são diferentes de attach e detach. Um attach começa do zero e termina com um dispositivo funcionando. Um detach começa com um dispositivo funcionando e termina com nada. Ambas são transições únicas sobre as quais o driver pode agir sem pressa. Um suspend não é nenhum dos dois. O driver entra no suspend já em execução, com interrupções ativas, transferências DMA em andamento, timers ativos e um dispositivo ao qual o kernel ainda espera que responda a consultas. O driver precisa encerrar tudo isso dentro de uma janela de tempo estreita, entregar o dispositivo a um estado de menor consumo de energia, sobreviver à perda de energia sem esquecer o que precisa saber e, em seguida, remontar tudo do outro lado como se nada tivesse acontecido. O usuário, idealmente, não percebe absolutamente nada. A tampa fecha, um segundo depois ela abre, e a videoconferência retoma na mesma aba do navegador como se a interrupção nunca tivesse acontecido.

O Capítulo 22 ensina como o driver conquista essa ilusão. O escopo do capítulo é precisamente este: o que é o gerenciamento de energia no nível do driver; como o kernel permite que o driver antecipe uma transição de energia; o que significa fazer o quiesce de um dispositivo para que nenhuma atividade vaze durante a transição; como preservar o estado de que o driver precisará após o resume; como restaurar esse estado para que o dispositivo retorne ao mesmo comportamento que o usuário via antes; como estender a mesma disciplina à economia de energia por runtime suspend em dispositivos ociosos; como testar transições a partir do espaço do usuário; como depurar as falhas características que um driver com suporte a energia enfrenta; e como organizar o novo código para que o driver permaneça legível à medida que cresce. O capítulo não avança até os tópicos posteriores que se constroem sobre essa disciplina. O Capítulo 23 ensina depuração e rastreamento em profundidade; o script de regressão com suporte a energia do Capítulo 22 é apenas uma primeira amostra, não o conjunto completo de ferramentas. O capítulo de drivers de rede na Parte 6 (Capítulo 28) adiciona os hooks de energia do iflib e a coordenação de suspend com múltiplas filas; o Capítulo 22 permanece com o driver `myfirst` de fila única. Os capítulos avançados de casos reais na Parte 7 exploram hotplug e gerenciamento de domínios de energia em plataformas embarcadas; o Capítulo 22 foca nos casos de desktop e servidor onde ACPI e PCIe dominam.

O arco da Parte 4 se fecha aqui com uma disciplina, não com uma nova primitiva. O Capítulo 16 deu ao driver um vocabulário de acesso a registradores por meio de `bus_space(9)`. O Capítulo 17 ensinou-o a pensar como um dispositivo, simulando um. O Capítulo 18 o apresentou a um dispositivo PCI real. O Capítulo 19 lhe deu um par de ouvidos em um único IRQ. O Capítulo 20 lhe deu vários ouvidos, um por fila que o dispositivo gerencia. O Capítulo 21 lhe deu mãos: a capacidade de fornecer ao dispositivo um endereço físico e deixá-lo executar uma transferência por conta própria. O Capítulo 22 ensina o driver a parar de fazer tudo isso sob demanda, aguardar calmamente enquanto o sistema dorme e retomar de forma limpa quando o sistema acorda. Essa disciplina é o último ingrediente ausente antes de o driver poder se considerar pronto para produção no sentido da Parte 4. Os capítulos posteriores adicionam observabilidade, especialização e refinamento; eles assumem que a disciplina de energia está em vigor.

### Por Que Suspend e Resume Merecem um Capítulo Próprio

Uma pergunta natural neste ponto é se suspend e resume realmente precisam de um capítulo completo, após a profundidade do Capítulo 21. O driver `myfirst` já possui um caminho de detach limpo. O detach já libera interrupções, drena tarefas, desmonta o DMA e retorna o dispositivo a um estado silencioso. O driver não poderia simplesmente chamar detach no suspend e attach no resume, e encerrar o assunto?

A resposta é não, por três razões interligadas.

A primeira é que **suspend não é detach**. Um detach é permanente. O driver não precisa se lembrar de nada sobre o dispositivo após o detach terminar; quando o dispositivo retornar, é um attach novo, do zero. Um suspend é temporário, e o driver precisa se lembrar de coisas durante ele. Ele precisa lembrar seu estado de software para que a sessão do usuário possa continuar de onde parou. Ele precisa lembrar quais vetores de interrupção havia alocado. Ele precisa lembrar seus sysctls de configuração. Ele precisa lembrar quais clientes tinham o dispositivo aberto. O detach esquece tudo isso; o suspend não pode. Os dois caminhos compartilham etapas de limpeza no meio, mas divergem nas extremidades. Tratar o suspend como um detach seguido de um attach posterior seria correto no sentido mecânico estrito e errado em todos os outros aspectos: descartaria a sessão do usuário, invalidaria os descritores de arquivo abertos em `/dev/myfirst0`, perderia o estado dos sysctl e pediria ao kernel que re-fizesse o probe do dispositivo a partir de sua identidade PCI bruta a cada resume. Não é assim que os drivers modernos do FreeBSD funcionam, e o Capítulo 22 mostra o padrão correto.

A segunda é que **o orçamento de tempo é diferente**. Um detach pode ser minucioso. Um driver que leva quinhentos milissegundos para fazer o detach não tem impacto visível ao usuário; detaches acontecem no boot, durante o descarregamento do módulo ou na remoção do dispositivo, e esses momentos são entendidos como lentos. Um suspend precisa terminar dentro de um orçamento medido em dezenas de milissegundos por dispositivo em um laptop com uma centena de dispositivos, pois a soma é o que o usuário percebe como latência ao fechar a tampa. Um driver que faz uma limpeza completa similar ao detach, aguarda as filas drenarem em seu ritmo natural, desfaz cada alocação e reconstrói tudo no resume será visivelmente lento para todo o conjunto de dispositivos do sistema. O padrão do Capítulo 22 é parar a atividade rapidamente, salvar o que precisa ser salvo, manter as alocações no lugar e restaurar a partir do estado salvo. Esse padrão é o que mantém o suspend-resume abaixo de um segundo em um laptop típico.

A terceira é que **o kernel fornece ao driver um contrato específico para transições de energia**, e esse contrato tem seu próprio vocabulário, sua própria ordem de operações e seus próprios modos de falha. Os métodos kobj `DEVICE_SUSPEND` e `DEVICE_RESUME` não são apenas "detach e attach com nomes diferentes". Eles são chamados em pontos específicos da sequência de suspend de todo o sistema, com a árvore de drivers percorrida em uma ordem específica, e interagem com o salvamento e restauração automáticos do espaço de configuração da camada PCI, com a maquinaria de estados de sleep do ACPI, com as chamadas de mascaramento e desmascaramento do subsistema de interrupções e com os helpers `bus_generic_suspend` e `bus_generic_resume` que percorrem a árvore de dispositivos. Um driver que ignora o contrato pode parecer correto durante o detach, durante o DMA e durante o tratamento de interrupções, e falhar apenas quando o usuário fecha a tampa. Essa classe de falha é notoriamente difícil de depurar porque é difícil de reproduzir, e o Capítulo 22 investe tempo em tornar o contrato explícito para que as falhas não aconteçam desde o início.

O Capítulo 22 justifica seu lugar ensinando essas três ideias juntas, concretamente, com o driver `myfirst` como exemplo contínuo. Um leitor que conclui o Capítulo 22 consegue adicionar os métodos `device_suspend`, `device_resume` e `device_shutdown` a qualquer driver FreeBSD, sabe qual aspecto da disciplina do capítulo se aplica onde e compreende as interações entre a camada ACPI, a camada PCI e o estado do próprio driver. Essa habilidade se transfere diretamente para qualquer driver FreeBSD com que o leitor venha a trabalhar.

### O que o Capítulo 21 Deixou no Driver

Um breve ponto de verificação antes de continuar. O Capítulo 22 estende o driver produzido ao final do Estágio 4 do Capítulo 21, marcado como versão `1.4-dma`. Se algum dos itens abaixo estiver em dúvida, retorne ao Capítulo 21 antes de começar este capítulo.

- Seu driver compila sem erros e se identifica como `1.4-dma` em `kldstat -v`.
- O driver aloca um ou três vetores MSI-X (dependendo da plataforma), registra filtros e tarefas por vetor, vincula cada vetor a uma CPU e imprime um banner de interrupção durante o attach.
- O driver aloca uma tag `bus_dma`, aloca um buffer DMA de 4 KB, carrega-o em um mapa e expõe o endereço do barramento por meio de `dev.myfirst.N.dma_bus_addr`.
- Escrever em `dev.myfirst.N.dma_test_write=0xAA` dispara uma transferência de host para dispositivo; escrever em `dev.myfirst.N.dma_test_read=1` dispara uma transferência de dispositivo para host; ambas registram sucesso no `dmesg`.
- O caminho de detach drena a tarefa de recepção (rx task), drena o callout da simulação, aguarda qualquer DMA em andamento ser concluído, chama `myfirst_dma_teardown`, desmonta os vetores MSI-X em ordem inversa e libera os recursos.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md` e `DMA.md` estão atualizados na sua árvore de trabalho.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` estão habilitados no seu kernel de teste.

Esse driver é o que o Capítulo 22 estende. As adições são modestas em linhas, mas importantes em disciplina: um novo arquivo `myfirst_power.c`, um header correspondente `myfirst_power.h`, um pequeno conjunto de novos campos no softc para rastrear o estado de suspend e o estado de tempo de execução salvo, novos pontos de entrada `myfirst_suspend` e `myfirst_resume` conectados à tabela `device_method_t`, um novo método `myfirst_shutdown`, uma chamada às primitivas de quiesce do Capítulo 21 a partir do novo caminho de suspend, um caminho de restauração que reinicializa o dispositivo sem repetir o attach, um incremento de versão para `1.5-power`, um novo documento `POWER.md` e atualizações no teste de regressão. O modelo mental também cresce: o driver começa a pensar em seu próprio ciclo de vida como attach, execução, quiesce, sleep, wake, execução novamente e, eventualmente, detach, em vez de apenas attach, execução, detach.

### O que Você Vai Aprender

Ao concluir este capítulo, você será capaz de:

- Descrever o que gerenciamento de energia significa para um driver de dispositivo, distinguir entre a economia de energia em nível de sistema e a economia em nível de dispositivo, e identificar a diferença entre um ciclo completo de suspend-resume e uma transição de energia em tempo de execução.
- Reconhecer os estados de suspensão do sistema ACPI (S0, S1, S3, S4, S5) e os estados de energia de dispositivo PCI (D0, D1, D2, D3hot, D3cold), explicar como eles se compõem em uma única transição, e identificar quais partes de cada um são responsabilidade do driver.
- Explicar o papel dos estados de link PCIe (L0, L0s, L1, L1.1, L1.2) e do gerenciamento de energia em estado ativo (ASPM), em um nível suficiente para ler um datasheet e reconhecer o que a plataforma controla automaticamente versus o que o driver deve configurar explicitamente.
- Adicionar entradas `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN` e (opcionalmente) `DEVICE_QUIESCE` à tabela `device_method_t` de um driver, e implementar cada uma delas de modo que se componham com `bus_generic_suspend(9)` e `bus_generic_resume(9)` na árvore de dispositivos.
- Explicar o que significa silenciar um dispositivo com segurança antes de uma transição de energia, e aplicar o padrão: mascarar interrupções no dispositivo, parar de submeter novo trabalho de DMA, drenar as transferências em andamento, drenar os callouts e taskqueues, liberar ou descartar buffers conforme a política determinar, e deixar o dispositivo em um estado definido e quieto.
- Explicar por que a camada PCI salva e restaura automaticamente o espaço de configuração em torno de `device_suspend` e `device_resume`, quando o driver precisa complementar isso com suas próprias chamadas a `pci_save_state`/`pci_restore_state`, e quando não deve fazê-lo.
- Implementar um caminho de resume limpo que reative o bus mastering, restaure os registradores do dispositivo a partir do estado salvo pelo driver, reative a máscara de interrupção, revalide a identidade do dispositivo e readmita clientes ao dispositivo sem perder dados nem gerar interrupções espúrias.
- Reconhecer os casos em que um dispositivo se reinicia silenciosamente durante o suspend, como detectar esse reinício e como reconstruir apenas o estado que foi efetivamente perdido.
- Implementar um auxiliar de gerenciamento de energia em tempo de execução que coloque um dispositivo ocioso no estado D3 e o acorde de volta ao D0 sob demanda, e discutir o tradeoff entre latência e consumo de energia.
- Disparar uma suspensão completa do sistema a partir do espaço do usuário com `acpiconf -s 3` ou `zzz`, uma suspensão por dispositivo com `devctl suspend` e `devctl resume`, e observar as transições por meio de `devinfo -v`, `sysctl hw.acpi.*` e dos próprios contadores do driver.
- Depurar as falhas características de código com consciência de energia: dispositivos travados, interrupções perdidas, DMA corrompido após o resume, eventos de wake PME# perdidos e reclamações do WITNESS sobre sleep-with-locks-held durante o suspend. Aplicar os padrões de recuperação correspondentes.
- Refatorar o código de gerenciamento de energia do driver em um par dedicado `myfirst_power.c`/`myfirst_power.h`, incrementar a versão do driver para `1.5-power`, estender o teste de regressão para cobrir suspend e resume, e produzir um documento `POWER.md` que explique o subsistema ao próximo leitor.
- Ler o código de gerenciamento de energia em um driver real como `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/xl/if_xl.c` ou `/usr/src/sys/dev/virtio/block/virtio_blk.c`, e mapear cada chamada de volta aos conceitos introduzidos no Capítulo 22.

A lista é longa. Os itens são específicos. O objetivo do capítulo é a composição, não qualquer item isolado.

### O Que Este Capítulo Não Aborda

Vários tópicos adjacentes são explicitamente adiados para que o Capítulo 22 permaneça focado na disciplina do lado do driver.

- **Internos avançados do ACPI**, como o interpretador AML, as tabelas SSDT/DSDT, a semântica dos métodos `_PSW`/`_PRW`/`_PSR` e o subsistema de botões ACPI. O capítulo usa ACPI apenas por meio da camada que o kernel expõe ao driver; os detalhes internos pertencem a um capítulo posterior, voltado para plataformas.
- **Mecânica de hibernação em disco (S4)**. O suporte a S4 no FreeBSD tem sido historicamente parcial em x86, e o contrato do lado do driver é essencialmente uma versão mais rigorosa do S3. O capítulo menciona S4 por completude e o trata como S3 para fins do driver.
- **Cpufreq, powerd e escalonamento de frequência de CPU**. Esses mecanismos afetam a energia da CPU, não a energia do dispositivo. Um driver cujo dispositivo está em D0 não é afetado pelo P-state da CPU; o capítulo não aborda o gerenciamento de energia da CPU.
- **Coordenação de suspend entre PF e VFs no SR-IOV**. O suspend de Virtual Function tem suas próprias restrições de ordenação e pertence a um capítulo especializado.
- **Hotplug e remoção surpresa**. Remover um dispositivo por desconexão física é semelhante em espírito ao suspend, mas usa caminhos de código diferentes (`BUS_CHILD_DELETED`, `device_delete_child`). A Parte 7 aborda hotplug em profundidade; o Capítulo 22 menciona a relação e segue em frente.
- **Suspend de Thunderbolt e docks USB-C**. Esses compõem ACPI, hotplug PCIe e gerenciamento de energia USB, e pertencem a uma seção posterior dedicada.
- **Frameworks de power-domain e clock-gating em plataformas embarcadas**, como as propriedades `power-domains` e `clocks` do device-tree em arm64 e RISC-V. O capítulo usa convenções x86 ACPI e PCI ao longo de toda sua extensão, e menciona os equivalentes embarcados de passagem quando o conceito é paralelo.
- **Política personalizada de wake-on-LAN, wake-on-pattern e fontes de wake específicas de aplicação**. O capítulo explica como uma fonte de wake é conectada (PME#, USB remote wakeup, GPIO wake) sem tentar ensinar cada variação específica de hardware.
- **Os internos dos caminhos `ksuspend`/`kresume` e a migração de cpuset do kernel em torno do suspend**. O driver não vê isso diretamente; esses mecanismos afetam o roteamento de interrupções e o desligamento de CPUs, não o contrato visível do driver.

Manter-se dentro dessas linhas faz do Capítulo 22 um capítulo sobre a disciplina de energia do lado do driver. O vocabulário se transfere; as especializações acrescentam detalhes em capítulos posteriores sem precisar de uma nova base.

### Estimativa de Tempo

- **Leitura apenas**: quatro a cinco horas. O modelo conceitual de gerenciamento de energia não é tão denso quanto DMA nem tão mecânico quanto interrupções; grande parte do tempo é dedicada a construir a imagem mental de como ACPI, PCI e o driver se compõem durante uma transição.
- **Leitura mais digitação dos exemplos práticos**: dez a doze horas ao longo de duas ou três sessões. O driver evolui em três estágios: esqueleto de suspend e resume com log, quiesce e restore completos, e finalmente a refatoração em `myfirst_power.c`. Cada estágio é curto, mas os testes são deliberados: um `bus_dmamap_sync` esquecido ou uma máscara de interrupção não configurada pode produzir corrupção silenciosa que só aparece no quinto ou sexto ciclo de suspend-resume.
- **Leitura mais todos os laboratórios e desafios**: quinze a vinte horas ao longo de quatro ou cinco sessões, incluindo o laboratório que estresa o driver com ciclos repetidos de suspend-resume, o laboratório que força uma falha intencional pós-resume e a depura, e o material de desafio que estende o driver com detecção de ociosidade em tempo de execução.

As Seções 3 e 4 são as mais densas. Se a disciplina de quiesce ou a ordenação do resume parecer opaca na primeira leitura, isso é normal. Pause, releia o diagrama correspondente, execute o exercício equivalente no dispositivo simulado e continue quando a estrutura tiver se assentado. Gerenciamento de energia é um daqueles tópicos em que um modelo mental funcional compensa repetidamente; vale a pena construí-lo com calma.

### Pré-requisitos

Antes de começar este capítulo, confirme:

- O código-fonte do seu driver corresponde ao Estágio 4 do Capítulo 21 (`1.4-dma`). O ponto de partida pressupõe todos os primitivos do Capítulo 21: a tag e o buffer DMA, o rastreador `dma_in_flight`, a variável de condição `dma_cv` e o caminho de teardown limpo.
- Sua máquina de laboratório roda FreeBSD 14.3 com `/usr/src` em disco, correspondendo ao kernel em execução.
- Um kernel de depuração com `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB` e `KDB_UNATTENDED` está construído, instalado e inicializando corretamente. A opção `WITNESS` é especialmente valiosa para o trabalho de suspend e resume, porque os caminhos de código executam sob locks não óbvios e a maquinaria de energia do kernel aperta vários invariantes durante a transição.
- `bhyve(8)` ou `qemu-system-x86_64` está disponível. Os laboratórios do Capítulo 22 funcionam em qualquer um dos dois alvos. O teste de suspend-resume não requer hardware real; `devctl suspend` e `devctl resume` permitem acionar os métodos de energia do driver diretamente, sem envolver o ACPI.
- Os comandos `devinfo(8)`, `sysctl(8)`, `pciconf(8)`, `procstat(1)`, `devctl(8)`, `acpiconf(8)` (se estiver em hardware real com ACPI) e `zzz(8)` estão no seu PATH.

Se algum item acima estiver pendente, resolva agora. Gerenciamento de energia, assim como DMA, é um tópico onde fraquezas latentes aparecem sob estresse. Um driver que quase funciona no detach frequentemente quebra no suspend; um driver que lida com um suspend limpo muitas vezes falha no décimo ciclo porque um contador transbordou, um map vazou ou uma variável de condição foi reinicializada incorretamente. O kernel de depuração com `WITNESS` habilitado é o que revela esses erros durante o desenvolvimento.

### Como Aproveitar ao Máximo Este Capítulo

Quatro hábitos darão resultado rapidamente.

Primeiro, deixe `/usr/src/sys/kern/device_if.m` e `/usr/src/sys/kern/subr_bus.c` nos seus favoritos. O primeiro arquivo define os métodos `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN` e `DEVICE_QUIESCE`; o segundo contém `bus_generic_suspend`, `bus_generic_resume`, `device_quiesce` e a maquinaria devctl que transforma requisições do espaço do usuário em chamadas de método. Ler os dois uma vez no início da Seção 2 e retornar a eles à medida que você trabalha cada seção é a coisa mais útil que você pode fazer para ganhar fluência.

Segundo, mantenha três exemplos de drivers reais à mão: `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/xl/if_xl.c` e `/usr/src/sys/dev/virtio/block/virtio_blk.c`. Cada um ilustra um estilo diferente de gerenciamento de energia. `if_re.c` é um driver de rede completo com suporte a wake-on-LAN, salvamento e restauração do espaço de configuração e um caminho de resume cuidadoso. `if_xl.c` é mais simples: seu `xl_shutdown` apenas chama `xl_suspend`, e `xl_suspend` para o chip e configura o wake-on-LAN. `virtio_blk.c` é mínimo: `vtblk_suspend` define um flag e quiesce a fila, `vtblk_resume` limpa o flag e reinicia o I/O. O Capítulo 22 fará referência a cada um deles no momento em que seu padrão melhor ilustrar o que o driver `myfirst` está fazendo.

Terceiro, digite as alterações manualmente e exercite cada estágio com `devctl suspend` e `devctl resume`. Gerenciamento de energia é onde pequenas omissões produzem falhas características: uma máscara de interrupção esquecida causa um resume travado; um `bus_dmamap_sync` esquecido causa dados desatualizados; uma variável de estado esquecida faz o driver acreditar que uma transferência ainda está em andamento. Digitar com cuidado e executar o script de regressão após cada estágio expõe esses erros no momento em que acontecem.

Quarto, após terminar a Seção 4, releia o caminho de detach do Capítulo 21. A disciplina de quiesce da Seção 3 e a disciplina de restore da Seção 4 compartilham infraestrutura com o detach do Capítulo 21: `callout_drain`, `taskqueue_drain`, `bus_dmamap_sync`, `pci_release_msi`. Ver suspend-resume e attach-detach lado a lado é o que torna as diferenças visíveis. Suspend não é detach; resume não é attach; mas eles usam os mesmos blocos de construção, compostos de formas diferentes, e ver essa composição em duas passagens vale a meia hora extra.

### Roteiro pelo Capítulo

As seções em ordem são:

1. **O Que É Gerenciamento de Energia em Drivers de Dispositivo?** A visão geral: por que um driver se preocupa com energia, como o gerenciamento de energia em nível de sistema difere do nível de dispositivo, o que os S-states do ACPI e os D-states do PCI significam, o que os estados de link PCIe e o ASPM acrescentam, e como são as fontes de wake nos sistemas que o leitor mais provavelmente terá. Conceitos primeiro, APIs depois.
2. **A Interface de Gerenciamento de Energia do FreeBSD.** Os métodos kobj: `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`. A ordem em que o kernel os entrega. O helper `bus_generic_suspend`, o caminho `pci_suspend_child` e a interação com ACPI. O primeiro código em execução: Estágio 1 do driver do Capítulo 22 (`1.5-power-stage1`), com handlers esqueleto que apenas registram logs.
3. **Quiescing um Dispositivo com Segurança.** Parar a atividade antes de uma transição de energia. Mascarar interrupções, parar a submissão de DMA, drenar trabalho em andamento, drenar callouts e taskqueues, descarregar buffers sensíveis a políticas. O Estágio 2 (`1.5-power-stage2`) transforma os esqueletos em um quiesce real.
4. **Restaurando o Estado no Resume.** Reinicializar o dispositivo a partir do estado salvo. O que o save/restore PCI faz por você e o que não faz. Reabilitar o bus-master, restaurar registradores do dispositivo, rearmar interrupções, validar identidade, tratar reset do dispositivo. O Estágio 3 (`1.5-power-stage3`) adiciona o caminho de resume que corresponde ao quiesce do Estágio 2.
5. **Tratando o Gerenciamento de Energia em Tempo de Execução.** Economia de energia em dispositivo ocioso. Detectar ociosidade. Colocar o dispositivo em D3 e trazê-lo de volta para D0 sob demanda. Latência versus energia. A seção opcional do capítulo, mas com um esboço prático com o qual você pode experimentar.
6. **Interagindo com o Framework de Energia.** Testando transições a partir do espaço do usuário. `acpiconf -s 3` e `zzz` para suspend completo do sistema. `devctl suspend` e `devctl resume` para suspend por dispositivo. `devinfo -v` para observar estados de energia. O script de regressão que envolve tudo isso.
7. **Depurando Problemas de Gerenciamento de Energia.** Os modos de falha característicos: dispositivo congelado, interrupções perdidas, DMA inválido após resume, PME# wake ausente, reclamações do WITNESS. Os padrões de depuração que encontram cada um deles.
8. **Refatorando e Versionando Seu Driver com Gerenciamento de Energia.** A divisão final em `myfirst_power.c` e `myfirst_power.h`, o Makefile atualizado, o documento `POWER.md` e o incremento de versão. Estágio 4 (`1.5-power`).

Após as oito seções, vêm um walkthrough estendido do código de gerenciamento de energia de `if_re.c`, várias análises mais profundas dos estados de sleep do ACPI, estados de link PCIe, fontes de wake e a interface de espaço do usuário devctl, um conjunto de laboratórios práticos, um conjunto de exercícios desafio, uma referência de resolução de problemas, um Encerrando que fecha a história do Capítulo 22 e abre a do Capítulo 23, uma ponte, e o usual material de referência rápida e glossário ao final do capítulo. O material de referência foi pensado para ser relido à medida que você trabalha os próximos capítulos; o vocabulário do Capítulo 22 (suspend, resume, quiesce, shutdown, D0, D3, ASPM, PME#) é a base que todo driver FreeBSD de produção compartilha.

Se esta é sua primeira leitura, leia linearmente e faça os laboratórios em ordem. Se você está revisitando, as Seções 3, 4 e 7 são independentes e fazem boas leituras em uma única sessão.



## Seção 1: O Que É Gerenciamento de Energia em Drivers de Dispositivo?

Antes do código, a imagem. A Seção 1 ensina o que gerenciamento de energia significa no nível que o driver enxerga: as camadas do sistema que cooperam para economizar energia, os estados de sleep e os estados de energia do dispositivo que essas camadas definem, a maquinaria em nível de link PCIe que acontece abaixo da visibilidade do driver, e as fontes de wake que trazem o sistema de volta. Um leitor que termina a Seção 1 pode ler o restante do capítulo com o vocabulário de gerenciamento de energia do ACPI e do PCI como objetos concretos, e não como acrônimos vagos.

### Por Que o Driver Precisa se Preocupar com Energia

O leitor passou os seis capítulos anteriores ensinando um driver a se comunicar com um dispositivo. Cada capítulo acrescentou uma capacidade: registradores mapeados em memória, backends simulados, PCI real, interrupções, interrupções com múltiplos vetores, DMA. Em todos eles, o dispositivo estava sempre pronto para responder. O BAR estava sempre mapeado. O vetor de interrupção estava sempre armado. O motor de DMA estava sempre ativo. Essa suposição é conveniente para o ensino, e é também a suposição que o driver faz durante a operação normal. Não é, porém, a suposição que o usuário faz. O usuário assume que, quando o laptop entra em suspensão, a bateria se descarrega lentamente; que quando o NVMe está ocioso, ele se resfria; que quando a placa Wi-Fi não tem nada a transmitir, ela não está consumindo watts da fonte de alimentação. Essas suposições fazem parte de uma engenharia de plataforma real, e o driver é uma das camadas que precisa cooperar para que o sistema funcione corretamente.

Cooperar significa reconhecer que o estado de energia do dispositivo pode mudar de forma inesperada, mesmo enquanto o driver está no controle. A mudança é sempre anunciada: o kernel chama um método no driver para informá-lo sobre o que está prestes a acontecer. Mas o anúncio só funciona se o driver o tratar corretamente. Um driver que ignora o anúncio deixa o dispositivo em um estado inconsistente, e o custo aparece de formas específicas às transições de energia: um laptop que não acorda, um servidor cujo controlador RAID se recusa a responder após um reset em nível de dispositivo, um dock USB que perde conectividade ao fechar a tampa. Cada uma dessas falhas remete a um driver que tratou um evento de energia como opcional quando a corretude da plataforma assumia que ele era obrigatório.

O que está em jogo não é apenas o consumo em modo ocioso. Um driver que não desativa o DMA antes de o sistema executar o suspend pode corromper a memória no exato momento em que a CPU para. Um driver que não mascara as interrupções antes de o barramento entrar em um estado de energia mais baixo pode causar eventos de wake espúrios. Um driver que não restaura sua configuração após o resume pode ler zeros de um registrador que antes continha um endereço válido. Cada um desses casos é um bug do kernel cujo sintoma é relatado pelo usuário como "às vezes meu computador não acorda". A disciplina do Capítulo 22 é o que previne essa classe de bugs.

### Gerenciamento de Energia em Nível de Sistema Versus em Nível de Dispositivo

Duas palavras que soam parecidas descrevem coisas diferentes. Vale fixá-las agora, porque o capítulo usa as duas e a distinção importa ao longo de todo o texto.

**Gerenciamento de energia em nível de sistema** é uma transição do computador inteiro. O usuário pressiona o botão de energia, fecha a tampa ou executa `shutdown -p now`. O kernel percorre a árvore de dispositivos, pede a cada driver que suspenda o seu dispositivo e, em seguida, ou estaciona a CPU num estado de baixo consumo (S1, S3), escreve o conteúdo da memória no disco (S4) ou desliga a energia (S5). Cada driver do sistema participa da transição. Se algum driver recusar, toda a transição falha; o kernel imprime uma mensagem como `DEVICE_SUSPEND(foo0) failed: 16`, o sistema permanece ligado e o usuário vê um laptop cuja tela ficou escura por meio segundo e depois voltou.

**Gerenciamento de energia em nível de dispositivo** é uma transição de um único dispositivo. O kernel decide (ou recebe instrução via `devctl suspend`) que um dispositivo específico pode ser colocado num estado de menor consumo, independentemente de qualquer outro dispositivo. A NIC PCIe, por exemplo, pode ir para D3 quando seu link estiver ocioso por alguns segundos, e retornar para D0 quando um pacote chegar. O sistema inteiro permanece em S0 durante todo o processo. Os demais dispositivos continuam funcionando. O usuário não percebe nada além de um leve aumento na latência do primeiro pacote após um período de ociosidade, porque a NIC precisou acordar a partir do D3.

As transições em nível de sistema e em nível de dispositivo usam os mesmos métodos do driver. `DEVICE_SUSPEND` é chamado tanto numa transição S3 completa (em que todos os dispositivos suspendem juntos) quanto num `devctl suspend myfirst0` direcionado (em que apenas o dispositivo `myfirst0` suspende). O driver geralmente não distingue os dois casos; a mesma disciplina de quiescência funciona para ambos. O que difere é o contexto em torno da chamada: uma suspensão em nível de sistema também desabilita todas as CPUs exceto a CPU de boot, interrompe a maioria das threads do kernel e espera que cada driver conclua rapidamente; uma suspensão por dispositivo deixa o restante do sistema em execução e é chamada a partir do contexto normal do kernel. O driver, na maior parte das vezes, não precisa se preocupar com isso. Ele precisa, porém, estar ciente de que os dois contextos existem, porque um driver que só testa a suspensão por dispositivo pode deixar passar um bug que só aparece numa suspensão completa do sistema, e vice-versa.

O Capítulo 22 exercita os dois caminhos. Os laboratórios usam `devctl suspend` e `devctl resume` para iteração rápida, pois levam milissegundos e não envolvem ACPI. O teste de integração usa `acpiconf -s 3` (ou `zzz`) para exercitar o caminho completo pela camada ACPI e pela hierarquia de barramentos. Um driver que passa nos dois testes tem muito mais probabilidade de estar correto em produção do que um que passa apenas no primeiro.

### Estados de Sleep do Sistema ACPI (S0 a S5)

Nos laptops e servidores x86 com que a maioria dos leitores trabalhará, o estado de energia do sistema é descrito pela especificação ACPI como um pequeno conjunto de letras e números chamados de **S-states**. Cada S-state define um nível distinto de "o quanto do sistema ainda está funcionando". O driver não escolhe um S-state (quem escolhe é o usuário, o BIOS ou a política do kernel), mas precisa saber quais existem e o que cada um implica para o dispositivo.

**S0** é o estado de operação. A CPU está em execução, a RAM está alimentada e todos os dispositivos estão no estado de energia de que precisam. Tudo o que o leitor fez até agora foi feito em S0. É o estado em que o sistema inicializa e do qual só sai quando uma solicitação de sleep é feita.

**S1** é chamado de "standby" ou "light sleep". A CPU para de executar, mas seus registradores e caches são preservados, a RAM permanece alimentada e a maioria dos dispositivos fica em D0 ou D1. A saída desse estado é rápida (normalmente menos de um segundo). No hardware moderno, S1 raramente é usado, porque S3 é mais eficiente em termos de energia e quase tão rápido para acordar. O FreeBSD suporta S1 se a plataforma o anunciar; a maioria das plataformas modernas não o faz.

**S2** é um estado raramente implementado, situado entre S1 e S3. Na maioria das plataformas não é anunciado, e quando é, o FreeBSD o trata de forma semelhante ao S1. O capítulo não retorna ao S2.

**S3** é o "suspend to RAM", também conhecido como "standby" ou "sleep" na linguagem do usuário. A CPU para, o contexto da CPU é perdido e precisa ser salvo antes da transição, a RAM permanece alimentada pelo mecanismo de auto-refresh, e a maioria dos dispositivos vai para D3 ou D3cold. A saída desse estado leva de um a três segundos num laptop típico. É o estado em que o usuário entra ao fechar a tampa do laptop. Num servidor, S3 é o estado que `acpiconf -s 3` ou `zzz` produz. O teste principal do Capítulo 22 é uma transição S3.

**S4** é o "suspend to disk" ou "hibernate". O conteúdo completo da RAM é gravado numa imagem em disco, a energia é removida e o sistema volta a funcionar lendo essa imagem no próximo boot. No FreeBSD, o suporte a S4 historicamente foi parcial no x86 (a imagem de memória pode ser produzida, mas o caminho de restauração não é tão polido quanto no Linux ou no Windows). Do ponto de vista do driver, S4 se parece com S3 com um passo extra ao final: o driver suspende exatamente como faria para S3. A diferença é invisível para o driver.

**S5** é o "soft off". O sistema está desligado; apenas o circuito de wake-up (botão de energia, wake-on-LAN) está recebendo energia. Do ponto de vista do driver, S5 é semelhante a um desligamento do sistema; o método que é chamado é `DEVICE_SHUTDOWN`, não `DEVICE_SUSPEND`.

No hardware real, o leitor pode ver quais estados de sleep a plataforma suporta com:

```sh
sysctl hw.acpi.supported_sleep_state
```

Um laptop típico imprime algo como:

```text
hw.acpi.supported_sleep_state: S3 S4 S5
```

Um servidor pode imprimir apenas `S5`, porque a suspensão por ACPI raramente é relevante em máquinas de datacenter. Uma VM pode imprimir combinações variadas dependendo do hypervisor. O `sysctl hw.acpi.s4bios` informa se S4 é assistido pelo BIOS (raramente é em sistemas modernos). O `sysctl hw.acpi.sleep_state` permite ao leitor entrar manualmente num estado de sleep; `acpiconf -s 3` é o wrapper de linha de comando preferido.

Para os fins do Capítulo 22, o leitor precisa estar ciente de S3 (o caso mais comum) e S5 (o caso de desligamento). S1 e S2 são tratados pelo driver da mesma forma que S3; S4 é um superconjunto de S3. O capítulo trata S3 como o exemplo canônico ao longo de todo o texto.

### Estados de Energia de Dispositivos PCI e PCIe (D0 a D3cold)

O estado de energia de um dispositivo é descrito pelos **D-states**, que a especificação PCI define de forma independente do S-state do sistema. Os métodos do driver controlam mais diretamente o D-state do seu dispositivo, e vale entender cada estado em detalhes.

**D0** é o estado totalmente ativo. O dispositivo está alimentado, com clock, acessível através de seu espaço de configuração e BARs, e capaz de realizar qualquer operação que o driver solicite. Todo o trabalho dos Capítulos 16 a 21 foi feito com o dispositivo em D0. `PCI_POWERSTATE_D0` é a constante simbólica em `/usr/src/sys/dev/pci/pcivar.h`.

**D1** e **D2** são estados intermediários de baixo consumo que a especificação PCI define, mas sem restrições rígidas. Um dispositivo em D1 ainda tem seus registradores de configuração acessíveis e pode responder a algum I/O; um dispositivo em D2 pode ter perdido mais contexto. Esses estados raramente são usados em PCs modernos porque o salto de D0 para D3 geralmente é preferível em termos de economia de energia. A maioria dos drivers não se preocupa com D1 e D2.

**D3hot** é o estado de baixo consumo em que o dispositivo está efetivamente desligado, mas o barramento ainda está alimentado, o espaço de configuração ainda pode ser acessado (leituras retornam principalmente zero ou a configuração preservada), e o dispositivo pode gerar um sinal PME# se estiver configurado para isso. A maioria dos dispositivos entra em D3hot durante a suspensão.

**D3cold** é o estado de consumo ainda mais baixo em que o próprio barramento perdeu energia. O dispositivo não pode ser acessado de forma alguma; leituras do seu espaço de configuração retornam tudo-uns. A única saída do D3cold é a plataforma restaurar a energia, o que normalmente acontece sob controle da plataforma (e não do driver). D3cold é comum durante S3 e S4 em nível de sistema completo.

Quando um driver chama `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`, a camada PCI transiciona o dispositivo do seu estado atual para D3 (especificamente D3hot; a transição para D3cold é responsabilidade da plataforma). Quando o driver chama `pci_set_powerstate(dev, PCI_POWERSTATE_D0)`, a camada PCI traz o dispositivo de volta para D0.

A camada PCI no FreeBSD também gerencia automaticamente essas transições durante a suspensão e o resume do sistema. A função `pci_suspend_child`, que o driver do barramento PCI registra para `bus_suspend_child`, chama primeiro o `DEVICE_SUSPEND` do driver e, em seguida (se o sysctl `hw.pci.do_power_suspend` for verdadeiro, o que é o padrão), transiciona o dispositivo para D3. No resume, `pci_resume_child` transiciona o dispositivo de volta para D0, restaura o espaço de configuração a partir de uma cópia em cache, limpa qualquer sinal PME# pendente e, em seguida, chama o `DEVICE_RESUME` do driver. O leitor pode observar o comportamento com:

```sh
sysctl hw.pci.do_power_suspend
sysctl hw.pci.do_power_resume
```

Ambos têm valor padrão 1. Um leitor que queira desabilitar a transição automática de D-state (para depuração, ou para um dispositivo que se comporta mal em D3) pode defini-los como 0; nesse caso, o `DEVICE_SUSPEND` e o `DEVICE_RESUME` do driver são executados, mas o dispositivo permanece em D0 durante toda a transição.

Para o Capítulo 22, os fatos importantes são:

- O método `DEVICE_SUSPEND` do driver é executado antes da mudança de D-state. O driver faz a quiescência enquanto o dispositivo ainda está em D0.
- O método `DEVICE_RESUME` do driver é executado depois que o dispositivo foi retornado a D0. O driver restaura enquanto o dispositivo está acessível.
- O driver geralmente não chama `pci_set_powerstate` diretamente durante a suspensão e o resume. A camada PCI cuida disso automaticamente.
- O driver geralmente não chama `pci_save_state` e `pci_restore_state` diretamente. A camada PCI cuida dessas operações automaticamente, por meio de `pci_cfg_save` e `pci_cfg_restore`.
- O driver *de fato* salva e restaura seu próprio estado específico do dispositivo: conteúdo de registradores locais ao BAR que o hardware pode ter perdido, campos da softc que rastreiam a configuração em tempo de execução, valores de máscara de interrupção. A camada PCI não tem conhecimento dessas informações.

O limite está onde o espaço de configuração PCI termina e os registradores acessados via BAR começam. A camada PCI protege o primeiro; o driver protege o segundo.

### Estados de Link PCIe e Active-State Power Management (ASPM)

Numa camada abaixo do D-state do dispositivo está o próprio link PCIe. Um link entre um root complex e um endpoint pode estar em um dos vários **L-states**, e as transições entre L-states acontecem automaticamente quando o tráfego no link é suficientemente baixo.

**L0** é o estado de link totalmente ativo. Os dados fluem normalmente. A latência está no seu mínimo. É o estado em que o link se encontra sempre que o dispositivo está ativo.

**L0s** é um estado de baixo consumo que o link entra quando fica ocioso por alguns microssegundos. O transmissor desliga seus drivers de saída em um dos lados; o link é bidirecional, portanto o L0s do outro lado é independente. A recuperação do L0s leva centenas de nanossegundos. É uma economia de energia barata que a plataforma pode realizar automaticamente quando o tráfego é em rajadas.

**L1** é um estado de baixo consumo mais profundo que o link entra após um período de ociosidade mais longo (dezenas de microssegundos). Ambos os lados desligam mais de seus circuitos da camada física. A recuperação leva microssegundos. É usado durante períodos de baixa carga quando a penalidade de latência é aceitável.

**L1.1** e **L1.2** são refinamentos do PCIe 3.0 e versões posteriores de L1 que adicionam um desligamento de energia ainda maior, permitindo corrente de ociosidade ainda menor ao custo de um wake-up mais lento.

**L2** é um estado de link quase desligado, usado durante D3cold e S3; o link é efetivamente desligado e o wake-up requer uma renegociação completa. O driver normalmente não gerencia L2 diretamente; ele é um efeito colateral do dispositivo entrar em D3cold.

O mecanismo que controla as transições entre L0 e L0s/L1 é chamado de **Active-State Power Management (ASPM)**. O ASPM é um recurso por link, configurado por meio dos registradores de capacidade PCIe em ambas as extremidades do link. Ele pode ser habilitado, desabilitado ou restrito apenas ao L0s por política da plataforma. No FreeBSD, o ASPM normalmente é controlado pelo firmware por meio do ACPI (o método `_OSC` indica ao sistema operacional quais capacidades ele deve gerenciar); o kernel não questiona a política do firmware a menos que seja explicitamente instruído a fazê-lo.

Para o Capítulo 22 e para a maioria dos drivers FreeBSD, o ASPM é uma preocupação da plataforma, não do driver. O driver não configura o ASPM; a plataforma o faz. O driver não precisa salvar nem restaurar o estado do ASPM durante o ciclo de suspensão; a camada PCI cuida dos registradores de capacidade PCIe como parte do salvamento e restauração automáticos do espaço de configuração. Um driver que precise desabilitar o ASPM para um dispositivo específico (por exemplo, porque o dispositivo possui uma errata conhecida que torna o L0s inseguro) pode fazer isso lendo e escrevendo diretamente no registrador Link Control do PCIe, mas isso é raro e específico.

O leitor não precisa adicionar código ASPM ao driver `myfirst`. É suficiente estar ciente de que os L-states existem, que eles transitam automaticamente com base no tráfego, que o D-state do driver e o L-state do link são relacionados mas distintos, e que a plataforma é responsável pela configuração do ASPM. Se um driver futuro com o qual o leitor trabalhar tiver um datasheet que especifique erratas de ASPM, o leitor já saberá onde procurar.

### A Anatomia de um Ciclo de Suspend-Resume

Juntando todas as peças, um ciclo completo de suspend-resume do sistema é assim, do ponto de vista do driver, acompanhando o driver `myfirst` durante uma transição S3:

1. O usuário fecha a tampa do notebook. O driver do botão ACPI (`acpi_lid`) detecta o evento e, com base na política do sistema, dispara uma requisição de sleep para o estado S3.
2. O kernel inicia a sequência de suspend. Daemons do userland são pausados; o kernel congela threads não essenciais.
3. O kernel percorre a árvore de dispositivos e chama `DEVICE_SUSPEND` em cada dispositivo, em ordem reversa de filhos. O driver do barramento PCI executa `bus_suspend_child`, que chama o método `device_suspend` do driver `myfirst`.
4. O método `device_suspend` do driver `myfirst` é executado. Ele mascara as interrupções no dispositivo, para de aceitar novas requisições de DMA, aguarda a conclusão de qualquer DMA em andamento, drena sua fila de tarefas, registra a transição em log e retorna 0 para indicar sucesso.
5. A camada PCI registra que o suspend do `myfirst` foi bem-sucedido. Ela chama `pci_cfg_save` para guardar em cache o espaço de configuração PCI. Se `hw.pci.do_power_suspend` for 1 (o padrão), ela transiciona o dispositivo para D3hot via `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`.
6. Mais acima na árvore, o próprio barramento PCI, a ponte host e eventualmente a plataforma passam por suas próprias chamadas a `DEVICE_SUSPEND`. O ACPI arma seus eventos de wake. A CPU entra no estado de baixo consumo correspondente a S3. O subsistema de memória entra em self-refresh. O link PCIe vai para L2 ou estado equivalente.
7. O tempo passa. Na escala que o driver observa, nenhum tempo se passa; o kernel não está em execução.
8. O usuário abre a tampa. O circuito de wake da plataforma acorda a CPU. O ACPI realiza os passos iniciais de resume: o contexto da CPU é restaurado, a memória é reativada, o firmware da plataforma reinicializa o que precisa ser reinicializado.
9. A sequência de resume do kernel é iniciada. Ela percorre a árvore de dispositivos em ordem direta, chamando `DEVICE_RESUME` em cada dispositivo.
10. Para o `myfirst`, o driver do barramento PCI executa `bus_resume_child`, que transiciona o dispositivo de volta para D0 via `pci_set_powerstate(dev, PCI_POWERSTATE_D0)`. Em seguida, chama `pci_cfg_restore` para gravar o espaço de configuração em cache de volta no dispositivo, e limpa qualquer sinal PME# pendente com `pci_clear_pme`. Por fim, chama o método `device_resume` do driver.
11. O método `device_resume` do driver é executado. O dispositivo está em D0, seu espaço de configuração foi restaurado e seus registradores BAR estão com valores zero ou padrão. O driver reativa o bus-mastering se necessário, grava de volta seus registradores específicos do dispositivo a partir do estado salvo, rearma a máscara de interrupção e marca o dispositivo como resumido. Retorna 0.
12. A sequência de resume do kernel continua subindo pela árvore. As threads do userland são descongeladas. O usuário vê um sistema funcionando, normalmente em um a três segundos.

Cada passo em que o driver tem trabalho a fazer é o passo 4 e o passo 11. Todo o resto é parte da maquinaria genérica da plataforma ou do kernel. O trabalho do driver é acertar esses dois passos e entender o suficiente sobre os passos ao redor deles para interpretar o comportamento que observa.

### Fontes de Wake

Um dispositivo que está suspenso pode ser a razão pela qual o sistema acorda. A forma como isso acontece depende do barramento:

- Em **PCIe**, um dispositivo em D3hot pode afirmar o sinal **PME#** (Power Management Event). O root complex da plataforma traduz o PME# em um evento de wake, o método ACPI `_PRW` identifica qual GPE (General-Purpose Event) é utilizado, e o subsistema ACPI traduz o GPE em um wake a partir do estado S3. No FreeBSD, a função `pci_enable_pme(dev)` habilita a saída PME# do dispositivo; `pci_clear_pme(dev)` limpa qualquer sinal pendente. O helper `pci_has_pm(dev)` informa se o dispositivo possui a capability de gerenciamento de energia.
- Em **USB**, um dispositivo pode solicitar **remote wakeup** por meio de seu descritor USB padrão. O driver do host controller (`xhci`, `ohci`, `uhci`) traduz o wake em um sinal PME# ou equivalente upstream. O driver normalmente não lida com isso diretamente; a pilha USB cuida disso.
- Em **plataformas embarcadas**, um dispositivo pode afirmar um pino **GPIO** que a plataforma conecta à sua lógica de wake. As propriedades `interrupt-extended` ou `wakeup-source` da device-tree identificam quais pinos são fontes de wake. O framework de interrupções GPIO do FreeBSD cuida disso.
- Em **Wake-on-LAN**, um controlador de rede monitora pacotes mágicos ou correspondências de padrões enquanto está suspenso e afirma PME# quando um é detectado. Tanto o driver quanto a plataforma precisam ser configurados; `re_setwol` em `if_re.c` é um bom exemplo do lado do driver.

Para o driver `myfirst`, o dispositivo simulado não possui realmente uma fonte de wake (ele não tem existência física fora da simulação). O capítulo explica o mecanismo no ponto adequado da Seção 4, mostra o que `pci_enable_pme` faz e onde seria chamado, e deixa o disparo real do wake para os gatilhos manuais do backend de simulação. Um driver de hardware real chamaria `pci_enable_pme` em seu caminho de suspend quando wake-on-X for solicitado, e `pci_clear_pme` em seu caminho de resume para reconhecer qualquer sinal pendente.

### Exemplos do Mundo Real: Wi-Fi, NVMe, USB

Ajuda muito ancorar as ideias em dispositivos que o leitor já utilizou. Considere três deles.

Um **adaptador Wi-Fi** como os tratados pelo `iwlwifi` no Linux ou pelo `iwn` no FreeBSD é um cidadão constante do gerenciamento de energia. Em S0, ele passa a maior parte do tempo em um estado de idle de baixo consumo no próprio chip, associado ao ponto de acesso mas sem trocar pacotes ativamente; quando um pacote é detectado, ele acorda para D0 por alguns milissegundos, troca o pacote e volta ao idle. No suspend do sistema (S3), o kernel pede ao driver que salve seu estado, o driver instrui o chip a desassociar de forma limpa (ou a configurar padrões WoWLAN se o usuário quiser wake-on-wireless), e a camada PCI transiciona o chip para D3. No resume, o processo se inverte: o chip volta para D0, o driver restaura seu estado e reassocia ao ponto de acesso. O usuário percebe um atraso de um ou dois segundos após abrir a tampa antes de o Wi-Fi voltar, e esse tempo é quase inteiramente o tempo de reassociação, não o tempo de resume do driver.

Um **SSD NVMe** gerencia seus estados de energia internamente por meio de sua própria maquinaria de estados de energia (definida na especificação NVMe como estados PSx, onde PS0 é potência total e números mais altos representam menor consumo). O driver NVMe participa do suspend do sistema drenando suas filas, aguardando a conclusão dos comandos em andamento e então instruindo o controlador a entrar em um estado de baixo consumo. No resume, o driver restaura a configuração das filas, instrui o controlador a retornar para PS0, e o sistema retoma as operações de I/O em disco. Como as filas NVMe são grandes e intensamente orientadas a DMA, o caminho de suspend do NVMe é um lugar clássico onde um `bus_dmamap_sync` esquecido ou uma drenagem de fila incompleta se manifesta como corrupção de sistema de arquivos após o resume.

Um **dispositivo USB** é tratado pelo driver do host controller USB (`xhci`, tipicamente). O driver do host controller é o que implementa `DEVICE_SUSPEND` e `DEVICE_RESUME`; drivers individuais de USB (para teclados, armazenamento, áudio etc.) são notificados pelo mecanismo de suspend e resume do próprio framework USB. Um driver para um dispositivo USB raramente precisa de seu próprio método `DEVICE_SUSPEND`; o framework USB cuida da tradução.

O driver `myfirst` no Capítulo 22 usa um modelo de endpoint PCI, que é o caso mais comum e cujo contrato os outros casos especializam. Aprender o padrão PCI primeiro fornece ao leitor o que é necessário para entender o padrão Wi-Fi, o padrão NVMe e o padrão USB quando esses drivers forem analisados mais adiante.

### O Que o Leitor Ganhou

A Seção 1 é conceitual. O leitor não deve se sentir obrigado a memorizar cada detalhe de cada estado mencionado. O que o leitor deve absorver dela é:

- O gerenciamento de energia é um sistema em camadas. O ACPI define o estado do sistema. O PCI define o estado do dispositivo. O PCIe define o estado do link. O driver vê mais diretamente o estado do seu dispositivo.
- O estado de cada camada pode transicionar, e as transições se compõem. Um S3 do sistema implica D3 para cada dispositivo, o que implica L2 para cada link. Um D3 por dispositivo (de runtime PM) não implica S3 para o sistema; o sistema permanece em S0.
- O driver tem um contrato específico com as camadas PCI e ACPI. O driver é responsável por encerrar a atividade do seu dispositivo no suspend e por restaurar o estado do dispositivo no resume. A camada PCI cuida automaticamente do salvamento do espaço de configuração, das transições de estado D e da sinalização de wake via PME#. O ACPI cuida do wake em nível de sistema.
- Fontes de wake existem e são conectadas por uma cadeia específica (PME#, remote wakeup, GPIO). O driver normalmente as habilita e desabilita por meio de uma API auxiliar; ele não fala diretamente com o hardware de wake.
- O teste também é em camadas. `devctl suspend`/`devctl resume` exercita apenas os métodos do driver. `acpiconf -s 3` exercita o sistema inteiro. Um bom script de regressão usa os dois.

Com esse panorama estabelecido, a Seção 2 pode introduzir as APIs específicas do FreeBSD que o driver usa para participar desse sistema.

### Encerrando a Seção 1

A Seção 1 estabeleceu por que um driver precisa se preocupar com energia, o que significam o gerenciamento de energia em nível de sistema e em nível de dispositivo, como são os S-states do ACPI e os D-states do PCI, o que os L-states do PCIe acrescentam, como um ciclo de suspend-resume flui do ponto de vista do driver e o que são fontes de wake. Ela não mostrou nenhum código de driver; esse é o trabalho da Seção 2. O que o leitor tem agora é um vocabulário e um modelo mental: suspend é uma transição que a plataforma anuncia e o driver encerra suas atividades; a camada PCI move o dispositivo para D3; o sistema dorme; ao acordar, a camada PCI move o dispositivo de volta para D0; o driver restaura o estado.

Com esse panorama em mente, a próxima seção introduz a API concreta do FreeBSD para tudo isso: os quatro métodos kobj (`DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`), como o kernel os invoca, como eles se compõem com `bus_generic_suspend` e `pci_suspend_child`, e como a tabela de métodos do driver `myfirst` cresce para incluí-los.



## Seção 2: A Interface de Gerenciamento de Energia do FreeBSD

A Seção 1 descreveu o mundo em camadas do ACPI, PCI, PCIe e das fontes de wake. A Seção 2 estreita o foco para a interface do kernel do FreeBSD: os métodos kobj específicos que o driver implementa, a forma como o kernel os despacha e os helpers genéricos que tornam todo o esquema gerenciável. Ao final desta seção, o driver `myfirst` terá uma implementação esqueleto de gerenciamento de energia que compila, registra as transições em log e pode ser exercitada com `devctl suspend` e `devctl resume`. O esqueleto ainda não encerra DMA nem restaura estado; esse é o trabalho das Seções 3 e 4. O trabalho da Seção 2 é fazer com que o kernel chame os métodos do driver, para que o restante do capítulo tenha algo concreto sobre o qual construir.

### Os Quatro Métodos Kobj

O framework de dispositivos do FreeBSD trata um driver como uma implementação de uma interface kobj definida em `/usr/src/sys/kern/device_if.m`. Esse arquivo é uma pequena linguagem de domínio específico (regras de `make -V` o transformam em um header de ponteiros de função e wrappers), e ele define o conjunto de métodos que todo driver pode implementar. O trabalho dos Capítulos 16 a 21 preencheu os métodos comuns: `DEVICE_PROBE`, `DEVICE_ATTACH`, `DEVICE_DETACH`. O gerenciamento de energia adiciona mais quatro, todos documentados no mesmo arquivo com comentários que o leitor pode ler diretamente:

1. **`DEVICE_SUSPEND`** é chamado quando o kernel decidiu colocar o dispositivo em estado suspenso. O método é executado com o dispositivo ainda em D0 e com o driver ainda responsável por ele. O trabalho do método é parar a atividade e, se necessário, salvar qualquer estado que não será restaurado automaticamente. Retornar 0 indica sucesso. Retornar um valor diferente de zero veta o suspend.

2. **`DEVICE_RESUME`** é chamado depois que o dispositivo foi retornado para D0 no caminho de volta do suspend. O trabalho do método é restaurar qualquer estado que o hardware perdeu e retomar a atividade. Retornar 0 indica sucesso. Retornar um valor diferente de zero faz o kernel registrar um aviso; o resume não pode ser vetado de forma significativa nesse ponto, pois o restante do sistema já retornou.

3. **`DEVICE_SHUTDOWN`** é chamado durante o desligamento do sistema para que o driver deixe o dispositivo em um estado seguro antes de uma reinicialização ou do desligamento completo da máquina. Muitos drivers implementam esse método simplesmente chamando o próprio método suspend, pois as duas tarefas são semelhantes: parar o dispositivo de forma limpa. Retornar 0 indica sucesso.

4. **`DEVICE_QUIESCE`** é chamado quando o framework deseja que o driver pare de aceitar novas tarefas, mas ainda não decidiu fazer o detach. É uma forma mais suave de detach: o dispositivo ainda está anexado, os recursos ainda estão alocados, mas o driver deve recusar novas submissões e aguardar que as operações em andamento se concluam. Esse método é opcional e menos comumente implementado que os outros três. O `device_quiesce` é chamado automaticamente antes de `device_detach` pela camada devctl, de modo que um driver que implementa tanto suspend quanto quiesce costuma compartilhar código entre os dois.

O arquivo também contém implementações no-op padrão: `null_suspend`, `null_resume`, `null_shutdown`, `null_quiesce`. Um driver que não implementa um dos métodos recebe o no-op correspondente, que retorna 0 e não faz nada. É por isso que os Capítulos 16 a 21 não mencionaram explicitamente esses métodos: os no-ops estavam sendo usados silenciosamente e, para um driver cujo dispositivo permanece ligado indefinidamente e cujo detach ocorre apenas no descarregamento do módulo, os no-ops fornecem o comportamento correto para a maioria das cargas de trabalho.

O primeiro passo do Capítulo 22 é substituir esses no-ops por implementações reais.

### Adicionando os Métodos à Tabela de Métodos do Driver

O array `device_method_t` em um driver FreeBSD lista os métodos kobj que o driver implementa. O array de métodos atual do driver `myfirst` (em `myfirst_pci.c`) tem uma aparência similar a esta:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,   myfirst_pci_probe),
        DEVMETHOD(device_attach,  myfirst_pci_attach),
        DEVMETHOD(device_detach,  myfirst_pci_detach),

        DEVMETHOD_END
};
```

Adicionar gerenciamento de energia é mecanicamente simples: o driver acrescenta três (ou quatro) linhas `DEVMETHOD`. Os nomes à esquerda são os nomes dos métodos kobj de `device_if.m`; os nomes à direita são as implementações do driver. Um conjunto completo tem esta aparência:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,    myfirst_pci_probe),
        DEVMETHOD(device_attach,   myfirst_pci_attach),
        DEVMETHOD(device_detach,   myfirst_pci_detach),
        DEVMETHOD(device_suspend,  myfirst_pci_suspend),
        DEVMETHOD(device_resume,   myfirst_pci_resume),
        DEVMETHOD(device_shutdown, myfirst_pci_shutdown),

        DEVMETHOD_END
};
```

As funções `myfirst_pci_suspend`, `myfirst_pci_resume` e `myfirst_pci_shutdown` são novas; elas ainda não existem. O restante da Seção 2 mostra o que cada uma delas faz no nível de esqueleto.

### Protótipos e Valores de Retorno

Cada um dos quatro métodos tem a mesma assinatura: um valor de retorno `int` e um argumento `device_t`. O `device_t` é o dispositivo sobre o qual o método está sendo chamado, e o driver pode usar `device_get_softc(dev)` para recuperar o ponteiro para o softc.

```c
static int myfirst_pci_suspend(device_t dev);
static int myfirst_pci_resume(device_t dev);
static int myfirst_pci_shutdown(device_t dev);
static int myfirst_pci_quiesce(device_t dev);  /* optional */
```

Os valores de retorno seguem a convenção habitual do FreeBSD. Zero significa sucesso. Um valor diferente de zero é um valor errno comum que indica o que deu errado: `EBUSY` se o driver não consegue suspender porque o dispositivo está ocupado, `EIO` se o hardware reportou um erro, `EINVAL` se o driver foi chamado em um estado impossível. A reação do kernel difere conforme o método.

Para `DEVICE_SUSPEND`, um retorno diferente de zero **veta** a suspensão. O kernel aborta a sequência de suspensão e chama `DEVICE_RESUME` nos drivers que já tinham sido suspensos com sucesso, desfazendo a suspensão parcial. Esse é o mecanismo que impede o sistema de entrar em S3 enquanto um dispositivo crítico está no meio de uma operação que o driver não consegue interromper. Ele deve ser usado com parcimônia; retornar `EBUSY` em toda suspensão sempre que algo estiver acontecendo é uma maneira garantida de tornar a suspensão não confiável. Um bom driver só veta quando o dispositivo está em um estado que realmente não pode ser suspenso.

Para `DEVICE_RESUME`, um retorno diferente de zero é registrado, mas em grande parte ignorado. Quando o resume está em execução, o sistema está voltando quer o driver queira ou não. O driver deve registrar o erro, marcar o dispositivo como defeituoso para que operações de I/O subsequentes falhem de forma limpa, e retornar. Um veto no momento do resume é tarde demais para ser útil.

Para `DEVICE_SHUTDOWN`, um retorno diferente de zero é igualmente em grande parte informacional. O sistema está desligando; o driver deve tentar ao máximo deixar o dispositivo em um estado seguro, mas um shutdown com falha não é uma emergência.

Para `DEVICE_QUIESCE`, um retorno diferente de zero impede que a operação subsequente (geralmente o detach) prossiga. Um driver que retorna `EBUSY` de `DEVICE_QUIESCE` força o usuário a esperar ou a usar `devctl detach -f` para forçar o detach.

### A Ordem de Entrega dos Eventos

O kernel não chama `DEVICE_SUSPEND` em todos os drivers ao mesmo tempo. Ele percorre a árvore de dispositivos e chama o método em uma ordem específica, geralmente em **ordem inversa dos filhos** na suspensão e em **ordem direta dos filhos** no resume. Isso ocorre porque uma suspensão é mais segura quando cada dispositivo é suspenso *após* os dispositivos que dependem dele, e cada dispositivo é retomado *antes* dos dispositivos que dependem dele.

Considere uma árvore simplificada:

```text
nexus0
  acpi0
    pci0
      pcib0
        myfirst0
      pcib1
        em0
      xhci0
        umass0
```

Em uma suspensão S3, a travessia na subárvore sob `pci0` suspende `myfirst0` antes de `pcib0`, `em0` antes de `pcib1` e `umass0` antes de `xhci0`. Em seguida, `pcib0`, `pcib1` e `xhci0` são suspensos. Depois `pci0`. Depois `acpi0`. Depois `nexus0`. Cada pai é suspenso apenas após todos os seus filhos terem sido suspensos.

No resume, a ordem é invertida. `nexus0` é retomado primeiro, depois `acpi0`, depois `pci0`, depois `pcib0`, `pcib1` e `xhci0`. Cada um desses chama `pci_resume_child` em seus filhos, o que transiciona o filho de volta ao D0 antes de chamar o `DEVICE_RESUME` do driver filho. Assim, o `device_resume` de `myfirst0` é executado com `pcib0` já ativo e `pci0` já reconfigurado.

A consequência prática para o driver é que durante `DEVICE_SUSPEND` ele ainda pode acessar o dispositivo normalmente (o barramento pai ainda está ativo), e durante `DEVICE_RESUME` também pode acessar o dispositivo normalmente (o barramento pai já foi retomado antes). O driver não precisa tratar o caso extremo de um pai suspenso.

Há uma sutileza: se um barramento pai informa que seus filhos devem ser suspensos em uma ordem específica (ACPI pode fazer isso para expressar dependências implícitas), o helper genérico `bus_generic_suspend` respeita essa ordem. O driver `myfirst`, cujo pai é um barramento PCI, não precisa se preocupar com ordem além de "filhos antes do pai"; o barramento PCI não tem uma ordenação rígida entre seus dispositivos filhos.

### bus_generic_suspend, bus_generic_resume e o Barramento PCI

Um **driver de barramento** é em si um driver de dispositivo, e quando o kernel chama `DEVICE_SUSPEND` em um barramento, o barramento geralmente precisa suspender todos os seus filhos antes de entrar em repouso. Implementar isso manualmente seria repetitivo, então o kernel fornece dois helpers em `/usr/src/sys/kern/subr_bus.c`:

```c
int bus_generic_suspend(device_t dev);
int bus_generic_resume(device_t dev);
```

O primeiro itera sobre os filhos do barramento em ordem inversa e chama `BUS_SUSPEND_CHILD` em cada um. O segundo itera em ordem direta e chama `BUS_RESUME_CHILD`. Se a suspensão de qualquer filho falhar, `bus_generic_suspend` desfaz a operação retomando os filhos que já tinham sido suspensos.

Um driver de barramento típico usa esses helpers diretamente:

```c
static device_method_t mybus_methods[] = {
        /* ... */
        DEVMETHOD(device_suspend, bus_generic_suspend),
        DEVMETHOD(device_resume,  bus_generic_resume),
        DEVMETHOD_END
};
```

O driver de barramento `virtio_pci_modern` faz exatamente isso em `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`, onde `vtpci_modern_suspend` e `vtpci_modern_resume` simplesmente chamam `bus_generic_suspend(dev)` e `bus_generic_resume(dev)`, respectivamente.

O **próprio barramento PCI** faz algo mais sofisticado: seu `bus_suspend_child` é `pci_suspend_child`, e seu `bus_resume_child` é `pci_resume_child`. Esses helpers (em `/usr/src/sys/dev/pci/pci.c`) fazem exatamente o que a Seção 1 descreveu: na suspensão eles chamam `pci_cfg_save` para armazenar o espaço de configuração em cache, depois chamam o `DEVICE_SUSPEND` do driver, e então, se `hw.pci.do_power_suspend` for verdadeiro, chamam `pci_set_powerstate(child, PCI_POWERSTATE_D3)`. No resume, eles invertem a sequência: transitam de volta para D0, restauram a configuração do cache, limpam o PME# pendente e chamam o `DEVICE_RESUME` do driver.

O driver `myfirst`, que se conecta diretamente a um dispositivo PCI, não implementa métodos de barramento em si; ele é um driver folha. Seus métodos de energia são os que importam para o estado do seu próprio dispositivo. Mas o leitor deve estar ciente de que o barramento PCI já realizou trabalho em ambos os lados dos métodos do driver: na suspensão, quando o `DEVICE_SUSPEND` do driver é executado, a camada PCI já salvou a configuração; no resume, quando o `DEVICE_RESUME` do driver é executado, a camada PCI já restaurou a configuração e trouxe o dispositivo de volta ao D0.

### pci_save_state e pci_restore_state: Quando o Driver os Chama

O save e restore automáticos tratados por `pci_cfg_save`/`pci_cfg_restore` cobrem os registradores de configuração PCI padrão: as atribuições de BAR, o registrador de comando, o tamanho de linha de cache, a linha de interrupção e o estado MSI/MSI-X. Para a maioria dos drivers, isso é suficiente, e o driver não precisa chamar `pci_save_state` ou `pci_restore_state` explicitamente.

Existem, no entanto, situações em que um driver deseja salvar a configuração manualmente. A API PCI expõe duas funções helper para isso:

```c
void pci_save_state(device_t dev);
void pci_restore_state(device_t dev);
```

`pci_save_state` é um wrapper em torno de `pci_cfg_save` que armazena em cache a configuração atual. `pci_restore_state` grava a configuração em cache de volta; se o dispositivo não estiver em D0 quando `pci_restore_state` for chamado, o helper o transiciona para D0 antes de restaurar.

Um driver normalmente chama essas funções em dois cenários:

1. **Antes e depois de um `pci_set_powerstate` manual que o próprio driver inicia**, por exemplo em um helper de gerenciamento de energia em tempo de execução. Se o driver decide colocar um dispositivo ocioso em D3 enquanto o sistema está em S0, ele chama `pci_save_state`, depois `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`. Quando ele acorda o dispositivo, chama `pci_set_powerstate(dev, PCI_POWERSTATE_D0)` seguido de `pci_restore_state`.

2. **Dentro de `DEVICE_SUSPEND` e `DEVICE_RESUME`, quando o save/restore automático está desabilitado**. Alguns drivers definem `hw.pci.do_power_suspend` como 0 para dispositivos que se comportam mal em D3, e gerenciam o estado de energia por conta própria. Nesse caso, o driver também é responsável por salvar e restaurar a configuração. Esse é um padrão incomum.

O driver `myfirst` do Capítulo 22 usa o cenário 1 na Seção 5 (PM em tempo de execução), onde o driver escolhe colocar o dispositivo em D3 enquanto o sistema permanece em S0. Para suspensão do sistema, o driver não chama esses helpers diretamente; a camada PCI cuida disso.

### O Helper pci_has_pm

Nem todo dispositivo PCI tem a capacidade de gerenciamento de energia PCI. Dispositivos mais antigos e alguns de uso específico não anunciam essa capacidade, o que significa que o driver não pode contar com o funcionamento de `pci_set_powerstate` ou `pci_enable_pme`. O kernel fornece um helper para verificar isso:

```c
bool pci_has_pm(device_t dev);
```

Retorna verdadeiro se o dispositivo expõe uma capacidade de gerenciamento de energia, falso caso contrário. A maioria dos dispositivos PCIe modernos retorna verdadeiro. Drivers que querem ser robustos contra hardware incomum protegem suas chamadas relacionadas a energia:

```c
if (pci_has_pm(sc->dev))
        pci_enable_pme(sc->dev);
```

O driver Realtek em `/usr/src/sys/dev/re/if_re.c` usa esse padrão em suas funções `re_setwol` e `re_clrwol`: se o dispositivo não tiver capacidade de PM, a função retorna antecipadamente sem tentar tocar no gerenciamento de energia.

### PME#: Habilitando, Desabilitando e Limpando

Em um dispositivo que possui a capacidade de PM, o driver pode solicitar ao hardware que acione PME# quando um evento relevante de wake ocorrer. A API consiste em três funções curtas:

```c
void pci_enable_pme(device_t dev);
void pci_clear_pme(device_t dev);
/* there is no explicit pci_disable_pme; pci_clear_pme both clears pending
 * events and disables the PME_En bit. */
```

`pci_enable_pme` define o bit PME_En no registrador de status/controle de gerenciamento de energia do dispositivo, de modo que o próximo evento de gerenciamento de energia detectado pelo dispositivo faz com que ele acione PME#. `pci_clear_pme` limpa qualquer bit de status PME pendente e limpa PME_En.

Um driver que deseja habilitar wake-on-LAN, por exemplo, normalmente:

1. Configura a própria lógica de wake do dispositivo (configura filtros de padrão, define o flag de magic packet etc.).
2. Chama `pci_enable_pme(dev)` no caminho de suspensão para que o dispositivo possa de fato acionar PME#.
3. No caminho do resume, chama `pci_clear_pme(dev)` para confirmar o evento de wake.

Se `pci_enable_pme` não for chamado, o dispositivo não acionará PME# mesmo que sua própria lógica de wake seja ativada. Se `pci_clear_pme` não for chamado no resume, um bit de status PME desatualizado pode causar eventos de wake espúrios no futuro.

O driver `myfirst` não implementa wake-on-X (o dispositivo simulado não tem nada para acordar), portanto essas chamadas não aparecem no código principal do driver. A Seção 4 inclui um esboço curto mostrando onde elas ficariam em um driver real.

### Um Primeiro Esqueleto: Estágio 1

Com todo o embasamento necessário estabelecido, podemos escrever a primeira versão dos métodos suspend, resume e shutdown do `myfirst`. O Estágio 1 do driver do Capítulo 22 não faz nada de substancial; apenas registra mensagens e retorna sucesso. O objetivo é fazer o kernel chamar os métodos para que o restante do capítulo possa testar de forma progressiva.

Primeiro, adicione protótipos perto do início de `myfirst_pci.c`:

```c
static int myfirst_pci_suspend(device_t dev);
static int myfirst_pci_resume(device_t dev);
static int myfirst_pci_shutdown(device_t dev);
```

Em seguida, estenda a tabela de métodos:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,    myfirst_pci_probe),
        DEVMETHOD(device_attach,   myfirst_pci_attach),
        DEVMETHOD(device_detach,   myfirst_pci_detach),
        DEVMETHOD(device_suspend,  myfirst_pci_suspend),
        DEVMETHOD(device_resume,   myfirst_pci_resume),
        DEVMETHOD(device_shutdown, myfirst_pci_shutdown),

        DEVMETHOD_END
};
```

Depois, implemente as três funções no final do arquivo:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "suspend (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_suspend_count, 1);
        return (0);
}

static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "resume (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_resume_count, 1);
        return (0);
}

static int
myfirst_pci_shutdown(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "shutdown (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_shutdown_count, 1);
        return (0);
}
```

Adicione os campos de contador ao softc em `myfirst.h`:

```c
struct myfirst_softc {
        /* ... existing fields ... */

        uint64_t power_suspend_count;
        uint64_t power_resume_count;
        uint64_t power_shutdown_count;
};
```

Exponha-os por meio de sysctls ao lado dos contadores do Capítulo 21, na função que já adiciona a árvore sysctl do `myfirst`:

```c
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_suspend_count",
    CTLFLAG_RD, &sc->power_suspend_count, 0,
    "Number of times DEVICE_SUSPEND has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_resume_count",
    CTLFLAG_RD, &sc->power_resume_count, 0,
    "Number of times DEVICE_RESUME has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_shutdown_count",
    CTLFLAG_RD, &sc->power_shutdown_count, 0,
    "Number of times DEVICE_SHUTDOWN has been called");
```

Incremente a string de versão no Makefile:

```make
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power-stage1\"
```

Compile, carregue e teste:

```sh
cd /path/to/driver
make clean && make
sudo kldload ./myfirst.ko
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
sysctl dev.myfirst.0.power_suspend_count
sysctl dev.myfirst.0.power_resume_count
dmesg | tail -6
```

A saída esperada no `dmesg` é:

```text
myfirst0: suspend (stage 1 skeleton)
myfirst0: resume (stage 1 skeleton)
```

E os contadores devem apresentar o valor 1 cada. Caso não apresentem, uma de três coisas está errada: o driver não foi recompilado, a tabela de métodos não incluiu as novas entradas ou o `devctl` reportou um erro porque o kernel não consegue encontrar o dispositivo por esse nome.

### O Que o Esqueleto Comprova

O Estágio 1 parece trivial, mas ele prova três coisas que importam para o restante do capítulo:

1. **O kernel está entregando os métodos.** Se os contadores incrementam, o dispatch do kobj de `devctl` pelo barramento PCI até o driver `myfirst` está corretamente conectado. Cada estágio posterior se apoia nisso, e é mais fácil identificar bugs de conexão agora do que depois de adicionar o código real de quiesce.

2. **A tabela de métodos está corretamente definida.** As linhas `DEVMETHOD` com o nome do método e o ponteiro de função do driver foram digitadas corretamente, os includes de cabeçalho estão certos, e o terminador `DEVMETHOD_END` está no lugar. Um erro aqui produz um kernel panic no momento do carregamento, não uma falha sutil em tempo de execução.

3. **O driver conta as transições.** Os contadores serão úteis ao longo do capítulo como uma verificação de invariante simples. `power_suspend_count` deve sempre ser igual a `power_resume_count` quando o sistema estiver ocioso; qualquer divergência indica um bug em um dos dois métodos.

Com o esqueleto no lugar, a Seção 3 pode transformar o método suspend de uma chamada puramente de log em um quiesce real da atividade do dispositivo.

### Uma Nota sobre Detach, Quiesce e Suspend

O leitor pode se perguntar como `DEVICE_DETACH`, `DEVICE_QUIESCE` e `DEVICE_SUSPEND` se relacionam. Eles parecem similares; cada um pede ao driver que pare de fazer algo. Aqui está a distinção prática, como o kernel a impõe:

- **`DEVICE_QUIESCE`** é o mais suave. Ele pede ao driver que pare de aceitar novo trabalho e que esvazie o trabalho em andamento, mas o dispositivo permanece anexado, seus recursos permanecem alocados, e outra requisição pode reativá-lo. O kernel o chama antes de `DEVICE_DETACH` para dar ao driver a oportunidade de recusar caso o dispositivo esteja ocupado.
- **`DEVICE_SUSPEND`** fica no meio-termo. Ele pede ao driver que pare a atividade, mas mantém os recursos alocados, porque o driver vai precisar deles novamente na retomada. O estado do dispositivo é preservado (em parte pelo kernel por meio do salvamento da configuração PCI, em parte pelo driver por meio de seu próprio estado salvo).
- **`DEVICE_DETACH`** é o mais rígido. Ele pede ao driver que pare a atividade, libere todos os recursos e esqueça o dispositivo. A única forma de retornar é por meio de um novo attach.

Muitos drivers implementam `DEVICE_QUIESCE` reutilizando partes do caminho de suspend (parar interrupções, parar DMA, esvaziar filas) e `DEVICE_SHUTDOWN` chamando o método de suspend diretamente. `/usr/src/sys/dev/xl/if_xl.c` faz exatamente isso: `xl_shutdown(dev)` simplesmente chama `xl_suspend(dev)`. A relação é:

- `shutdown` ≈ `suspend` (para a maioria dos drivers que não distinguem comportamento específico de shutdown, como padrões de wake-on-LAN definidos de forma diferente)
- `quiesce` ≈ metade do `suspend` (parar a atividade, não salvar estado)
- `suspend` = quiesce + salvar estado
- `resume` = restaurar estado + desfazer quiesce
- `detach` = quiesce + liberar recursos

O Capítulo 22 implementa suspend, resume e shutdown por completo. Ele não implementa `DEVICE_QUIESCE` para o driver `myfirst`, porque o caminho de detach do Capítulo 21 já faz quiesce corretamente, e `device_quiesce` seria redundante. Um driver que quisesse permitir um estado de "parar I/O mas manter o dispositivo anexado" (por exemplo, para suportar `devctl detach -f` de forma elegante) adicionaria `DEVICE_QUIESCE` como um método separado. O capítulo menciona isso para fins de completude e segue adiante.

### O Caminho de Detach como Inspiração

O caminho de detach do Capítulo 21 é uma referência útil porque já realiza a maior parte do que o suspend precisa fazer. O caminho de detach mascara interrupções, esvazia a tarefa rx e o callout de simulação, aguarda a conclusão de qualquer DMA em andamento e chama `myfirst_dma_teardown`. O caminho de suspend fará os três primeiros passos (mascarar, esvaziar, aguardar) e pulará o último (teardown). Um driver bem estruturado fatoriza esses passos comuns em helpers compartilhados, para que ambos os caminhos usem o mesmo código.

A Seção 3 apresenta exatamente esses helpers: `myfirst_stop_io`, `myfirst_drain_workers`, `myfirst_mask_interrupts`. Cada um é uma pequena função extraída do caminho de detach do Capítulo 21. O suspend os usa sem desfazer os recursos; o detach os usa e depois desfaz. A reutilização mantém os dois caminhos corretos por construção.

### Observando o Esqueleto com WITNESS

Um leitor que construiu o kernel de depuração com `WITNESS` pode agora executar o esqueleto e observar se surgem avisos de ordem de lock. Não deve haver nenhum, porque o esqueleto não adquire nenhum lock. A Seção 3 adicionará aquisição de lock no caminho de suspend, e `WITNESS` vai notar imediatamente se a ordem discordar da ordem usada no detach. Esse é um dos benefícios de trabalhar em etapas: a linha de base está silenciosa, então qualquer aviso posterior é claramente atribuível à etapa que o introduziu.

### Encerrando a Seção 2

A Seção 2 estabeleceu a API específica do FreeBSD para gerenciamento de energia: os quatro métodos kobj (`DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`), a semântica dos seus valores de retorno, a ordem em que o kernel os entrega, os helpers genéricos (`bus_generic_suspend`, `bus_generic_resume`) que percorrem a árvore de dispositivos, e os helpers específicos de PCI (`pci_suspend_child`, `pci_resume_child`) que salvam e restauram automaticamente o espaço de configuração em torno dos métodos do driver. O esqueleto do Estágio 1 deu ao driver implementações apenas de log para os três métodos principais, adicionou contadores ao softc e verificou que `devctl suspend` e `devctl resume` os invocam corretamente.

O que o esqueleto não faz é interagir com o dispositivo de nenhuma forma. Os handlers de interrupção continuam disparando; transferências DMA ainda podem estar em andamento quando o suspend retorna; o dispositivo não fica silencioso. A Seção 3 corrige tudo isso: ela apresenta a disciplina de quiesce, fatoriza os helpers comuns de parada de I/O a partir do caminho de detach do Capítulo 21, e transforma o esqueleto do Estágio 1 em um driver do Estágio 2 que realmente coloca o dispositivo em quiesce antes de reportar sucesso.



## Seção 3: Colocando um Dispositivo em Quiesce com Segurança

A Seção 2 deu ao esqueleto do driver métodos de suspend, resume e shutdown que apenas registram logs. A Seção 3 transforma o esqueleto de suspend em um suspend real: um que para as interrupções, para o DMA, esvazia o trabalho diferido e deixa o dispositivo em um estado de quietude definido antes de retornar. Fazer isso corretamente é a parte mais difícil do gerenciamento de energia, e é onde as primitivas do Capítulo 21 compensam o investimento. Se você tem um caminho de teardown de DMA limpo, um esvaziamento de taskqueue limpo e um esvaziamento de callout limpo, você tem a maior parte do que precisa; quiesce é a arte de aplicá-los na ordem certa sem desfazer os recursos que serão necessários novamente na retomada.

### O que Quiesce Realmente Significa

A palavra "quiesce" aparece em vários lugares no FreeBSD (`DEVICE_QUIESCE`, `device_quiesce`, `pcie_wait_for_pending_transactions`), e ela significa algo específico: **levar um dispositivo a um estado em que nenhuma atividade está em andamento, nenhuma atividade pode começar e o hardware não está prestes a gerar mais interrupções ou realizar mais DMA**. O dispositivo ainda está totalmente anexado, ainda tem todos os seus recursos, ainda tem seus handlers de interrupção registrados, mas não está fazendo nada e não vai fazer nada até que algo o instrua a começar novamente.

Quiesce é diferente de detach porque detach desfaz as alocações de recursos. Quiesce também é diferente de simplesmente "definir um flag que indica que o dispositivo está ocupado, para que requisições futuras bloqueiem", porque esse flag só protege contra novo trabalho que entra no driver; ele não impede o próprio hardware nem a infraestrutura do lado do kernel (tarefas, callouts) de fazer qualquer coisa.

Um dispositivo em quiesce, no sentido do Capítulo 22, tem estas propriedades:

1. O dispositivo não está ativando, e não pode ser provocado a ativar, uma interrupção. Qualquer máscara de interrupção que o dispositivo possua está configurada para suprimir todas as fontes. O handler de interrupção, se for chamado, não tem nada a fazer.
2. O dispositivo não está realizando DMA. Qualquer transferência DMA em andamento foi concluída ou abortada. O registrador de controle do motor está em estado ocioso ou foi explicitamente reiniciado.
3. O trabalho diferido do driver foi esvaziado. Qualquer tarefa enfileirada no taskqueue foi executada ou explicitamente aguardada. Qualquer callout foi esvaziado e não vai disparar.
4. Os campos do softc do driver refletem o estado em quiesce. O flag `dma_in_flight` é false. Qualquer contador de operações em andamento que o driver mantém é zero. O flag `suspended` é true, de modo que qualquer nova requisição que o espaço do usuário ou outro driver possa enviar recebe um erro.

Somente quando todas as quatro propriedades forem verdadeiras é que o dispositivo está realmente quieto. Um driver que mascara interrupções mas esquece de esvaziar o taskqueue ainda tem uma tarefa rodando em segundo plano. Um driver que esvazia o taskqueue mas esquece o callout ainda tem um timer que pode disparar. Um driver que esvazia ambos mas esquece de parar o DMA pode ter uma transferência que grava bytes na memória depois que a CPU parou de monitorar. Cada omissão produz sua própria falha característica, e a forma mais econômica de evitar todas elas é ter uma função que aplica toda a disciplina em uma ordem conhecida.

### A Ordem Importa

Os quatro passos acima não são independentes. Eles têm uma ordem de dependência que o driver precisa respeitar, porque a infraestrutura do lado do kernel e o dispositivo interagem. Considere o que acontece se a ordem estiver errada.

**Se o DMA for parado antes de as interrupções serem mascaradas**, uma interrupção pode chegar entre a parada do DMA e o mascaramento. O filtro é executado, vê um bit de status desatualizado e agenda uma tarefa. A tarefa é executada, espera que um buffer DMA esteja preenchido, encontra dados desatualizados e pode corromper o estado interno do driver. É melhor mascarar as interrupções primeiro, para que nenhuma nova interrupção chegue enquanto a parada está em andamento.

**Se o taskqueue for esvaziado antes de o dispositivo parar de produzir interrupções**, uma nova interrupção pode disparar após o esvaziamento retornar e agendar uma tarefa em uma fila que acabou de ser esvaziada. A tarefa é executada depois, fora de sincronia com a sequência de suspend. É melhor parar as interrupções primeiro, para que nenhuma nova tarefa seja agendada.

**Se o callout for esvaziado antes de o DMA ser parado**, um motor simulado acionado por um callout pode ver seu callout desmontado enquanto uma transferência ainda está em andamento. A transferência nunca termina; `dma_in_flight` permanece true; o driver fica suspenso aguardando uma conclusão que nunca virá. É melhor parar o DMA primeiro, aguardar a conclusão e então esvaziar o callout.

A ordem segura, usada pelo caminho de detach do Capítulo 21 e adaptada para o suspend do Capítulo 22, é:

1. Marcar o driver como suspenso (definir um flag no softc para que novas requisições sejam rejeitadas).
2. Mascarar todas as interrupções no dispositivo (escrever no registrador de máscara de interrupção).
3. Parar o DMA: se houver uma transferência em andamento, abortá-la e aguardar até que chegue a um estado terminal.
4. Esvaziar o taskqueue (qualquer tarefa já em execução pode terminar; nenhuma nova tarefa começa).
5. Esvaziar quaisquer callouts (qualquer disparo em andamento pode terminar; nenhum novo disparo ocorre).
6. Verificar o invariante (`dma_in_flight == false`, `softc->busy == 0`, etc.).

Cada passo se apoia no anterior. No passo 6, o dispositivo está quieto.

### Helpers, Não Código Inline

Uma implementação ingênua colocaria todos os seis passos inline em `myfirst_pci_suspend`. Isso funciona, mas duplica código que o caminho de detach já possui, e torna ambos os caminhos mais difíceis de manter. O padrão preferido do capítulo é fatorizar os passos em pequenas funções auxiliares que ambos os caminhos chamam.

Três helpers são suficientes para cobrir toda a disciplina:

```c
static void myfirst_mask_interrupts(struct myfirst_softc *sc);
static int  myfirst_drain_dma(struct myfirst_softc *sc);
static void myfirst_drain_workers(struct myfirst_softc *sc);
```

Cada um tem uma única responsabilidade:

- `myfirst_mask_interrupts` escreve no registrador de máscara de interrupção do dispositivo para desabilitar cada vetor que o driver monitora. Após retornar, nenhuma interrupção pode chegar deste dispositivo.
- `myfirst_drain_dma` pede que qualquer transferência DMA em andamento pare (define o bit ABORT) e aguarda até que `dma_in_flight` seja false. Retorna 0 em caso de sucesso, ou um errno diferente de zero se o dispositivo não parar dentro do tempo limite.
- `myfirst_drain_workers` chama `taskqueue_drain` no taskqueue do driver e `callout_drain` no callout da simulação. Após retornar, nenhum trabalho diferido está pendente.

O caminho de suspend chama os três em ordem. O caminho de detach também chama os três, mais `myfirst_dma_teardown` e as chamadas de liberação de recursos. Os dois caminhos compartilham os passos de quiesce e diferem apenas no final.

Aqui está o ponto de entrada do quiesce que o caminho de suspend usa:

```c
static int
myfirst_quiesce(struct myfirst_softc *sc)
{
        int err;

        MYFIRST_LOCK(sc);
        if (sc->suspended) {
                MYFIRST_UNLOCK(sc);
                return (0);  /* already quiet, nothing to do */
        }
        sc->suspended = true;
        MYFIRST_UNLOCK(sc);

        myfirst_mask_interrupts(sc);

        err = myfirst_drain_dma(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "quiesce: DMA did not stop cleanly (err %d)\n", err);
                /* Do not fail the quiesce; we still have to drain workers. */
        }

        myfirst_drain_workers(sc);

        return (err);
}
```

Observe a escolha de design: `myfirst_quiesce` não desfaz em caso de falha no esvaziamento do DMA. Um DMA que não para é um problema de hardware, e o driver não pode desfazer a máscara nem remover o flag de suspend em resposta. O driver registra o problema, reporta o erro ao chamador e continua esvaziando os workers para que o restante do estado ainda seja consistente. O chamador (`myfirst_pci_suspend`) decide o que fazer com o erro.

### Implementando myfirst_mask_interrupts

Para o driver `myfirst`, mascarar interrupções significa escrever no registrador de máscara de interrupção do dispositivo. O backend de simulação do Capítulo 19 já possui um registrador `INTR_MASK` em um offset conhecido; o driver escreve todos os bits em 1 nele para desabilitar cada fonte.

```c
static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
        MYFIRST_ASSERT_UNLOCKED(sc);

        /*
         * Disable all interrupt sources at the device. After this write,
         * the hardware will not assert any interrupt vector. Any already-
         * pending status bits remain, but the filter will not be called
         * to notice them.
         */
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);

        /*
         * For real hardware: also clear any pending status bits so we
         * don't see a stale interrupt on resume.
         */
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}
```

A função não mantém o softc lock. Ela se comunica com o dispositivo apenas por meio de `CSR_WRITE_4`, que é um wrapper em torno de `bus_write_4` e não exige nenhuma disciplina de lock específica. A chamada a `MYFIRST_ASSERT_UNLOCKED` é um invariante habilitado pelo WITNESS que detecta um chamador que, por engano, mantinha o lock ao invocar esta função; ela é barata e útil.

O valor de máscara `0xFFFFFFFF` pressupõe que o registrador INTR_MASK da simulação usa semântica em que o bit 1 significa "mascarado". A simulação do Capítulo 19 adota essa convenção; um driver real deve consultar o datasheet do dispositivo. O mapa de registradores do `myfirst` está documentado em `INTERRUPTS.md`; o leitor pode conferir a convenção nesse arquivo.

Uma sutileza: em alguns dispositivos reais, mascarar interrupções impede apenas que o dispositivo sinalize novas interrupções; a interrupção atualmente ativa continua ativa até que o driver a reconheça por meio do registrador de status. É por isso que a função também limpa INTR_STATUS: para garantir que nenhum bit obsoleto permaneça ativo e possa disparar novamente após o resume. Na simulação, o registrador de status se comporta de forma semelhante, portanto a mesma escrita está correta.

### Implementando myfirst_drain_dma

Drenar o DMA é o mais delicado dos três helpers, porque precisa aguardar o dispositivo. O driver do Capítulo 21 rastreia operações DMA em andamento com `dma_in_flight` e notifica a conclusão por meio de `dma_cv`. O caminho de suspend reutiliza exatamente esses mecanismos.

```c
static int
myfirst_drain_dma(struct myfirst_softc *sc)
{
        int err = 0;

        MYFIRST_LOCK(sc);
        if (sc->dma_in_flight) {
                /*
                 * Tell the engine to abort. The abort bit produces an
                 * ERR status that the filter will translate into a task
                 * that wakes us via cv_broadcast.
                 */
                CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL,
                    MYFIRST_DMA_CTRL_ABORT);

                /*
                 * Wait up to one second for the abort to land. The
                 * cv_timedwait drops the lock while we sleep.
                 */
                err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
                if (err == EWOULDBLOCK) {
                        device_printf(sc->dev,
                            "drain_dma: timeout waiting for abort\n");
                        /*
                         * Force the state forward. The device is
                         * beyond our reach at this point; treat the
                         * transfer as failed.
                         */
                        sc->dma_in_flight = false;
                }
        }
        MYFIRST_UNLOCK(sc);

        return (err == EWOULDBLOCK ? ETIMEDOUT : 0);
}
```

A função solicita ao engine de DMA que aborte a operação, em seguida dorme na variável de condição usada pelo caminho de conclusão do Capítulo 21. Se a conclusão chegar, o filtro a confirma e enfileira uma tarefa; a tarefa chama `myfirst_dma_handle_complete`, que executa a sincronização POSTREAD/POSTWRITE, limpa `dma_in_flight` e transmite o CV. O `cv_timedwait` do caminho de suspend retorna, a função de drenagem retorna 0 e o suspend prossegue.

Se a conclusão não chegar dentro de um segundo (um segundo é um timeout generoso para um dispositivo simulado cujo callout dispara a cada poucos milissegundos), a função registra um aviso e força `dma_in_flight` para false. Esta é uma escolha defensiva: um dispositivo real que não honra o abort dentro de um segundo está com mau funcionamento, e o driver precisa continuar. Deixar `dma_in_flight` como true causaria um deadlock no suspend. O custo da limpeza defensiva é que uma transferência muito lenta poderia, em princípio, concluir após o suspend retornar, escrevendo em um buffer que o driver não espera mais estar ativo. Na simulação, isso não pode acontecer porque o callout é drenado no passo seguinte. Em hardware real, o risco é específico do hardware e um driver real adicionaria aqui uma recuperação específica do dispositivo.

O valor de retorno é 0 em uma drenagem limpa (incluindo o caso de timeout, que foi forçosamente limpo) e `ETIMEDOUT` se o chamador precisar saber que ocorreu um timeout. O caminho de suspend registra o erro mas não veta o suspend; quando a drenagem atingiu o timeout, o dispositivo está efetivamente com defeito de qualquer forma.

### Implementando myfirst_drain_workers

Drenar trabalho diferido é mais fácil porque não envolve o dispositivo. A tarefa rx e o callout da simulação têm cada um primitivas de drenagem bem conhecidas.

```c
static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
        /*
         * Drain the per-vector rx task. Any task currently running is
         * allowed to finish; no new tasks will be scheduled because
         * interrupts are already masked.
         */
        if (sc->rx_vector.has_task)
                taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);

        /*
         * Drain the simulation's DMA callout. Any fire in flight is
         * allowed to finish; no new firings will happen.
         *
         * This is a simulation-only call; a real-hardware driver would
         * omit it.
         */
        if (sc->sim != NULL)
                myfirst_sim_drain_dma_callout(sc->sim);
}
```

A função pode ser chamada com segurança com o lock liberado. `taskqueue_drain` é documentado para fazer sua própria sincronização; `callout_drain` (que `myfirst_sim_drain_dma_callout` envolve internamente) é igualmente seguro.

Uma propriedade importante de ambas as chamadas de drenagem: elas aguardam a conclusão do trabalho em execução, mas não o cancelam no meio. Uma tarefa que está no meio de `myfirst_dma_handle_complete` concluirá seu trabalho, incluindo qualquer `bus_dmamap_sync` e atualização de contador, antes que a drenagem retorne. Esse é o comportamento que desejamos: o suspend não deve interromper uma tarefa no meio, porque os invariantes da tarefa devem ser mantidos para que o caminho de resume seja correto.

### Atualizando o Método de Suspend

Com os três helpers implementados, o método de suspend é breve:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        device_printf(dev, "suspend: starting\n");

        err = myfirst_quiesce(sc);
        if (err != 0) {
                device_printf(dev,
                    "suspend: quiesce returned %d; continuing anyway\n",
                    err);
        }

        atomic_add_64(&sc->power_suspend_count, 1);
        device_printf(dev,
            "suspend: complete (dma in flight=%d, suspended=%d)\n",
            sc->dma_in_flight, sc->suspended);
        return (0);
}
```

O método de suspend não retorna o erro de quiesce ao kernel. Essa é uma decisão de política e merece explicação.

Retornar um valor diferente de zero de `DEVICE_SUSPEND` veta o suspend, o que tem um grande efeito subsequente: o kernel desfaz o suspend parcial, reporta uma falha ao usuário e deixa o sistema em S0. Para o driver do Capítulo 22, um timeout de quiesce não justifica esse nível de perturbação. O dispositivo ainda está acessível; mascarar a interrupção e marcar o flag de suspended é suficiente para impedir qualquer atividade. Um driver real poderia escolher diferentemente: um controlador de armazenamento com uma escrita pendente poderia vetar o suspend até que a escrita fosse concluída, porque perder essa escrita durante a transição corromperia o sistema de arquivos. Cada driver toma sua própria decisão.

O logging é detalhado nessa fase. A Seção 7 introduzirá a capacidade de desativar o logging detalhado (ou aumentá-lo para depuração) por meio de um sysctl. Por ora, o detalhe extra é útil ao percorrer pela primeira vez a sequência de suspend.

### Salvando o Estado de Execução que o Hardware Perde

Até agora o caminho de suspend apenas parou a atividade. Ele não salvou nenhum estado. Para a maioria dos dispositivos isso é correto: a camada PCI salva o espaço de configuração automaticamente, e os registradores de execução do dispositivo (aqueles que o driver escreve por meio dos BARs) são restaurados a partir do softc do driver no resume ou regenerados a partir do estado de software do driver. A simulação `myfirst` não tem nenhum estado local de BAR que o driver precise preservar; a simulação começa do zero no resume, e o driver escreve os registradores que precisar naquele momento.

Um driver real pode ter mais. Considere o driver `re(4)`: sua função `re_setwol` escreve registradores relacionados a wake-on-LAN no espaço de configuração respaldado por EEPROM da NIC antes do suspend. Esses valores são privados do dispositivo; a camada PCI não os conhece. Se o driver não os escrevesse durante o suspend, a NIC não saberia que deveria despertar com um magic packet, e o wake-on-LAN não funcionaria.

Para o Capítulo 22, o único estado que o driver `myfirst` salva é o valor pré-suspend da máscara de interrupção. O suspend do Estágio 2 escreve `0xFFFFFFFF` no registrador de máscara, mas o resume do Estágio 2 precisa saber qual valor estava lá antes (que determina quais vetores estão habilitados na operação normal). O driver armazena isso em um campo do softc:

```c
struct myfirst_softc {
        /* ... */
        uint32_t saved_intr_mask;
};
```

E o helper de máscara o salva:

```c
static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
        sc->saved_intr_mask = CSR_READ_4(sc, MYFIRST_REG_INTR_MASK);

        CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}
```

O caminho de resume escreverá `sc->saved_intr_mask` de volta no registrador de máscara após o dispositivo ter sido reinicializado. Este é um exemplo mínimo de salvar e restaurar estado; a Seção 4 elabora quando mostra o fluxo completo de resume.

### O Flag suspended como Invariante Visível ao Usuário

Definir `sc->suspended = true` durante o quiesce tem um segundo propósito além de suprimir novas requisições: torna o estado observável para o espaço do usuário. O driver pode expor o flag por meio de um sysctl:

```c
SYSCTL_ADD_BOOL(ctx, kids, OID_AUTO, "suspended",
    CTLFLAG_RD, &sc->suspended, 0,
    "Whether the driver is in the suspended state");
```

Após `devctl suspend myfirst0`, o leitor vê:

```sh
# sysctl dev.myfirst.0.suspended
dev.myfirst.0.suspended: 1
```

Após `devctl resume myfirst0`, o valor deve voltar a 0 (a Seção 4 conecta o caminho de resume para limpá-lo). Esta é uma forma rápida de verificar o estado do driver sem precisar inferi-lo a partir de outros contadores.

### Tratando o Caso em que Não Há DMA em Andamento

O helper `myfirst_drain_dma` trata o caso em que uma transferência está em execução ativa. Ele também deve tratar o caso muito mais comum em que nada está em andamento no momento do suspend, sem fazer nada desnecessário.

O pseudocódigo acima trata isso: o guard `if (sc->dma_in_flight)` pula completamente o abort e a espera quando o flag é false. A função retorna 0 imediatamente e o suspend prossegue.

Esse caminho é rápido: em um dispositivo ocioso, `myfirst_drain_dma` consiste em adquirir o lock, verificar o flag e liberar o lock. O custo do quiesce é dominado por `taskqueue_drain` (que faz uma viagem de ida e volta completa pela thread do taskqueue) e `callout_drain` (similar). Um suspend típico de dispositivo ocioso leva de algumas centenas de microssegundos a alguns milissegundos, dominado pelas drenagens de trabalho diferido, não pelo dispositivo.

### Testando o Estágio 2

Com o código de quiesce implementado, o teste do Estágio 2 é mais interessante. O leitor executa uma transferência, depois suspende imediatamente, e observa:

```sh
# Start with no activity.
sysctl dev.myfirst.0.dma_transfers_read
# 0

# Trigger a transfer. The transfer should complete quickly.
sudo sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.dma_transfers_read
# 1

# Now stress the path: start a transfer and immediately suspend.
sudo sysctl dev.myfirst.0.dma_test_read=1 &
sudo devctl suspend myfirst0

# Check the state.
sysctl dev.myfirst.0.suspended
# 1

sysctl dev.myfirst.0.power_suspend_count
# 1

sysctl dev.myfirst.0.dma_in_flight
# 0 (the transfer completed or was aborted)

dmesg | tail -8
```

A saída esperada do `dmesg` mostra o log do suspend, o DMA em andamento sendo abortado e a conclusão. Se `dma_in_flight` ainda for 1 após o suspend ter retornado, o abort não surtiu efeito, e o leitor deve verificar o tratamento de abort da simulação.

Em seguida, resume:

```sh
sudo devctl resume myfirst0

sysctl dev.myfirst.0.suspended
# 0 (after Section 4 is implemented)

sysctl dev.myfirst.0.power_resume_count
# 1

# Try another transfer to check the device is back.
sudo sysctl dev.myfirst.0.dma_test_read=1
dmesg | tail -4
```

A última transferência deve ser bem-sucedida; se não for, o suspend deixou o dispositivo em um estado que o resume não recuperou. A Seção 4 ensina o caminho de resume que torna isso correto.

### Uma Nota Importante sobre Locking

O código de quiesce é executado a partir do método `DEVICE_SUSPEND`, que é chamado pelo caminho de gerenciamento de energia do kernel. Esse caminho não mantém nenhum lock de driver ao chamar o método; o driver é responsável por sua própria sincronização. Os helpers desta seção seguem uma disciplina específica:

- `myfirst_mask_interrupts` não mantém nenhum lock. Ele apenas escreve registradores de hardware, que são atômicos no PCIe.
- `myfirst_drain_dma` adquire o lock do softc para ler `dma_in_flight` e usa `cv_timedwait` para dormir enquanto mantém o lock (que é o uso correto de um CV de sleep-mutex).
- `myfirst_drain_workers` não mantém nenhum lock. `taskqueue_drain` e `callout_drain` fazem sua própria sincronização e devem ser chamados sem o lock para evitar deadlock (a tarefa sendo drenada pode tentar adquirir o lock ela própria).

A sequência completa de quiesce, portanto, adquire e libera o lock múltiplas vezes: uma vez brevemente no topo de `myfirst_quiesce` para definir `suspended`, uma vez dentro de `myfirst_drain_dma` para o sleep, e nunca dentro de `myfirst_drain_workers`. Isso é intencional. Manter o lock durante `taskqueue_drain` causaria deadlock, porque a tarefa drenada adquire o mesmo lock ao iniciar.

Um leitor que executar este código sob `WITNESS` não verá nenhum aviso de ordem de lock, porque o lock só é mantido durante o sleep no CV e nenhum outro lock é adquirido durante essa janela. Se trabalho futuro adicionar mais locks ao driver (por exemplo, um lock por vetor), o código de quiesce deve continuar sendo cuidadoso sobre quais locks são mantidos em torno das chamadas de drenagem.

### Integrando com o Método de Shutdown

O método de shutdown compartilha quase toda sua lógica com o suspend. Uma implementação razoável é:

```c
static int
myfirst_pci_shutdown(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "shutdown: starting\n");
        (void)myfirst_quiesce(sc);
        atomic_add_64(&sc->power_shutdown_count, 1);
        device_printf(dev, "shutdown: complete\n");
        return (0);
}
```

A única diferença em relação ao suspend é a ausência de chamadas de salvamento de estado (o shutdown é final; não há resume para o qual salvar estado) e a ausência de verificação do valor de retorno do quiesce (o shutdown não pode ser vetado de forma significativa). Muitos drivers reais seguem o mesmo padrão; o `xl_shutdown` de `/usr/src/sys/dev/xl/if_xl.c` simplesmente chama `xl_suspend`. O driver `myfirst` pode usar qualquer um dos estilos; o capítulo prefere a versão ligeiramente mais explícita acima porque torna a intenção mais clara no código.

### Encerrando a Seção 3

A Seção 3 transformou o esqueleto do Estágio 1 em um driver do Estágio 2 que efetivamente coloca o dispositivo em quiesce no suspend. Ela introduziu a disciplina de quiesce (marcar como suspended, mascarar interrupções, parar DMA, drenar workers, verificar), dividiu os passos em três funções helper (`myfirst_mask_interrupts`, `myfirst_drain_dma`, `myfirst_drain_workers`), explicou a ordem em que devem ser executadas e por quê, mostrou como integrá-las nos métodos de suspend e shutdown e discutiu a disciplina de locking.

O que o driver do Estágio 2 ainda não faz é retornar corretamente. O método de resume ainda é o esqueleto do Estágio 1; ele registra e retorna 0 sem restaurar nada. Se o dispositivo suspenso tinha algum estado que o hardware perdeu, esse estado se foi, e uma transferência subsequente falhará. A Seção 4 corrige o caminho de resume para que um ciclo completo de suspend-resume deixe o dispositivo no mesmo estado em que estava antes.



## Seção 4: Restaurando o Estado no Resume

A Seção 3 forneceu ao driver um caminho de suspend correto. A Seção 4 escreve o resume correspondente. O resume é o complemento do suspend: tudo que o suspend parou, o resume reinicia; todo valor que o suspend salvou, o resume escreve de volta; todo flag que o suspend definiu, o resume limpa. A sequência não é um espelho exato (o resume é executado em um contexto de kernel diferente, com a camada PCI já tendo realizado trabalho, e o dispositivo em um estado diferente do que o suspend o deixou), mas os conteúdos correspondem um a um. Fazer o resume corretamente é uma questão de respeitar o contrato que a camada PCI já cumpriu parcialmente e preencher o restante.

### O que a Camada PCI Já Fez

Quando o método `DEVICE_RESUME` do kernel é chamado no driver, várias coisas já aconteceram:

1. A CPU saiu do estado S (retomou de S3 ou S4 para S0).
2. A memória foi atualizada e o kernel restabeleceu seu próprio estado.
3. O barramento pai foi retomado. Para o `myfirst`, isso significa que o driver do barramento PCI já tratou da bridge do host e do complexo raiz PCIe.
4. A camada PCI chamou `pci_set_powerstate(dev, PCI_POWERSTATE_D0)` no dispositivo, fazendo a transição do estado de baixo consumo em que ele se encontrava (tipicamente D3hot) de volta para alimentação plena.
5. A camada PCI chamou `pci_cfg_restore(dev, dinfo)`, que escreve os valores do espaço de configuração armazenados em cache (BARs, registrador de comando, tamanho da linha de cache, etc.) de volta no dispositivo.
6. A camada PCI chamou `pci_clear_pme(dev)` para limpar quaisquer bits de evento de gerenciamento de energia pendentes.
7. A configuração de MSI ou MSI-X, que faz parte do estado armazenado em cache, foi restaurada. Os vetores de interrupção do driver estão disponíveis novamente para uso.

Neste ponto, o driver do barramento PCI chama o `DEVICE_RESUME` do `myfirst`. O dispositivo está em D0, com seus BARs mapeados, sua tabela MSI/MSI-X restaurada e seu estado PCI genérico intacto. O que o driver precisa restaurar é o estado específico do dispositivo que a camada PCI desconhecia: os registradores locais do BAR que o driver escreveu durante ou após o attach.

Para a simulação do `myfirst`, os registradores locais do BAR relevantes são a máscara de interrupção (que o caminho de suspend deliberadamente configurou para mascarar tudo) e os registradores de DMA (que podem ter sido deixados em um estado abortado). O driver precisa restaurá-los para valores que reflitam a operação normal.

### A Disciplina do Resume

Um caminho de resume correto realiza quatro coisas, em ordem:

1. **Reativar o bus-mastering**, caso a restauração do configuration space não o tenha feito ou o restore automático da camada PCI tenha sido desabilitado. Isso é `pci_enable_busmaster(dev)`. No FreeBSD moderno, geralmente é redundante, mas inofensivo; caminhos de código mais antigos ou BIOSes com bugs às vezes deixam o bus-mastering desabilitado. Chamá-la de forma defensiva é barato.

2. **Restaurar qualquer estado específico do dispositivo** que o driver tenha salvo durante o suspend. Para o `myfirst`, isso significa gravar `saved_intr_mask` de volta no registrador `INTR_MASK`. Um driver real também restauraria coisas como bits de configuração específicos do fabricante, programação do DMA engine, timers de hardware, etc.

3. **Desmascarar as interrupções e limpar o flag de suspended**, para que o dispositivo possa retomar a atividade. Este é o ponto de virada: antes dele, o dispositivo ainda está quieto; depois dele, o dispositivo pode gerar interrupções e aceitar trabalho.

4. **Registrar a transição e atualizar contadores**, para fins de observabilidade e testes de regressão.

Veja como esse padrão se parece em código:

```c
static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        device_printf(dev, "resume: starting\n");

        err = myfirst_restore(sc);
        if (err != 0) {
                device_printf(dev,
                    "resume: restore failed (err %d)\n", err);
                atomic_add_64(&sc->power_resume_errors, 1);
                /*
                 * Continue anyway. By the time we're here, the system
                 * is coming back whether we like it or not.
                 */
        }

        atomic_add_64(&sc->power_resume_count, 1);
        device_printf(dev, "resume: complete\n");
        return (0);
}
```

O helper `myfirst_restore` realiza as três etapas concretas:

```c
static int
myfirst_restore(struct myfirst_softc *sc)
{
        /* Step 1: re-enable bus-master (defensive). */
        pci_enable_busmaster(sc->dev);

        /* Step 2: restore device-specific state.
         *
         * For myfirst, this is just the interrupt mask. A real driver
         * would restore more: DMA engine programming, hardware timers,
         * vendor-specific configuration, etc.
         */
        if (sc->saved_intr_mask == 0xFFFFFFFF) {
                /*
                 * Suspend saved a fully-masked mask, which means the
                 * driver had no idea what the mask should be. Use the
                 * default: enable DMA completion, disable everything
                 * else.
                 */
                sc->saved_intr_mask = ~MYFIRST_INTR_COMPLETE;
        }
        CSR_WRITE_4(sc->dev, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);

        /* Step 3: clear the suspended flag and unmask the device. */
        MYFIRST_LOCK(sc);
        sc->suspended = false;
        MYFIRST_UNLOCK(sc);

        return (0);
}
```

A função retorna 0 porque nenhuma etapa acima pode falhar na simulação `myfirst`. Um driver real verificaria os valores de retorno de suas chamadas de inicialização de hardware e propagaria quaisquer erros.

### Por que pci_enable_busmaster Importa

O bus-mastering é um bit no registrador de comando PCI que controla se o dispositivo pode emitir transações DMA. Sem ele, o dispositivo não consegue ler nem gravar na memória do host; qualquer gatilho DMA seria silenciosamente ignorado pela ponte de host PCI.

O Capítulo 18 habilitou o bus-mastering durante o attach. O restore automático do configuration space pela camada PCI grava o registrador de comando de volta ao seu valor salvo, que inclui o bit de bus-master. Portanto, em princípio, o driver não precisa chamar `pci_enable_busmaster` novamente no resume. Na prática, várias coisas podem dar errado:

- O firmware da plataforma pode reinicializar o registrador de comando como parte da ativação do dispositivo.
- O sysctl `hw.pci.do_power_suspend` pode ser 0, caso em que a camada PCI não salva nem restaura o configuration space.
- Um quirk específico do dispositivo pode limpar o bus-mastering como efeito colateral da transição de D3 para D0.

Chamar `pci_enable_busmaster` incondicionalmente no resume é uma rede de segurança de baixo custo. Vários drivers FreeBSD de produção seguem esse padrão; o caminho de resume do `if_re.c` é um exemplo. A chamada é idempotente: se o bus-mastering já estiver ativo, a chamada apenas o reafirma.

### Restaurando o Estado Específico do Dispositivo

A simulação `myfirst` não tem muito estado que o driver precise restaurar manualmente. Os registradores locais à BAR são:

- A máscara de interrupção (restaurada de `saved_intr_mask`).
- Os bits de status de interrupção (foram limpos no suspend; devem permanecer limpos até que nova atividade chegue).
- Os registradores do DMA engine (`DMA_ADDR_LOW`, `DMA_ADDR_HIGH`, `DMA_LEN`, `DMA_DIR`, `DMA_CTRL`, `DMA_STATUS`). Estes são transitórios: armazenam os parâmetros da transferência em andamento. Após o resume, nenhuma transferência está em progresso, portanto os valores não importam; a próxima transferência os sobrescreverá.

Um driver real teria mais. Considere alguns exemplos:

- Um driver de armazenamento pode ter um anel de descritores DMA cujo endereço base o dispositivo aprendeu durante o attach. Após o resume, o registrador de nível BAR que contém esse endereço base pode ter sido reinicializado; o driver precisa reprogramá-lo.
- Um driver de rede pode ter tabelas de filtros (endereços MAC, listas de multicast, tags de VLAN) programadas em registradores do dispositivo. Após o resume, essas tabelas podem estar vazias; o driver as reconstrói a partir de cópias no softc.
- Um driver de GPU pode ter estado de registrador para temporização de display, tabelas de cores e cursores de hardware. Após o resume, o driver restaura o modo ativo.

Para o `myfirst`, a máscara de interrupção é o único estado local à BAR que precisa ser restaurado. O padrão mostrado acima é o template que um driver real adaptaria ao seu dispositivo.

### Validando a Identidade do Dispositivo Após o Resume

Alguns dispositivos são completamente reinicializados durante um ciclo de suspend para D3cold. O dispositivo que retorna é funcionalmente o mesmo, mas todo o seu estado foi reinicializado como se tivesse acabado de ser ligado. Um driver que assumisse que nada mudou teria comportamento incorreto de forma silenciosa.

Um caminho de resume defensivo pode detectar isso lendo um valor de registrador conhecido e comparando com o que foi lido no momento do attach. Para um dispositivo PCI, o vendor ID e o device ID no configuration space são sempre os mesmos (a camada PCI os restaurou), mas algum registrador privado do dispositivo (um revision ID, um registrador de self-test, uma versão de firmware) pode ser verificado:

```c
static int
myfirst_validate_device(struct myfirst_softc *sc)
{
        uint32_t magic;

        magic = CSR_READ_4(sc->dev, MYFIRST_REG_MAGIC);
        if (magic != MYFIRST_MAGIC_VALUE) {
                device_printf(sc->dev,
                    "resume: device identity mismatch (got %#x, "
                    "expected %#x)\n", magic, MYFIRST_MAGIC_VALUE);
                return (EIO);
        }
        return (0);
}
```

Para a simulação `myfirst`, não existe um registrador mágico assim (a simulação não foi construída com a validação pós-resume em mente). Um leitor que queira adicionar um como desafio pode estender o backend de simulação com um registrador `MAGIC` somente leitura, e fazer o driver verificá-lo. O Lab 3 deste capítulo inclui essa opção.

Um driver real cujo dispositivo realmente sofre um reset após D3cold precisa dessa verificação, porque sem ela uma falha sutil pode ocorrer: o driver assume que a máquina de estados interna do dispositivo está no estado `IDLE`, mas após o reset a máquina de estados está na verdade no estado `RESETTING`. Qualquer comando que o driver envie é rejeitado, o driver interpreta a rejeição como uma falha de hardware e o dispositivo é marcado como quebrado. Detectar o reset explicitamente e reconstruir o estado é mais limpo.

### Detectando e Recuperando de um Reset do Dispositivo

Se a validação encontrar uma inconsistência, as opções de recuperação do driver dependem do hardware. Para a simulação `myfirst`, a resposta mais simples é registrar o evento, marcar o dispositivo como quebrado e fazer com que as operações subsequentes falhem:

```c
if (myfirst_validate_device(sc) != 0) {
        MYFIRST_LOCK(sc);
        sc->broken = true;
        MYFIRST_UNLOCK(sc);
        return (EIO);
}
```

O softc ganha um flag `broken`, e qualquer requisição do usuário verifica esse flag e falha com um erro. O caminho de detach ainda funciona (o detach sempre é bem-sucedido, mesmo em um dispositivo quebrado), então o usuário pode descarregar e recarregar o driver.

Um driver real que detecta um reset tem mais opções. Um driver de rede pode reexecutar sua sequência de attach a partir do ponto após o `pci_alloc_msi` (que foi restaurado pela camada PCI). Um driver de armazenamento pode reinicializar seu controlador usando o mesmo caminho de código que o attach utilizou. A implementação depende muito do dispositivo; o padrão é "detectar, depois fazer qualquer inicialização que o attach ainda exija".

O driver `myfirst` deste capítulo adota a abordagem mais simples: ele não implementa a detecção de reset para a simulação, e o caminho de resume não inclui a chamada de validação por padrão. O código acima é fornecido como referência para o leitor que queira estender o driver como exercício.

### Restaurando o Estado DMA

A configuração DMA do Capítulo 21 aloca uma tag, aloca memória, carrega o mapa e retém o endereço de barramento no softc. Nada disso está visível no mapa de registradores local à BAR; o DMA engine aprende o endereço de barramento somente quando o driver o grava em `DMA_ADDR_LOW` e `DMA_ADDR_HIGH` como parte do início de uma transferência.

Isso significa que o estado DMA não precisa ser restaurado no sentido de "gravar registradores". A tag, o mapa e a memória são estruturas de dados do kernel; elas sobrevivem ao suspend intactas. A próxima transferência programará os registradores DMA como parte de sua submissão normal.

O que pode precisar de restauração em um dispositivo real é:

- **O endereço base do anel de descritores DMA**, se o dispositivo mantiver um ponteiro persistente. Uma NIC real grava um registrador de endereço base uma vez no attach e aponta o dispositivo para um anel de descritores; após D3cold, esse registrador pode ter sido reinicializado e o driver deve reprogramá-lo.
- **O bit de habilitação do DMA engine**, se for separado das transferências individuais.
- **Qualquer configuração por canal** (tamanho de burst, prioridade, etc.) que seja mantida em registradores que a camada PCI não armazenou em cache.

Para o `myfirst`, nada disso se aplica. O DMA engine é programado por transferência. O resume não precisa de nenhuma restauração específica de DMA além do que a restauração genérica de estado já cobriu.

### Reativando as Interrupções

Mascarar as interrupções foi o passo 2 do suspend. Desmascarar é o passo 3 do resume. O resume do Stage 3 grava `saved_intr_mask` de volta no registrador `INTR_MASK`, que (por convenção) escreve 0 nos bits correspondentes aos vetores habilitados e 1 nos bits dos vetores desabilitados. Após a gravação, o dispositivo está pronto para afirmar interrupções nos vetores habilitados assim que houver motivo.

Há uma sutileza em relação à ordem. O caminho de resume desmascara as interrupções antes de limpar o flag `suspended`. Isso significa que uma interrupção muito inoportuna poderia chegar, chamar o filtro e encontrar `suspended == true`. O filtro se recusaria a tratá-la e retornaria `FILTER_STRAY`, o que deixaria a interrupção afirmada.

Para evitar isso, o caminho de resume adquire o lock do softc em torno da mudança de estado e faz a desmaskagem e a limpeza do flag na ordem inversa: limpa `suspended` primeiro, depois desmascara. Dessa forma, qualquer interrupção que o dispositivo gere após a limpeza da máscara enxerga `suspended == false` e é tratada normalmente.

O código no trecho anterior faz isso corretamente: `myfirst_restore` grava a máscara, depois adquire o lock, limpa o flag e libera o lock. A ordem é importante; invertê-la cria uma janela estreita onde interrupções podem ser perdidas.

### Limpeza da Fonte de Wake

Se o driver habilitou uma fonte de wake durante o suspend (`pci_enable_pme`), o caminho de resume deve limpar qualquer evento de wake pendente (`pci_clear_pme`). O helper `pci_resume_child` da camada PCI já chama `pci_clear_pme(child)` antes do `DEVICE_RESUME` do driver, portanto o driver geralmente não precisa chamá-lo novamente.

O único caso em que o driver pode querer chamar `pci_clear_pme` explicitamente é em um contexto de runtime-PM onde o driver está retomando o dispositivo enquanto o sistema permanece em S0. Nesse caso, `pci_resume_child` não foi envolvido, e o driver é responsável por limpar o status de PME por conta própria.

Um esboço hipotético para um driver com wake-on-X:

```c
static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        if (pci_has_pm(dev))
                pci_clear_pme(dev);  /* defensive; PCI layer already did this */

        /* ... rest of the resume path ... */
}
```

Para o `myfirst`, não há fonte de wake, portanto a chamada não teria utilidade; o capítulo a omite do código principal e menciona o padrão aqui para completude.

### Atualizando o Driver do Stage 3

O Stage 3 reúne tudo o que foi discutido acima em um resume funcional completo. O diff em relação ao Stage 2 é:

- `myfirst.h` ganha um campo `saved_intr_mask` (adicionado para o Stage 2) e um flag `broken`.
- `myfirst_pci.c` recebe um helper `myfirst_restore` e um `myfirst_pci_resume` reescrito.
- A versão no Makefile avança para `1.5-power-stage3`.

Construa e teste:

```sh
cd /path/to/driver
make clean && make
sudo kldunload myfirst     # unload any previous version
sudo kldload ./myfirst.ko

# Quiet baseline.
sysctl dev.myfirst.0.dma_transfers_read
# 0
sysctl dev.myfirst.0.suspended
# 0

# Full cycle.
sudo devctl suspend myfirst0
sysctl dev.myfirst.0.suspended
# 1

sudo devctl resume myfirst0
sysctl dev.myfirst.0.suspended
# 0

# A transfer after resume should work.
sudo sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.dma_transfers_read
# 1

# Do it several times to make sure the path is stable.
for i in 1 2 3 4 5; do
  sudo devctl suspend myfirst0
  sudo devctl resume myfirst0
  sudo sysctl dev.myfirst.0.dma_test_read=1
done
sysctl dev.myfirst.0.dma_transfers_read
# 6 (1 + 5)

sysctl dev.myfirst.0.power_suspend_count dev.myfirst.0.power_resume_count
# should be equal, around 6 each
```

Se os contadores divergirem (o contador de suspend não igual ao de resume) ou se `dma_test_read` começar a falhar após um suspend, algo no caminho de restauração não está colocando o dispositivo de volta em um estado utilizável. O primeiro passo de depuração é ler o `INTR_MASK` e comparar com `saved_intr_mask`; o segundo é rastrear o registrador de status do DMA engine e verificar se ele está reportando um erro.

### Interação com o Setup MSI-X do Capítulo 20

O driver `myfirst` do Capítulo 20 usa MSI-X quando disponível, com um layout de três vetores (admin, rx, tx). A configuração MSI-X reside nos registradores de capability MSI-X do dispositivo e em uma tabela do lado do kernel. O save-and-restore do configuration space pela camada PCI cobre os registradores de capability; o estado do lado do kernel não é afetado pela transição de estado D.

Isso significa que o driver `myfirst` não precisa fazer nada especial para restaurar seus vetores MSI-X. Os recursos de interrupção (`irq_res`) permanecem alocados, os cookies permanecem registrados e os bindings de CPU permanecem no lugar. Quando o dispositivo gera um vetor MSI-X no resume, o kernel o entrega ao filtro que foi registrado no momento do attach.

Um leitor que queira verificar isso pode gravar em um dos sysctls de simulação após o resume e observar que o contador por vetor correspondente incrementa:

```sh
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
sudo sysctl dev.myfirst.0.intr_simulate_admin=1
sysctl dev.myfirst.0.vec0_fire_count
# should be incremented
```

Se o contador não incrementar, o caminho MSI-X foi perturbado. A causa mais provável é um bug no gerenciamento de estado do próprio driver (o flag `suspended` não foi limpo, ou o filter está rejeitando a interrupção por outro motivo). A seção de solução de problemas do capítulo contém mais detalhes.

### Lidando com uma Retomada Falha de Forma Elegante

Se alguma etapa da retomada falhar, o driver tem opções limitadas. Ele não pode vetar a retomada (o kernel não possui um caminho de desfazimento nesse ponto). Normalmente também não pode tentar novamente (o estado do hardware é incerto). O melhor que ele pode fazer é:

1. Registrar a falha de forma proeminente com `device_printf` para que o usuário a veja no dmesg.
2. Incrementar um contador (`power_resume_errors`) que um script de regressão ou uma ferramenta de observabilidade possa verificar.
3. Marcar o dispositivo como defeituoso para que requisições subsequentes falhem de forma clara em vez de corrromper dados silenciosamente.
4. Manter o driver conectado (attached), para que o estado da árvore de dispositivos permaneça consistente e o usuário possa eventualmente descarregar e recarregar o driver.
5. Retornar 0 de `DEVICE_RESUME`, porque o kernel espera que ele seja bem-sucedido.

O padrão "marcar como defeituoso, manter conectado" é comum em drivers de produção. Ele transforma a falha de "corrupção misteriosa posterior" em "erro imediatamente visível ao usuário", o que proporciona uma experiência de depuração muito melhor.

### Um Pequeno Desvio: pci_save_state / pci_restore_state no Runtime PM

A Seção 2 mencionou que `pci_save_state` e `pci_restore_state` às vezes são chamados pelo próprio driver, tipicamente em um helper de gerenciamento de energia em tempo de execução (runtime power management). Vale a pena uma esboço concreto antes de a Seção 5 desenvolvê-lo completamente.

Um helper de runtime PM que coloca um dispositivo ocioso em D3 tem a seguinte aparência:

```c
static int
myfirst_runtime_suspend(struct myfirst_softc *sc)
{
        int err;

        err = myfirst_quiesce(sc);
        if (err != 0)
                return (err);

        pci_save_state(sc->dev);
        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3);
        if (err != 0) {
                /* roll back */
                pci_restore_state(sc->dev);
                myfirst_restore(sc);
                return (err);
        }

        return (0);
}

static int
myfirst_runtime_resume(struct myfirst_softc *sc)
{
        int err;

        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
        if (err != 0)
                return (err);
        pci_restore_state(sc->dev);

        return (myfirst_restore(sc));
}
```

O padrão é semelhante ao de suspend/resume do sistema, mas utiliza os helpers PCI explícitos porque a camada PCI não está no circuito. A Seção 5 transformará esse esboço em uma implementação real e o conectará a uma política de detecção de ociosidade.

### Uma Verificação da Realidade com um Driver Real

Antes de prosseguir, vale a pena pausar e examinar o caminho de retomada de um driver real. A função `re_resume` em `/usr/src/sys/dev/re/if_re.c` tem cerca de trinta linhas. Sua estrutura é:

1. Adquirir o lock do softc.
2. Se um flag de sleep do MAC estiver definido, tirar o chip do modo sleep escrevendo em um registrador GPIO.
3. Limpar quaisquer padrões de wake-on-LAN para que o filtragem normal de recebimento não seja interferida.
4. Se a interface estiver administrativamente ativa, reinicializá-la via `re_init_locked`.
5. Limpar o flag `suspended`.
6. Liberar o lock do softc.
7. Retornar 0.

A chamada a `re_init_locked` é o trabalho substantivo: ela reprograma o endereço MAC, redefine os anéis de descritores de recebimento e transmissão, reabilita as interrupções na NIC e inicia os motores de DMA. Para o `myfirst`, o trabalho equivalente é muito mais curto porque o dispositivo é muito mais simples, mas a forma é a mesma: adquirir o estado, fazer a reinicialização específica do hardware, liberar o lock, retornar.

Um leitor que ler `re_resume` após implementar a retomada do `myfirst` reconhecerá a estrutura imediatamente. O vocabulário é o mesmo; apenas os detalhes diferem.

### Encerrando a Seção 4

A Seção 4 completou o caminho de retomada. Ela mostrou o que a camada PCI já fez no momento em que `DEVICE_RESUME` é chamado (transição para D0, restauração do espaço de configuração, limpeza do PME#, restauração do MSI-X), o que o driver ainda precisa fazer (reabilitar bus-master, restaurar registradores específicos do dispositivo, limpar o flag suspended, desmascarar as interrupções) e por que cada etapa é importante. O driver do Estágio 3 agora consegue realizar um ciclo completo de suspend-resume e continuar operando normalmente; o teste de regressão pode executar vários ciclos consecutivos e verificar se os contadores estão consistentes.

Com as Seções 3 e 4 juntas, o driver está ciente de energia no sentido de suspend do sistema: ele lida corretamente com transições S3 e S4. O que ele ainda não faz é nenhuma economia de energia no nível do dispositivo enquanto o sistema está em execução. Isso é o gerenciamento de energia em tempo de execução, e a Seção 5 o ensina.



## Seção 5: Lidando com o Gerenciamento de Energia em Tempo de Execução

O suspend do sistema é uma transição grande e visível: a tampa se fecha, a tela escurece, a bateria economiza energia por horas. O gerenciamento de energia em tempo de execução (runtime power management) é o oposto: dezenas de pequenas transições invisíveis por segundo, cada uma economizando um pouco, juntas economizando grande parte da energia que um sistema moderno consome em ociosidade. O usuário nunca as percebe; o engenheiro de plataforma vive ou morre pela sua correção.

Esta seção é marcada como opcional no esboço do capítulo porque nem todo driver precisa de runtime PM. Um driver para um dispositivo sempre ativo (uma NIC em um servidor ocupado, um controlador de disco para o sistema de arquivos raiz) não economiza energia tentando suspender seu dispositivo; o dispositivo está ocupado, e tentar suspendê-lo desperdiça ciclos configurando transições que nunca se completam. Um driver para um dispositivo frequentemente ocioso (uma webcam, um leitor de impressão digital, uma placa WLAN em um laptop) se beneficia. A decisão de adicionar runtime PM é uma decisão de política orientada pelo perfil de uso do dispositivo.

Para o Capítulo 22, implementamos runtime PM no driver `myfirst` como um exercício de aprendizado. O dispositivo já é simulado; podemos fingir que ele está ocioso sempre que nenhum sysctl foi escrito nos últimos alguns segundos, e observar o driver passar pelas etapas. A implementação é curta, e ela ensina os primitivos de nível PCI que um driver real de runtime PM utiliza.

### O Que Runtime PM Significa no FreeBSD

O FreeBSD não possui atualmente um framework centralizado de runtime PM da maneira que o Linux tem. Não existe maquinaria do kernel do tipo "se o dispositivo ficou ocioso por N milissegundos, chame seu hook de ociosidade". Em vez disso, o runtime PM é local ao driver: o driver decide quando suspender e retomar seu dispositivo, usando os mesmos primitivos da camada PCI (`pci_set_powerstate`, `pci_save_state`, `pci_restore_state`) que usaria dentro de `DEVICE_SUSPEND` e `DEVICE_RESUME`.

Isso tem duas consequências. Primeiro, cada driver que deseja runtime PM implementa sua própria política: quanto tempo o dispositivo deve ficar ocioso antes de suspender, o que conta como ociosidade, com que rapidez o dispositivo deve acordar sob demanda. Segundo, o driver deve integrar seu runtime PM com seu PM do sistema; os dois caminhos compartilham muito código e não podem interferir um no outro.

O padrão que o Capítulo 22 utiliza é direto:

1. O driver adiciona uma pequena máquina de estados com os estados `RUNNING` e `RUNTIME_SUSPENDED`.
2. Quando o driver observa ociosidade (a Seção 5 usa uma política baseada em callout de "nenhuma requisição nos últimos 5 segundos"), ele chama `myfirst_runtime_suspend`.
3. Quando o driver observa uma nova requisição enquanto está em `RUNTIME_SUSPENDED`, ele chama `myfirst_runtime_resume` antes de processar a requisição.
4. No suspend do sistema, se o dispositivo estiver em `RUNTIME_SUSPENDED`, o caminho de suspend do sistema se ajusta a isso (o dispositivo já está quiesced; o quiesce do suspend do sistema é uma operação sem efeito, mas a retomada do sistema precisa trazer o dispositivo de volta a D0).
5. Na retomada do sistema, o driver retorna ao estado `RUNNING`, a menos que tenha sido explicitamente suspenso em runtime e queira permanecer assim.

Isso é mais simples que o framework de runtime PM do Linux, que tem conceitos mais ricos (contagem de referências pai/filho, timers de autosuspend, barreiras). Para um único driver em hardware simples, a abordagem do FreeBSD é suficiente.

### A Máquina de Estados do Runtime PM

O softc ganha uma variável de estado e um timestamp:

```c
enum myfirst_runtime_state {
        MYFIRST_RT_RUNNING = 0,
        MYFIRST_RT_SUSPENDED = 1,
};

struct myfirst_softc {
        /* ... */
        enum myfirst_runtime_state runtime_state;
        struct timeval             last_activity;
        struct callout             idle_watcher;
        int                        idle_threshold_seconds;
        uint64_t                   runtime_suspend_count;
        uint64_t                   runtime_resume_count;
};
```

O `idle_threshold_seconds` é um parâmetro de política exposto através de um sysctl; o valor padrão de cinco segundos oferece observabilidade rápida sem ser tão agressivo a ponto de causar wakeups desnecessários durante o uso normal. Um driver de produção ajustaria isso por dispositivo; cinco segundos é um valor amigável para aprendizado que torna as transições visíveis sem exigir horas de espera.

O callout `idle_watcher` dispara uma vez por segundo para verificar o tempo de ociosidade. Se o dispositivo estiver ocioso há mais de `idle_threshold_seconds` e atualmente estiver em `RUNNING`, o callout aciona `myfirst_runtime_suspend`.

### Implementação

O caminho de attach inicia o observador de ociosidade:

```c
static void
myfirst_start_idle_watcher(struct myfirst_softc *sc)
{
        sc->idle_threshold_seconds = 5;
        microtime(&sc->last_activity);
        callout_init_mtx(&sc->idle_watcher, &sc->mtx, 0);
        callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
}
```

O callout é inicializado com o mutex do softc, portanto ele adquire o mutex automaticamente ao disparar. Isso simplifica o callback: ele é executado sob o lock.

O callback verifica o tempo desde a última atividade e suspende se necessário:

```c
static void
myfirst_idle_watcher_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct timeval now, diff;

        MYFIRST_ASSERT_LOCKED(sc);

        if (sc->runtime_state == MYFIRST_RT_RUNNING && !sc->suspended) {
                microtime(&now);
                timersub(&now, &sc->last_activity, &diff);

                if (diff.tv_sec >= sc->idle_threshold_seconds) {
                        /*
                         * Release the lock while suspending. The
                         * runtime_suspend helper acquires it again as
                         * needed.
                         */
                        MYFIRST_UNLOCK(sc);
                        (void)myfirst_runtime_suspend(sc);
                        MYFIRST_LOCK(sc);
                }
        }

        /* Reschedule. */
        callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
}
```

Observe a liberação do lock em torno de `myfirst_runtime_suspend`. O helper de suspensão chama `myfirst_quiesce`, que adquire o lock por conta própria. Mantê-lo durante essa chamada causaria um deadlock.

A atividade é registrada sempre que o driver atende a uma requisição. O caminho de DMA do Capítulo 21 é um bom ponto de inserção: toda vez que um usuário escreve em `dma_test_read` ou `dma_test_write`, o handler do sysctl registra a atividade:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        /* ... existing code ... */

        /* Mark the device active before processing. */
        myfirst_mark_active(sc);

        /* If runtime-suspended, bring the device back before running. */
        if (sc->runtime_state == MYFIRST_RT_SUSPENDED) {
                int err = myfirst_runtime_resume(sc);
                if (err != 0)
                        return (err);
        }

        /* ... proceed with the transfer ... */
}
```

O helper `myfirst_mark_active` é uma função de uma linha:

```c
static void
myfirst_mark_active(struct myfirst_softc *sc)
{
        MYFIRST_LOCK(sc);
        microtime(&sc->last_activity);
        MYFIRST_UNLOCK(sc);
}
```

### Os Helpers de Runtime-Suspend e Runtime-Resume

Eles foram esboçados na Seção 4. Aqui estão as versões completas:

```c
static int
myfirst_runtime_suspend(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "runtime suspend: starting\n");

        err = myfirst_quiesce(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime suspend: quiesce failed (err %d)\n", err);
                /* Undo the suspended flag the quiesce set. */
                MYFIRST_LOCK(sc);
                sc->suspended = false;
                MYFIRST_UNLOCK(sc);
                return (err);
        }

        pci_save_state(sc->dev);
        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime suspend: set_powerstate(D3) failed "
                    "(err %d)\n", err);
                pci_restore_state(sc->dev);
                myfirst_restore(sc);
                return (err);
        }

        MYFIRST_LOCK(sc);
        sc->runtime_state = MYFIRST_RT_SUSPENDED;
        MYFIRST_UNLOCK(sc);
        atomic_add_64(&sc->runtime_suspend_count, 1);

        device_printf(sc->dev, "runtime suspend: device in D3\n");
        return (0);
}

static int
myfirst_runtime_resume(struct myfirst_softc *sc)
{
        int err;

        MYFIRST_LOCK(sc);
        if (sc->runtime_state != MYFIRST_RT_SUSPENDED) {
                MYFIRST_UNLOCK(sc);
                return (0);  /* nothing to do */
        }
        MYFIRST_UNLOCK(sc);

        device_printf(sc->dev, "runtime resume: starting\n");

        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime resume: set_powerstate(D0) failed "
                    "(err %d)\n", err);
                return (err);
        }
        pci_restore_state(sc->dev);

        err = myfirst_restore(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime resume: restore failed (err %d)\n", err);
                return (err);
        }

        MYFIRST_LOCK(sc);
        sc->runtime_state = MYFIRST_RT_RUNNING;
        MYFIRST_UNLOCK(sc);
        atomic_add_64(&sc->runtime_resume_count, 1);

        device_printf(sc->dev, "runtime resume: device in D0\n");
        return (0);
}
```

A forma é idêntica à do suspend/resume do sistema, exceto que o driver chama explicitamente `pci_set_powerstate` e `pci_save_state`/`pci_restore_state`. As transições automáticas da camada PCI não estão no circuito para runtime PM porque o kernel não está coordenando uma mudança de energia em todo o sistema; o driver está por conta própria.

### Interação Entre Runtime PM e PM do Sistema

Os dois caminhos precisam cooperar. Considere o que acontece se o dispositivo estiver suspenso em runtime (em D3) quando o usuário fechar a tampa do laptop:

1. O kernel inicia o suspend do sistema.
2. O barramento PCI chama `myfirst_pci_suspend`.
3. Dentro de `myfirst_pci_suspend`, o driver percebe que o dispositivo já está suspenso em runtime. O quiesce é uma operação sem efeito (nada está acontecendo). O salvamento automático do espaço de configuração pela camada PCI é executado; ele lê o espaço de configuração (que ainda é acessível em D3) e o armazena em cache.
4. A camada PCI transiciona o dispositivo de D3 para... espere, ele já está em D3. A transição para D3 é uma operação sem efeito.
5. O sistema dorme.
6. Na retomada, a camada PCI transiciona o dispositivo de volta para D0. O `myfirst_pci_resume` do driver é executado. Ele restaura o estado. Mas agora o driver acredita que o dispositivo está em `RUNNING` (porque a retomada do sistema limpou o flag `suspended`), enquanto conceitualmente ele estava suspenso em runtime antes. A próxima atividade usará o dispositivo normalmente e definirá `last_activity`; o observador de ociosidade eventualmente o ressuspenderá se ainda estiver ocioso.

A interação é na maior parte benigna; o pior que acontece é que o dispositivo faz uma viagem extra por D0 antes de o observador de ociosidade ressuspendê-lo. Uma implementação mais refinada lembraria o estado de suspensão em runtime ao longo do suspend do sistema e o restauraria, mas para um driver de aprendizado a abordagem simples é suficiente.

O inverso (suspender em sistema um dispositivo que já está suspenso em runtime) já está correto em nossa implementação porque `myfirst_quiesce` verifica `suspended` e retorna 0 se já estiver definido. O caminho de suspensão em runtime definiu `suspended = true` como parte de seu quiesce, portanto o quiesce do suspend do sistema vê o flag e o ignora.

### Expondo os Controles de Runtime PM Através de Sysctl

A política de runtime PM do driver pode ser controlada e observada através de sysctls:

```c
SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "idle_threshold_seconds",
    CTLFLAG_RW, &sc->idle_threshold_seconds, 0,
    "Runtime PM idle threshold (seconds)");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_suspend_count",
    CTLFLAG_RD, &sc->runtime_suspend_count, 0,
    "Runtime suspends performed");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_resume_count",
    CTLFLAG_RD, &sc->runtime_resume_count, 0,
    "Runtime resumes performed");
SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "runtime_state",
    CTLFLAG_RD, (int *)&sc->runtime_state, 0,
    "Runtime state: 0=running, 1=suspended");
```

Um leitor pode agora fazer isto:

```sh
# Watch the device idle out.
while :; do
        sysctl dev.myfirst.0.runtime_state dev.myfirst.0.runtime_suspend_count
        sleep 1
done &
```

Após cinco segundos de inatividade, `runtime_state` passa de 0 para 1 e `runtime_suspend_count` incrementa. Uma escrita em qualquer sysctl ativo aciona uma retomada e reverte o estado:

```sh
sudo sysctl dev.myfirst.0.dma_test_read=1
# The log shows: runtime resume, then the test read
```

### Compromissos

O runtime PM troca latência de wakeup por economia de energia em ociosidade. Cada transição de D3 para D0 tem um custo de tempo (dezenas de microssegundos em um link PCIe, incluindo a saída do ASPM) e, em alguns dispositivos, um custo de energia (a própria transição consome corrente). Para um dispositivo que fica ocioso na maior parte do tempo com rajadas raras de atividade, o compromisso é favorável. Para um dispositivo que está ativo na maior parte do tempo com períodos raros de ociosidade, o custo das transições domina.

O parâmetro `idle_threshold_seconds` permite que a plataforma ajuste isso. Um valor de 0 ou 1 é agressivo e adequado para uma webcam que é usada por segundos de cada vez e fica ociosa por minutos. Um valor de 60 é conservador e adequado para uma NIC cujos períodos de ociosidade são curtos mas frequentes. Um valor de 0 (se permitido) desabilitaria o runtime PM completamente, o que é apropriado para dispositivos que devem permanecer ligados o tempo todo.

O segundo tradeoff está na complexidade do código. O runtime PM adiciona uma máquina de estados, um callout, um idle watcher, dois helpers com comportamento similar ao do kobj e preocupações adicionais com a ordenação entre os caminhos de runtime PM e de PM do sistema. Cada um desses elementos é pequeno, mas juntos eles aumentam a superfície de exposição a bugs. Muitos drivers FreeBSD omitem deliberadamente o runtime PM por esse motivo; eles deixam o dispositivo permanecer em D0 e contam com os estados internos de baixo consumo do dispositivo (clock gating, PCIe ASPM) para economizar energia. Essa é uma escolha defensável e, para drivers em que a correção importa mais do que milivatts, é a escolha certa.

O driver `myfirst` do Capítulo 22 mantém o runtime PM como recurso opcional, controlado por uma flag de compilação:

```make
CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM
```

O leitor pode compilar com ou sem a flag; o código da Seção 5 só é incluído na compilação quando a flag está definida. O padrão da Etapa 3 é deixar o runtime PM desativado; a Etapa 4 o ativa no driver consolidado.

### Uma Nota sobre Runtime PM em Plataformas

Algumas plataformas fornecem seu próprio mecanismo de runtime PM ao lado do mecanismo local do driver. Em sistemas embarcados arm64 e RISC-V, o device tree pode descrever propriedades `power-domains` e `clocks` que o driver usa para desligar domínios de energia e fazer gate de clocks. Os subsistemas `ext_resources/clk`, `ext_resources/regulator` e `ext_resources/power` do FreeBSD tratam esses casos.

O runtime PM em tal plataforma é mais capaz do que o runtime PM exclusivo de PCI, porque a plataforma pode desligar blocos inteiros de SoC (um controlador USB, um motor de exibição, uma GPU) em vez de simplesmente mover o dispositivo PCI para D3. O driver usa o mesmo padrão (marcar como ocioso, desligar recursos no estado ocioso, religar para atividade), porém por meio de APIs diferentes.

O Capítulo 22 permanece no caminho de PCI porque é onde vive o driver `myfirst`. Um leitor que mais tarde trabalhe em uma plataforma embarcada encontrará a mesma estrutura conceitual com APIs específicas da plataforma. O capítulo menciona essa distinção aqui para que o leitor saiba que esse território existe.

### Encerrando a Seção 5

A Seção 5 adicionou gerenciamento de energia em tempo de execução ao driver. Ela definiu uma máquina de dois estados (`RUNNING`, `RUNTIME_SUSPENDED`), um observador de ociosidade baseado em callout, um par de funções auxiliares (`myfirst_runtime_suspend`, `myfirst_runtime_resume`) que utilizam as APIs explícitas de estado de energia e salvamento de estado da camada PCI, os hooks de registro de atividade nos manipuladores sysctl de DMA e os controles sysctl que expõem a política ao espaço do usuário. Ela também discutiu a interação entre runtime PM e PM de sistema, o tradeoff entre latência e consumo de energia, e a alternativa de runtime PM em nível de plataforma em sistemas embarcados.

Com as Seções 2 a 5 implementadas, o driver agora trata suspend de sistema, resume de sistema, shutdown de sistema, runtime suspend e runtime resume. O que ainda falta fazer de forma clara é mostrar como o leitor testa tudo isso a partir do espaço do usuário. A Seção 6 se volta para a interface do espaço do usuário: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v` e o teste de regressão que os combina.



## Seção 6: Interagindo com o Framework de Energia

Um driver que trata suspend e resume corretamente representa apenas metade da história. A outra metade é ser capaz de *testar* essa correção de forma repetida e deliberada, a partir do espaço do usuário. A Seção 6 apresenta as ferramentas que o FreeBSD oferece para esse propósito, explica como cada uma se encaixa no modelo de estado do driver e mostra como combiná-las em um script de regressão que exercita todos os caminhos construídos nas Seções 2 a 5.

### Os Quatro Pontos de Entrada no Espaço do Usuário

Quatro comandos cobrem praticamente tudo o que um desenvolvedor de drivers precisa:

- **`acpiconf -s 3`** (e suas variantes) pede ao ACPI que coloque o sistema inteiro no estado de sleep S3. Este é o teste mais realista; ele exercita o caminho completo, do espaço do usuário pela maquinaria de suspend do kernel pela camada PCI até os métodos do driver.
- **`zzz`** é um wrapper fino em torno de `acpiconf -s 3`. Ele lê `hw.acpi.suspend_state` (com padrão S3) e entra no estado de sleep correspondente. Para a maioria dos usuários é a forma mais conveniente de suspender a partir de um shell.
- **`devctl suspend myfirst0`** e **`devctl resume myfirst0`** acionam suspend e resume por dispositivo por meio dos ioctls `DEV_SUSPEND` e `DEV_RESUME` em `/dev/devctl2`. Eles chamam apenas os métodos do driver; o restante do sistema permanece em S0. Este é o alvo de iteração mais rápido e o que o Capítulo 22 usa na maior parte do desenvolvimento.
- **`devinfo -v`** lista todos os dispositivos na árvore de dispositivos com seus estados atuais. Ele mostra se um dispositivo está attached, suspended ou detached.

Cada um tem pontos fortes e fracos. O `acpiconf` é realista, mas lento (de um a três segundos por ciclo em hardware típico) e impactante (o sistema realmente dorme). O `devctl` é rápido (milissegundos por ciclo), mas exercita apenas o driver, não o código ACPI ou de plataforma. O `devinfo -v` é passivo e barato; observa sem alterar o estado.

Uma boa estratégia de regressão usa os três: `devctl` para testes unitários dos métodos do driver, `acpiconf` para testes de integração do caminho completo de suspend e `devinfo -v` como verificação rápida de sanidade.

### Usando acpiconf para Suspender o Sistema

Em uma máquina com ACPI funcionando, `acpiconf -s 3` é o que a Seção 1 chamou de suspend total de sistema. O comando:

```sh
sudo acpiconf -s 3
```

faz o seguinte:

1. Abre `/dev/acpi` e verifica se a plataforma suporta S3 via ioctl `ACPIIO_ACKSLPSTATE`.
2. Envia o ioctl `ACPIIO_REQSLPSTATE` para requisitar S3.
3. O kernel inicia a sequência de suspend: userland pausado, threads congeladas, percurso da árvore de dispositivos com `DEVICE_SUSPEND` em cada dispositivo.
4. Supondo que nenhum driver vetoe, o kernel entra em S3. A máquina dorme.
5. Um evento de despertar (a tampa é aberta, o botão de energia é pressionado, um dispositivo USB envia um sinal de remote-wakeup) acorda a plataforma.
6. O kernel executa a sequência de resume: `DEVICE_RESUME` em cada dispositivo, descongelando as threads e retomando o userland.
7. O prompt do shell retorna. A máquina está de volta em S0.

Para que o driver `myfirst` seja exercitado, ele deve estar carregado antes do suspend. A sequência completa do ponto de vista do usuário tem a seguinte aparência:

```sh
sudo kldload ./myfirst.ko
sudo sysctl dev.myfirst.0.dma_test_read=1  # exercise it a bit
sudo acpiconf -s 3
# [laptop sleeps; user opens lid]
dmesg | grep myfirst
```

A saída do `dmesg` deve mostrar duas linhas do logging do Capítulo 22:

```text
myfirst0: suspend: starting
myfirst0: suspend: complete (dma in flight=0, suspended=1)
myfirst0: resume: starting
myfirst0: resume: complete
```

Se essas linhas estiverem presentes e nessa ordem, os métodos do driver foram chamados corretamente pelo caminho completo do sistema.

Se a máquina não voltar, o caminho de suspend quebrou em alguma camada abaixo de `myfirst`. Se a máquina voltar mas o driver estiver em estado estranho (os sysctls retornam erros, os contadores têm valores inesperados, transferências DMA falham), o problema está na implementação de suspend ou resume do `myfirst`.

### Usando zzz

No FreeBSD, `zzz` é um pequeno script de shell que lê `hw.acpi.suspend_state` e chama `acpiconf -s <state>`. Não é um binário; normalmente é instalado em `/usr/sbin/zzz` e tem poucas linhas. Uma invocação típica é:

```sh
sudo zzz
```

O valor padrão de `hw.acpi.suspend_state` é `S3` em máquinas que o suportam. Um leitor que queira testar S4 (hibernação) pode fazer:

```sh
sudo sysctl hw.acpi.suspend_state=S4
sudo zzz
```

O suporte a S4 no FreeBSD historicamente foi parcial; se ele funciona depende do firmware da plataforma e do layout do sistema de arquivos. Para os propósitos do Capítulo 22, S3 é suficiente, e `zzz` é a forma abreviada conveniente.

### Usando devctl para Suspend por Dispositivo

O comando `devctl(8)` foi criado para permitir que um usuário manipule a árvore de dispositivos a partir do espaço do usuário. Ele suporta attach, detach, enable, disable, suspend, resume e mais. Para o Capítulo 22, `suspend` e `resume` são os dois que importam.

```sh
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
```

O primeiro comando emite `DEV_SUSPEND` por meio de `/dev/devctl2`; o kernel traduz isso em uma chamada a `BUS_SUSPEND_CHILD` no barramento pai, que para um dispositivo PCI acaba chamando `pci_suspend_child`, que salva o espaço de configuração, coloca o dispositivo em D3 e chama o `DEVICE_SUSPEND` do driver. O inverso acontece para o resume.

As principais diferenças em relação ao `acpiconf`:

- Apenas o dispositivo alvo e seus filhos passam pela transição. O restante do sistema permanece em S0.
- A CPU não é estacionada. O userland não é congelado. O kernel não dorme.
- O dispositivo PCI realmente vai para D3hot (supondo que `hw.pci.do_power_suspend` seja 1). O leitor pode verificar com `pciconf`:

```sh
# Before suspend: device should be in D0
pciconf -lvbc | grep -A 2 myfirst

# After devctl suspend myfirst0: device should be in D3
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 2 myfirst
```

O estado de energia normalmente é mostrado na linha `powerspec` de `pciconf -lvbc`. A passagem de `D0` para `D3` é o sinal observável de que a transição realmente aconteceu.

### Usando devinfo para Inspecionar o Estado do Dispositivo

O utilitário `devinfo(8)` lista a árvore de dispositivos com vários níveis de detalhe. A flag `-v` mostra informações detalhadas, incluindo o estado do dispositivo (attached, suspended ou not present).

```sh
devinfo -v | grep -A 5 myfirst
```

Saída típica:

```text
myfirst0 pnpinfo vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004 class=0x008880 at slot=5 function=0 dbsf=pci0:0:5:0
    Resource: <INTERRUPT>
        10
    Resource: <MEMORY>
        0xfeb80000-0xfeb80fff
```

O estado é implícito na saída: se o dispositivo estiver suspended, a linha mostra o dispositivo e seus recursos sem o marcador "active". Uma consulta explícita de estado pode ser feita pelo sysctl da softc; as chaves `dev.myfirst.0.%parent` e `dev.myfirst.0.%desc` indicam ao usuário onde o dispositivo está na hierarquia.

Para o Capítulo 22, `devinfo -v` é mais útil como verificação de sanidade após uma transição com falha: se o dispositivo estiver ausente da saída, o caminho de detach foi executado; se o dispositivo estiver presente mas os recursos estiverem errados, o caminho de attach ou resume deixou o dispositivo em estado inconsistente.

### Inspecionando Estados de Energia pelo sysctl

A camada PCI expõe informações de estado de energia pelo `sysctl` sob `hw.pci`. Duas variáveis são mais relevantes:

```sh
sysctl hw.pci.do_power_suspend
sysctl hw.pci.do_power_resume
```

Ambas têm valor padrão 1, o que significa que a camada PCI faz a transição dos dispositivos para D3 no suspend e de volta para D0 no resume. Definir qualquer uma delas como 0 desativa a transição automática para fins de depuração.

A camada ACPI expõe informações de estado do sistema:

```sh
sysctl hw.acpi.supported_sleep_state
sysctl hw.acpi.suspend_state
sysctl hw.acpi.s4bios
```

A primeira lista quais estados de sleep a plataforma suporta (tipicamente algo como `S3 S4 S5`). A segunda é o estado que `zzz` entra (normalmente `S3`). A terceira indica se S4 é implementado com assistência do BIOS.

Para observação por dispositivo, o driver expõe seu próprio estado por meio de `dev.myfirst.N.*`. O driver do Capítulo 22 adiciona:

- `dev.myfirst.N.suspended`: 1 se o driver considera estar suspended, 0 caso contrário.
- `dev.myfirst.N.power_suspend_count`: número de vezes que `DEVICE_SUSPEND` foi chamado.
- `dev.myfirst.N.power_resume_count`: número de vezes que `DEVICE_RESUME` foi chamado.
- `dev.myfirst.N.power_shutdown_count`: número de vezes que `DEVICE_SHUTDOWN` foi chamado.
- `dev.myfirst.N.runtime_state`: 0 para `RUNNING`, 1 para `RUNTIME_SUSPENDED`.
- `dev.myfirst.N.runtime_suspend_count`, `dev.myfirst.N.runtime_resume_count`: contadores de runtime PM.
- `dev.myfirst.N.idle_threshold_seconds`: limiar de ociosidade do runtime PM.

Entre esses sysctls e o `dmesg`, o leitor pode ver em detalhes completos o que o driver fez durante qualquer transição.

### Um Script de Regressão

O diretório de laboratórios ganha um novo script: `ch22-suspend-resume-cycle.sh`. O script:

1. Registra os valores de referência de cada contador.
2. Executa uma transferência DMA para confirmar que o dispositivo está funcionando.
3. Chama `devctl suspend myfirst0`.
4. Verifica se `dev.myfirst.0.suspended` é 1.
5. Verifica se `dev.myfirst.0.power_suspend_count` incrementou em 1.
6. Chama `devctl resume myfirst0`.
7. Verifica se `dev.myfirst.0.suspended` é 0.
8. Verifica se `dev.myfirst.0.power_resume_count` incrementou em 1.
9. Executa mais uma transferência DMA para confirmar que o dispositivo ainda funciona.
10. Imprime um resumo PASS/FAIL.

O script completo está no diretório de exemplos; um esboço resumido da lógica:

```sh
#!/bin/sh
set -e

DEV="dev.myfirst.0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present"
    exit 1
fi

before_s=$(sysctl -n ${DEV}.power_suspend_count)
before_r=$(sysctl -n ${DEV}.power_resume_count)
before_xfer=$(sysctl -n ${DEV}.dma_transfers_read)

# Baseline: run one transfer.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

# Suspend.
devctl suspend myfirst0
[ "$(sysctl -n ${DEV}.suspended)" = "1" ] || {
    echo "FAIL: device did not mark suspended"
    exit 1
}

# Resume.
devctl resume myfirst0
[ "$(sysctl -n ${DEV}.suspended)" = "0" ] || {
    echo "FAIL: device did not clear suspended"
    exit 1
}

# Another transfer.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

after_s=$(sysctl -n ${DEV}.power_suspend_count)
after_r=$(sysctl -n ${DEV}.power_resume_count)
after_xfer=$(sysctl -n ${DEV}.dma_transfers_read)

if [ $((after_s - before_s)) -ne 1 ]; then
    echo "FAIL: suspend count did not increment by 1"
    exit 1
fi
if [ $((after_r - before_r)) -ne 1 ]; then
    echo "FAIL: resume count did not increment by 1"
    exit 1
fi
if [ $((after_xfer - before_xfer)) -ne 2 ]; then
    echo "FAIL: expected 2 transfers (pre+post), got $((after_xfer - before_xfer))"
    exit 1
fi

echo "PASS: one suspend-resume cycle completed cleanly"
```

Executar o script repetidamente (digamos, cem vezes em um loop fechado) é um bom teste de estresse. Um driver que passa em um ciclo mas falha no 50º geralmente tem um vazamento de recurso ou um caso especial que só aparece sob repetição. Essa classe de bug é exatamente o que um script de regressão visa encontrar.

### Executando o Teste de Estresse

O diretório `labs/` do capítulo também inclui `ch22-suspend-stress.sh`, que executa o script de ciclo cem vezes:

```sh
#!/bin/sh
N=100
i=0
while [ $i -lt $N ]; do
    if ! sh ./ch22-suspend-resume-cycle.sh > /dev/null; then
        echo "FAIL on iteration $i"
        exit 1
    fi
    i=$((i + 1))
done
echo "PASS: $N cycles"
```

Em uma máquina moderna com o driver myfirst somente de simulação, cem ciclos levam cerca de um segundo. Se alguma iteração falhar, o script para e reporta o número da iteração. Executar isso após cada alteração durante o desenvolvimento captura regressões imediatamente.

### Combinando Runtime PM e Testes no Espaço do Usuário

O caminho de runtime PM precisa de um teste diferente, pois não é acionado por comandos do usuário; é acionado por ociosidade. O teste tem a seguinte aparência:

```sh
# Ensure runtime_state is running.
sysctl dev.myfirst.0.runtime_state
# 0

# Do nothing for 6 seconds.
sleep 6

# The callout should have fired and runtime-suspended the device.
sysctl dev.myfirst.0.runtime_state
# 1

# Counter should have incremented.
sysctl dev.myfirst.0.runtime_suspend_count
# 1

# Any activity should bring it back.
sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.runtime_state
# 0

sysctl dev.myfirst.0.runtime_resume_count
# 1
```

Um leitor que observe o `dmesg` durante esse processo verá as linhas "runtime suspend: starting" e "runtime suspend: device in D3" após cerca de cinco segundos de inatividade e, em seguida, "runtime resume: starting" quando a escrita no sysctl chegar.

O diretório de laboratórios do capítulo inclui `ch22-runtime-pm.sh` para automatizar essa sequência.

### Interpretando Modos de Falha

Quando um teste de espaço do usuário falha, o caminho de diagnóstico depende de qual camada falhou:

- **Se `devctl suspend` retornar um código de saída diferente de zero**: o método `DEVICE_SUSPEND` do driver retornou um valor diferente de zero, vetando a suspensão. Verifique o `dmesg` para ver a saída de log do driver; o método de suspensão deve estar registrando o que deu errado.
- **Se `devctl suspend` for bem-sucedido, mas `dev.myfirst.0.suspended` for 0 logo depois**: o quiesce do driver definiu o flag brevemente, mas algo o limpou. Isso geralmente significa que o quiesce está re-entrando em si mesmo, ou que o caminho de detach está disputando com a suspensão em uma condição de corrida.
- **Se `devctl resume` for bem-sucedido, mas a próxima transferência falhar**: o caminho de restauração não reinicializou completamente o dispositivo. Na maioria dos casos, uma máscara de interrupção ou um registrador de DMA não foi escrito; verifique os contadores de disparo por vetor antes e depois do resume para ver se as interrupções estão chegando ao driver.
- **Se `acpiconf -s 3` for bem-sucedido, mas o sistema não voltar**: um driver abaixo de `myfirst` na árvore está bloqueando o resume. Isso é incomum em uma VM de teste; é o modo de falha clássico em hardware real com drivers novos.
- **Se `acpiconf -s 3` retornar `EOPNOTSUPP`**: a plataforma não suporta S3. Verifique `sysctl hw.acpi.supported_sleep_state`.

Em todos os casos, a primeira fonte de informação é o `dmesg`. O driver do Capítulo 22 registra cada transição; se as linhas de log não aparecerem, o método não foi chamado, e o problema está em uma camada abaixo do driver.

### Um Fluxo Mínimo de Resolução de Problemas

Um fluxograma compacto para um ciclo de suspend-resume com falha:

1. O driver está carregado? `kldstat | grep myfirst`.
2. O dispositivo está conectado? `sysctl dev.myfirst.0.%driver`.
3. Os métodos suspend e resume registram mensagens? `dmesg | tail`.
4. O `dev.myfirst.0.suspended` alternou corretamente? `sysctl dev.myfirst.0.suspended`.
5. Os contadores incrementam? `sysctl dev.myfirst.0.power_suspend_count dev.myfirst.0.power_resume_count`.
6. Uma transferência após o resume é concluída com sucesso? `sudo sysctl dev.myfirst.0.dma_test_read=1; dmesg | tail -2`.
7. Os contadores de interrupção por vetor incrementam? `sysctl dev.myfirst.0.vec0_fire_count dev.myfirst.0.vec1_fire_count dev.myfirst.0.vec2_fire_count`.

Qualquer resposta "não" aponta para uma camada específica da implementação. A Seção 7 aprofunda os modos de falha mais comuns e como depurá-los.

### Encerrando a Seção 6

A Seção 6 apresentou a interface do espaço do usuário com a infraestrutura de gerenciamento de energia do kernel: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v` e as variáveis `sysctl` relevantes. Mostrou como combinar essas ferramentas em um script de regressão que exercita um ciclo de suspend-resume e em um script de stress que executa cem ciclos seguidos. Discutiu o fluxo de teste de PM em tempo de execução, a interpretação dos modos de falha mais comuns e o fluxograma mínimo de resolução de problemas que você pode seguir quando um teste falha.

Com as ferramentas do espaço do usuário em mãos, a próxima seção mergulha nos modos de falha característicos que você provavelmente encontrará ao escrever código com suporte a energia, e em como depurar cada um deles.



## Seção 7: Depurando Problemas de Gerenciamento de Energia

O código de gerenciamento de energia tem uma classe especial de bugs. A máquina dorme; a máquina acorda; o bug aparece em um momento desconhecido após o wake e parece uma falha genérica, sem qualquer relação aparente com a transição de energia. A cadeia de causa e efeito é mais longa do que na maioria dos bugs de driver, a reprodução é mais lenta, e o relato do usuário costuma ser "meu laptop às vezes não acorda", o que não contém quase nenhuma informação que o desenvolvedor do driver possa usar.

A Seção 7 trata de reconhecer os sintomas característicos, rastreá-los até suas prováveis causas e aplicar os padrões de depuração correspondentes. Ela usa o driver `myfirst` do Capítulo 22 como exemplo concreto, mas os padrões se aplicam a qualquer driver FreeBSD.

### Sintoma 1: Dispositivo Congelado Após o Resume

O bug de gerenciamento de energia mais comum, tanto em drivers de aprendizado quanto em drivers de produção, é um dispositivo que para de responder após o resume. O driver se conecta corretamente na inicialização, funciona normalmente em S0, passa por um ciclo de suspend-resume sem erro visível e, no próximo comando, fica silencioso. As interrupções não disparam. As transferências DMA não completam. Qualquer leitura de um registrador do dispositivo retorna valores obsoletos ou zeros.

A causa usual é que os registradores do dispositivo não foram escritos após o resume. O dispositivo voltou a um estado padrão (máscara de interrupção totalmente mascarada, engine DMA desabilitada, qualquer registrador que o hardware redefine na entrada D0), o driver não os reprogramou e, da perspectiva do dispositivo, nada está configurado para funcionar.

**Padrão de depuração.** Compare os valores dos registradores do dispositivo antes e depois do suspend. O driver `myfirst` expõe vários de seus registradores via sysctls (se o leitor os adicionar); caso contrário, você pode escrever um pequeno auxiliar no espaço do kernel que lê cada registrador e o exibe. Após um ciclo de suspend-resume:

1. Leia o registrador de máscara de interrupção. Se for `0xFFFFFFFF` (tudo mascarado), o caminho de resume não restaurou a máscara.
2. Leia o registrador de controle DMA. Se tiver o bit ABORT definido, o abort realizado durante o quiesce nunca foi limpo.
3. Leia o espaço de configuração do dispositivo via `pciconf -lvbc`. O registrador de comando deve ter o bit de bus-master ativo; se não estiver, `pci_enable_busmaster` foi omitido no caminho de resume.

**Padrão de correção.** O caminho de resume deve incluir uma reprogramação incondicional de cada registrador específico do dispositivo de que a operação normal do driver depende. Salvá-los no suspend dentro do softc e restaurá-los no resume é uma abordagem; recomputá-los a partir do estado do softc (a abordagem que `re_resume` adota) é outra. Ambas funcionam; a escolha depende de qual é mais fácil de provar correta para o dispositivo específico.

### Sintoma 2: Interrupções Perdidas

Uma variante mais sutil do problema de dispositivo congelado são as interrupções perdidas: o dispositivo responde a alguns comandos, mas suas interrupções não chegam ao driver. O engine DMA aceita um comando START, executa a transferência, gera a interrupção de conclusão... e o contador de interrupções não incrementa. A fila de tarefas não recebe uma entrada. A CV não faz broadcast. A transferência eventualmente expira por timeout, e o driver reporta EIO.

Várias coisas podem causar isso:

- A **máscara de interrupção** no dispositivo ainda está totalmente ativa. O dispositivo quer gerar a interrupção, mas a máscara a suprime. (Bug no caminho de resume.)
- A **configuração de MSI ou MSI-X** não foi restaurada. O dispositivo está gerando a interrupção, mas o kernel não a encaminha para o handler do driver. (Incomum; a camada PCI deve cuidar disso automaticamente.)
- O **ponteiro para a função de filtro** foi corrompido. Extremamente incomum; geralmente indica corrupção de memória em outro lugar do driver.
- O **flag suspended** ainda está verdadeiro, e o filtro está retornando antecipadamente. (Bug no caminho de resume: flag não limpo.)

**Padrão de depuração.** Leia os contadores de disparo por vetor antes e depois do ciclo de suspend-resume. Se o contador não incrementar, a interrupção não está chegando ao filtro. Em seguida, verifique, na ordem:

1. O flag suspended foi limpo? `sysctl dev.myfirst.0.suspended`.
2. A máscara de interrupção no dispositivo está correta? Leia o registrador.
3. A tabela MSI-X no dispositivo está correta? `pciconf -c` exibe os registradores de capacidade.
4. O estado de despacho MSI do kernel está consistente? `procstat -t` exibe as threads de interrupção.

**Padrão de correção.** Certifique-se de que o caminho de resume (a) limpa o flag suspended sob o lock, (b) desmascara o registrador de interrupção do dispositivo após limpar o flag, (c) não depende de restauração de MSI-X que o driver deve realizar por conta própria (a menos que especificamente desabilitado via sysctl).

### Sintoma 3: DMA Errado Após o Resume

Uma classe de bug mais perigosa é o DMA que parece funcionar, mas produz dados incorretos. O driver programa o engine, o engine executa, a interrupção de conclusão dispara, a tarefa roda, o sync é chamado, o driver lê o buffer... e os bytes estão errados. Não zeros, não lixo, apenas sutilmente incorretos: o padrão escrito anteriormente, o padrão de dois ciclos atrás, ou um padrão que indica que o DMA endereçou a página errada.

Causas:

- O **endereço de barramento armazenado no softc** está obsoleto. Isso é incomum para uma alocação estática (o endereço é definido uma vez no attach e não deveria mudar), mas pode ocorrer se o driver realocar o buffer DMA no resume (uma má ideia; veja abaixo).
- O **registrador de endereço-base do engine DMA** não foi reprogramado após o resume, e ele contém um valor obsoleto que aponta para outro lugar.
- As **chamadas a `bus_dmamap_sync`** estão ausentes ou fora de ordem. Este é o bug clássico de corretude de DMA, e vale a pena ficar atento a ele nos caminhos de resume porque o código do driver próximo às chamadas sync frequentemente é editado durante refatorações.
- A **tabela de tradução do IOMMU** não foi restaurada. Muito raro no FreeBSD porque a configuração do IOMMU é por sessão e sobrevive ao suspend na maioria das plataformas; mas se o driver estiver rodando em um sistema onde `DEV_IOMMU` é incomum, isso pode causar problemas.

**Padrão de depuração.** Adicione uma escrita de padrão conhecido antes de cada DMA, uma verificação após cada DMA, e registre ambos. Reduzir o ciclo a "escreve 0xAA, sync, lê, espera 0xAA" torna os bugs de corrupção de dados imediatamente visíveis.

```c
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE);
/* run transfer */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE);
if (((uint8_t *)sc->dma_vaddr)[0] != 0xAA) {
        device_printf(sc->dev,
            "dma: corruption detected after transfer\n");
}
```

Para a simulação, isso deve sempre ter sucesso porque a simulação não modifica o buffer em uma transferência de escrita. Em hardware real, o padrão depende do dispositivo. Um leitor depurando um bug em hardware real adapta o teste.

**Padrão de correção.** Se o endereço de barramento for o problema, reconstrua-o no resume:

```c
/* In resume, after PCI restore is complete. */
err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &sc->dma_bus_addr,
    BUS_DMA_NOWAIT);
```

Faça isso apenas se o endereço de barramento realmente mudou, o que é raro. Mais comumente, a correção é escrever o registrador de endereço-base no início de cada transferência (em vez de depender de um valor persistente) e garantir que as chamadas sync estejam na ordem correta.

### Sintoma 4: Eventos de Wake PME# Perdidos

Em um dispositivo que suporta wake-on-X, o sintoma é "o dispositivo deveria ter acordado o sistema, mas não acordou". O driver reportou um suspend bem-sucedido; o sistema foi para S3; o evento esperado (magic packet, pressão de botão, timer) ocorreu; e o sistema permaneceu dormindo.

Causas:

- **`pci_enable_pme` não foi chamado** no caminho de suspend. O bit PME_En do dispositivo é 0, então mesmo quando o dispositivo normalmente ativaria PME#, o bit é suprimido.
- **A própria lógica de wake do dispositivo não está configurada**. Para uma NIC, os registradores de wake-on-LAN devem ser programados antes do suspend. Para um controlador USB host, a capacidade de remote-wakeup deve ser habilitada por porta.
- **O GPE de wake da plataforma não está habilitado**. Isso geralmente é uma questão de firmware; o método ACPI `_PRW` deveria ter registrado o GPE, mas em algumas máquinas o BIOS o desabilita por padrão.
- **O bit de status PME está definido no momento do suspend**, e um PME# obsoleto é o que dispara o wake (em vez do evento esperado). O sistema parece acordar imediatamente após dormir.

**Padrão de depuração.** Leia o espaço de configuração PCI via `pciconf -lvbc`. O registrador de status/controle da capacidade de gerenciamento de energia exibe PME_En e o bit PME_Status. Antes de suspender, PME_Status deve ser 0 (nenhum wake pendente). Após suspender com wake habilitado, PME_En deve ser 1.

Em uma máquina onde o wake não ocorre, verifique as configurações da BIOS para "wake on LAN", "wake on USB", etc. O driver pode estar perfeito e o sistema ainda assim não acordar se a plataforma não estiver configurada.

**Padrão de correção.** No caminho de suspend de um driver com capacidade de wake:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        /* ... quiesce as before ... */

        if (sc->wake_enabled && pci_has_pm(dev)) {
                /* Program device-specific wake logic here. */
                myfirst_program_wake(sc);
                pci_enable_pme(dev);
        }

        /* ... rest of suspend ... */
}
```

No caminho de resume:

```c
static int
myfirst_pci_resume(device_t dev)
{
        if (pci_has_pm(dev))
                pci_clear_pme(dev);
        /* ... rest of resume ... */
}
```

O driver `myfirst` do Capítulo 22 não implementa wake (a simulação não tem lógica de wake). O padrão acima é mostrado apenas como referência.

### Sintoma 5: Reclamações do WITNESS Durante o Suspend

Um kernel de debug com `WITNESS` habilitado frequentemente produz mensagens como:

```text
witness: acquiring sleepable lock foo_mtx @ /path/to/driver.c:123
witness: sleeping with non-sleepable lock bar_mtx @ /path/to/driver.c:456
```

Essas são violações de ordem de lock ou violações de sleep-while-locked, e elas costumam aparecer no código de suspend porque o suspend faz coisas que o driver normalmente não faz: adquire locks, dorme e coordena múltiplas threads.

Causas:

- O caminho de suspend adquire um lock e depois chama uma função que dorme sem tolerância explícita para dormir com aquele lock mantido.
- O caminho de suspend adquire locks em uma ordem diferente do restante do driver, e o `WITNESS` percebe a inversão.
- O caminho de suspend chama `taskqueue_drain` ou `callout_drain` enquanto mantém o lock do softc, o que causa um deadlock se a tarefa ou o callout tentar adquirir o mesmo lock.

**Padrão de depuração.** Leia a mensagem do `WITNESS` com atenção. Ela inclui os nomes dos locks e os números de linha do código-fonte onde cada um foi adquirido. Rastreie o caminho desde a aquisição até o sleep ou a inversão de lock.

**Padrão de correção.** O `myfirst_quiesce` do Capítulo 22 solta o lock do softc antes de chamar `myfirst_drain_workers` exatamente por essa razão. Ao estender o driver:

- Não chame `taskqueue_drain` com nenhum lock do driver mantido.
- Não chame `callout_drain` com o lock que o callout adquire.
- Primitivas de sleep (`pause`, `cv_wait`) devem ser chamadas com apenas sleep-mutexes mantidos (não spin-mutexes).
- Se for necessário soltar um lock para um sleep, faça-o explicitamente e readquira após.

### Sintoma 6: Contadores que Não Correspondem

O script de regressão do capítulo espera que `power_suspend_count == power_resume_count` após cada ciclo. Quando eles divergem, algo está errado.

Causas:

- O `DEVICE_SUSPEND` do driver foi chamado, mas o driver retornou prematuramente antes de incrementar o contador. (Geralmente porque alguma verificação de sanidade foi acionada.)
- O `DEVICE_RESUME` do driver não foi chamado porque `DEVICE_SUSPEND` retornou um valor diferente de zero e o kernel desfez a operação.
- Os contadores não são atômicos e uma atualização concorrente perdeu um incremento. (Improvável se o código utilizar `atomic_add_64`.)
- O driver foi descarregado e recarregado entre as contagens, reiniciando-as.

**Padrão de depuração.** Execute o script de regressão com o buffer limpo previamente via `dmesg -c`, e rode `dmesg` após cada ciclo. O log exibe cada invocação de método; contar as linhas do log é uma alternativa à contagem dos contadores, e qualquer diferença indica um bug.

### Sintoma 7: Travamentos Durante o Suspend

Um travamento durante o suspend é o pior cenário de diagnóstico: o kernel ainda está em execução (o console ainda responde a break-to-DDB), mas a sequência de suspend está parada no `DEVICE_SUSPEND` de algum driver. Entre no DDB e use `ps` para ver em que estado cada thread se encontra:

```text
db> ps
...  0 myfirst_drain_dma+0x42 myfirst_pci_suspend+0x80 ...
```

**Padrão de depuração.** Identifique a thread travada e a função em que ela está presa. Em geral, trata-se de um `cv_wait` ou `cv_timedwait` que nunca foi concluído, ou de um `taskqueue_drain` aguardando uma tarefa que não termina.

**Padrão de correção.** Adicione um timeout a qualquer espera realizada pelo caminho de suspend. A função `myfirst_drain_dma` usa `cv_timedwait` com timeout de um segundo; uma variante que use `cv_wait` (sem timeout) pode travar indefinidamente. A implementação deste capítulo sempre usa variantes com timeout por esse motivo.

### Usando DTrace para Rastrear Suspend e Resume

DTrace é uma ferramenta excelente para observar o caminho de gerenciamento de energia com alta granularidade, sem precisar adicionar instruções de impressão. Um script D simples que mede o tempo de cada chamada:

```d
fbt::device_suspend:entry,
fbt::device_resume:entry
{
    self->ts = timestamp;
    printf("%s: %s %s\n", probefunc,
        args[0] != NULL ? stringof(args[0]->name) : "?",
        args[0] != NULL ? stringof(args[0]->desc) : "?");
}

fbt::device_suspend:return,
fbt::device_resume:return
/self->ts/
{
    printf("%s: returned %d after %d us\n",
        probefunc, arg1,
        (timestamp - self->ts) / 1000);
    self->ts = 0;
}
```

Salve esse script como `trace-devpower.d` e execute com `dtrace -s trace-devpower.d`. Qualquer `devctl suspend` ou `acpiconf -s 3` produzirá saída mostrando o tempo de suspend e resume de cada dispositivo, além dos valores de retorno.

Para o driver `myfirst` especificamente, `fbt::myfirst_pci_suspend:entry` e `fbt::myfirst_pci_resume:entry` são as probes correspondentes. Um script D focado no driver:

```d
fbt::myfirst_pci_suspend:entry {
    self->ts = timestamp;
    printf("myfirst_pci_suspend: entered\n");
    stack();
}

fbt::myfirst_pci_suspend:return
/self->ts/ {
    printf("myfirst_pci_suspend: returned %d after %d us\n",
        arg1, (timestamp - self->ts) / 1000);
    self->ts = 0;
}
```

A chamada `stack()` imprime a pilha de chamadas na entrada, o que é útil para confirmar que o método está sendo invocado de onde se espera (por exemplo, do `bus_suspend_child` do barramento PCI).

### Uma Nota Sobre Disciplina de Logging

O código do Capítulo 22 registra eventos de forma generosa durante o suspend e o resume: cada método registra entrada e saída, e cada helper registra seus próprios eventos. Essa verbosidade é útil durante o desenvolvimento, mas irritante em produção (cada suspend do laptop imprime meia dúzia de linhas no dmesg).

Um bom driver de produção expõe um sysctl que controla a verbosidade dos logs:

```c
static int myfirst_power_verbose = 1;
SYSCTL_INT(_dev_myfirst, OID_AUTO, power_verbose,
    CTLFLAG_RWTUN, &myfirst_power_verbose, 0,
    "Verbose power-management logging (0=off, 1=on, 2=debug)");
```

E o logging passa a ser condicional:

```c
if (myfirst_power_verbose >= 1)
        device_printf(dev, "suspend: starting\n");
```

Um leitor que queira habilitar a depuração em um sistema de produção pode definir `dev.myfirst.power_verbose=2` temporariamente, reproduzir o problema e redefinir a variável. O driver do Capítulo 22 não implementa esse escalonamento; o driver de aprendizado registra tudo e aceita o ruído.

### Usando o Kernel com INVARIANTS para Cobertura de Asserções

Um kernel de depuração com `INVARIANTS` compilado faz com que as macros `KASSERT` avaliem suas condições e causem panic em caso de falha. O código em `myfirst_dma.c` e `myfirst_pci.c` usa vários KASSERTs; o código de gerenciamento de energia adiciona mais. Por exemplo, a invariante de quiesce:

```c
static int
myfirst_quiesce(struct myfirst_softc *sc)
{
        /* ... */

        KASSERT(sc->dma_in_flight == false,
            ("myfirst: dma_in_flight still true after drain"));

        return (0);
}
```

Em um kernel com `INVARIANTS`, um bug que deixe `dma_in_flight` como verdadeiro causa um panic imediato com uma mensagem útil. Em um kernel de produção, a asserção é compilada fora e nada acontece. O driver de aprendizado roda deliberadamente em um kernel com `INVARIANTS` para capturar essa classe de bug.

Da mesma forma, o caminho de resume pode fazer uma asserção:

```c
KASSERT(sc->suspended == true,
    ("myfirst: resume called but not suspended"));
```

Isso captura um bug em que o driver recebe uma chamada de resume sem que o suspend correspondente tenha ocorrido (geralmente um bug em um driver de barramento pai, não no próprio driver `myfirst`).

### Um Estudo de Caso de Depuração

Para integrar os padrões apresentados, considere um cenário concreto. O leitor escreve o suspend do Estágio 2, executa um ciclo de regressão e observa:

```text
myfirst0: suspend: starting
myfirst0: drain_dma: timeout waiting for abort
myfirst0: suspend: complete (dma in flight=0, suspended=1)
myfirst0: resume: starting
myfirst0: resume: complete
```

Em seguida:

```sh
sudo sysctl dev.myfirst.0.dma_test_read=1
# Returns EBUSY after a long delay
```

O sintoma visível para o usuário é que a transferência após o resume não funciona. O log mostra um timeout de drain durante o suspend, que é a primeira anomalia.

**Hipótese.** O motor de DMA não respeitou o bit ABORT. O driver limpou `dma_in_flight` à força, mas o motor ainda está em execução; quando o usuário aciona uma nova transferência, o motor não está pronto.

**Teste.** Verifique o registrador de status do motor antes e depois do abort:

```c
/* In myfirst_drain_dma, after writing ABORT: */
uint32_t pre_status = CSR_READ_4(sc->dev, MYFIRST_REG_DMA_STATUS);
DELAY(100);  /* let the engine notice */
uint32_t post_status = CSR_READ_4(sc->dev, MYFIRST_REG_DMA_STATUS);
device_printf(sc->dev, "drain: status %#x -> %#x\n", pre_status, post_status);
```

Executar o ciclo novamente produz:

```text
myfirst0: drain: status 0x4 -> 0x4
```

O status 0x4 é RUNNING. O motor ignorou o ABORT. Isso aponta para o backend de simulação: o motor simulado pode não implementar abort, ou pode implementá-lo apenas quando o callout da simulação disparar.

**Correção.** Examine o código do motor de DMA da simulação e verifique a semântica do abort. Neste caso, o motor da simulação trata o abort no callback do callout, que não dispara por alguns milissegundos. Estenda o timeout de drain de 1 segundo (mais do que suficiente) para... espere, 1 segundo é mais do que suficiente para um callout que dispara a cada poucos milissegundos. O problema real está em outro lugar.

Uma investigação mais aprofundada revela que o callout da simulação foi drenado *antes* que o drain de DMA fosse concluído. A ordem em `myfirst_drain_workers` (task primeiro, callout segundo) estava errada; deveria ser callout primeiro, task segundo, porque é o callout que conduz a conclusão do abort.

**Resolução.** Reordene o drain:

```c
static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
        /*
         * Drain the callout first: it runs the simulated engine's
         * completion logic, and the drain-DMA path waits on that
         * completion. Draining the callout after drain_dma would let
         * drain_dma time out and force-clear the in-flight flag.
         *
         * Wait - actually, drain_dma has already completed by the time
         * we get here, because myfirst_quiesce calls it first. So the
         * order of the two drains inside this function does not matter
         * for that reason. But drain_workers is also called from detach,
         * where drain_dma may not have been called, and the order there
         * does matter.
         */
        if (sc->sim != NULL)
                myfirst_sim_drain_dma_callout(sc->sim);

        if (sc->rx_vector.has_task)
                taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);
}
```

Mas atenção: no momento em que `myfirst_drain_workers` é chamado a partir de `myfirst_quiesce`, `myfirst_drain_dma` já foi concluído. A espera do drain-dma está dentro da chamada drain-dma; a chamada drain-workers apenas limpa o estado residual. A ordem dentro de drain-workers é em grande parte estética para o suspend.

A correção real é anterior: o próprio `myfirst_drain_dma` não deveria ter atingido o timeout. O timeout de 1 segundo deveria ter sido mais do que suficiente. A causa real é diferente: talvez o callout da simulação não estivesse disparando porque o driver mantinha um lock de sysctl que o bloqueava. Ou a escrita do bit ABORT não chegou à simulação porque o handler de MMIO da simulação também estava bloqueado.

**Lição.** Depurar problemas de gerenciamento de energia é um processo iterativo. Cada sintoma sugere uma hipótese; cada teste a delimita; a correção frequentemente está em uma camada diferente daquela para a qual o sintoma apontava. A paciência para seguir essa cadeia é o que distingue um código power-aware bem feito de um código que "quase sempre" funciona.

### Encerrando a Seção 7

A Seção 7 percorreu os modos de falha característicos dos drivers power-aware: dispositivos congelados, interrupções perdidas, DMA incorreto, eventos de wake ignorados, reclamações do WITNESS, deriva de contadores e travamentos completos. Para cada um deles, foi mostrada a causa típica, um padrão de depuração para delimitar o problema e o padrão de correção que o elimina. A seção também apresentou DTrace para medição, discutiu disciplina de logging e mostrou como `INVARIANTS` e `WITNESS` capturam a classe de bug que só aparece em condições específicas.

A disciplina de depuração da Seção 7, assim como a disciplina de quiesce da Seção 3 e a disciplina de restore da Seção 4, foi concebida para acompanhar o leitor além do driver `myfirst`. Todo driver power-aware tem alguma variação desses bugs escondida em sua implementação; os padrões acima são a forma de encontrá-los antes que cheguem ao usuário.

A Seção 8 encerra o Capítulo 22 consolidando o código das Seções 2 a 7 em um arquivo `myfirst_power.c` refatorado, incrementando a versão para `1.5-power`, adicionando um documento `POWER.md` e preparando um teste de integração final.



## Seção 8: Refatorando e Versionando Seu Driver Power-Aware

Os Estágios 1 a 3 adicionaram o código de gerenciamento de energia diretamente em `myfirst_pci.c`. Isso foi conveniente para o ensino, pois cada alteração aparecia ao lado do código de attach e detach que o leitor já conhecia. É menos conveniente para a legibilidade: `myfirst_pci.c` agora contém attach, detach, três métodos de energia e vários helpers, e o arquivo ficou longo o suficiente para que um leitor iniciante precise rolar bastante para encontrar o que procura.

O Estágio 4, versão final do driver do Capítulo 22, extrai todo o código de gerenciamento de energia de `myfirst_pci.c` para um novo par de arquivos, `myfirst_power.c` e `myfirst_power.h`. Isso segue o mesmo padrão da divisão em `myfirst_msix.c` no Capítulo 20 e da divisão em `myfirst_dma.c` no Capítulo 21: o novo arquivo possui uma API bem documentada e estreita, e o código chamador em `myfirst_pci.c` utiliza apenas essa API.

### O Layout-Alvo

Após o Estágio 4, os arquivos-fonte do driver são:

- `myfirst.c` - cola de nível superior, estado compartilhado, árvore de sysctl.
- `myfirst_hw.c`, `myfirst_hw_pci.c` - helpers de acesso a registradores.
- `myfirst_sim.c` - backend de simulação.
- `myfirst_pci.c` - PCI attach, detach, tabela de métodos e encaminhamento fino para os módulos de subsistema.
- `myfirst_intr.c` - interrupção de vetor único (caminho legado do Capítulo 19).
- `myfirst_msix.c` - configuração de interrupção multivetor (Capítulo 20).
- `myfirst_dma.c` - configuração, desmontagem e transferência de DMA (Capítulo 21).
- `myfirst_power.c` - gerenciamento de energia (Capítulo 22, novo).
- `cbuf.c` - suporte ao buffer circular.

O novo `myfirst_power.h` declara a API pública do subsistema de energia:

```c
#ifndef _MYFIRST_POWER_H_
#define _MYFIRST_POWER_H_

struct myfirst_softc;

int  myfirst_power_setup(struct myfirst_softc *sc);
void myfirst_power_teardown(struct myfirst_softc *sc);

int  myfirst_power_suspend(struct myfirst_softc *sc);
int  myfirst_power_resume(struct myfirst_softc *sc);
int  myfirst_power_shutdown(struct myfirst_softc *sc);

#ifdef MYFIRST_ENABLE_RUNTIME_PM
int  myfirst_power_runtime_suspend(struct myfirst_softc *sc);
int  myfirst_power_runtime_resume(struct myfirst_softc *sc);
void myfirst_power_mark_active(struct myfirst_softc *sc);
#endif

void myfirst_power_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_POWER_H_ */
```

O par `_setup` e `_teardown` inicializa e desmonta o estado de nível de subsistema (o callout, os sysctls). As funções por transição encapsulam a mesma lógica construída ao longo das Seções 3 a 5. As funções de runtime PM são compiladas apenas quando a flag de build correspondente está definida.

### O Arquivo myfirst_power.c

O novo arquivo tem aproximadamente trezentas linhas. Sua estrutura espelha a de `myfirst_dma.c`: includes de cabeçalho, helpers estáticos, funções públicas, handlers de sysctl e `_add_sysctls`.

Os helpers são os três da Seção 3:

- `myfirst_mask_interrupts`
- `myfirst_drain_dma`
- `myfirst_drain_workers`

Mais um da Seção 4:

- `myfirst_restore`

E, se o runtime PM estiver habilitado, dois da Seção 5:

- `myfirst_idle_watcher_cb`
- `myfirst_start_idle_watcher`

As funções públicas `myfirst_power_suspend`, `myfirst_power_resume` e `myfirst_power_shutdown` tornam-se wrappers finos que chamam os helpers na ordem correta e atualizam os contadores. Os handlers de sysctl expõem os controles de política e os contadores de observabilidade.

### Atualizando myfirst_pci.c

O arquivo `myfirst_pci.c` fica agora muito mais curto. Seus três métodos de energia simplesmente encaminham para o subsistema de energia:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        return (myfirst_power_suspend(device_get_softc(dev)));
}

static int
myfirst_pci_resume(device_t dev)
{
        return (myfirst_power_resume(device_get_softc(dev)));
}

static int
myfirst_pci_shutdown(device_t dev)
{
        return (myfirst_power_shutdown(device_get_softc(dev)));
}
```

A tabela de métodos permanece a mesma configurada pelo Estágio 1. Os três protótipos acima são agora o único código relacionado a energia em `myfirst_pci.c`, além da chamada a `myfirst_power_setup` no attach e a `myfirst_power_teardown` no detach.

O caminho de attach recebe uma chamada adicional:

```c
static int
myfirst_pci_attach(device_t dev)
{
        /* ... existing attach code ... */

        err = myfirst_power_setup(sc);
        if (err != 0) {
                device_printf(dev, "power setup failed\n");
                /* unwind */
                myfirst_dma_teardown(sc);
                /* ... rest of unwind ... */
                return (err);
        }

        myfirst_power_add_sysctls(sc);

        return (0);
}
```

O caminho de detach recebe uma chamada correspondente:

```c
static int
myfirst_pci_detach(device_t dev)
{
        /* ... existing detach code ... */

        myfirst_power_teardown(sc);

        /* ... rest of detach ... */

        return (0);
}
```

`myfirst_power_setup` inicializa o `saved_intr_mask`, a flag `suspended`, os contadores e (se o runtime PM estiver habilitado) o callout do idle watcher. `myfirst_power_teardown` drena o callout e limpa qualquer estado de nível de subsistema. O teardown deve ser realizado antes do teardown de DMA, pois o callout ainda pode referenciar estado de DMA.

### Atualizando o Makefile

O novo arquivo-fonte vai para a lista `SRCS`, e a versão é incrementada:

```make
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       myfirst_power.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power\"

# Optional: enable runtime PM.
# CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM

.include <bsd.kmod.mk>
```

A flag `MYFIRST_ENABLE_RUNTIME_PM` está desabilitada por padrão no Estágio 4; o código de runtime PM compila, mas está envolto em `#ifdef`. Um leitor que queira experimentar habilita a flag em tempo de build.

### Escrevendo o POWER.md

O padrão estabelecido pelo Capítulo 21 cria o precedente: cada subsistema recebe um documento markdown que descreve seu propósito, sua API, seu modelo de estados e sua história de testes. `POWER.md` é o próximo.

Um bom `POWER.md` contém estas seções:

1. **Propósito**: um parágrafo explicando o que o subsistema faz.
2. **API Pública**: uma tabela de protótipos de funções com descrições de uma linha.
3. **Modelo de Estados**: um diagrama ou descrição textual dos estados e transições.
4. **Contadores e Sysctls**: os sysctls somente leitura e de leitura/escrita expostos pelo subsistema.
5. **Fluxos de Transição**: o que acontece durante cada um dos ciclos de suspend, resume e shutdown.
6. **Interação com Outros Subsistemas**: como o gerenciamento de energia se relaciona com DMA, interrupções e a simulação.
7. **Runtime PM (opcional)**: como o runtime PM funciona e quando é habilitado.
8. **Testes**: os scripts de regressão e estresse.
9. **Limitações Conhecidas**: o que o subsistema ainda não faz.
10. **Consulte Também**: referências cruzadas para `bus(9)`, `pci(9)` e o texto do capítulo.

O documento completo está no diretório de exemplos (`examples/part-04/ch22-power/stage4-final/POWER.md`); o capítulo não o reproduz inline, mas um leitor que queira verificar a estrutura esperada pode abri-lo.

### Script de Regressão

O script de regressão do Estágio 4 exercita todos os caminhos:

```sh
#!/bin/sh
# ch22-full-regression.sh

set -e

# 1. Basic sanity.
sudo kldload ./myfirst.ko

# 2. One suspend-resume cycle.
sudo sh ./ch22-suspend-resume-cycle.sh

# 3. One hundred cycles in a row.
sudo sh ./ch22-suspend-stress.sh

# 4. A transfer before, during, and after a cycle.
sudo sh ./ch22-transfer-across-cycle.sh

# 5. If runtime PM is enabled, test it.
if sysctl -N dev.myfirst.0.runtime_state >/dev/null 2>&1; then
    sudo sh ./ch22-runtime-pm.sh
fi

# 6. Unload.
sudo kldunload myfirst

echo "FULL REGRESSION PASSED"
```

Cada sub-script tem algumas dezenas de linhas e testa uma coisa só. Executar a regressão completa após cada alteração detecta regressões imediatamente.

### Integração com os Testes de Regressão Existentes

O script de regressão do Capítulo 21 verificava:

- `dma_complete_intrs == dma_complete_tasks` (a task sempre vê cada interrupção).
- `dma_complete_intrs == dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`.

O script do Capítulo 22 adiciona:

- `power_suspend_count == power_resume_count` (cada suspensão tem uma retomada correspondente).
- O flag `suspended` é 0 fora de uma transição.
- Após um ciclo de suspend-resume, os contadores de DMA ainda somam o total esperado (sem transferências fantasmas).

A regressão combinada é o script completo do Capítulo 22. Ele exercita DMA, interrupções, MSI-X e gerenciamento de energia em conjunto. Um driver que passa por ele está em boa forma.

### Histórico de Versões

O driver evoluiu por diversas versões:

- `1.0` - Capítulo 16: driver apenas com MMIO, backend de simulação.
- `1.1` - Capítulo 18: attach PCI, BAR real.
- `1.2-intx` - Capítulo 19: interrupção de vetor único com filter+task.
- `1.3-msi` - Capítulo 20: MSI-X com múltiplos vetores e fallback.
- `1.4-dma` - Capítulo 21: configuração de `bus_dma`, motor de DMA simulado, conclusão orientada a interrupções.
- `1.5-power` - Capítulo 22: suspend/resume/shutdown, refatorado em `myfirst_power.c`, PM em tempo de execução opcional.

Cada versão é construída sobre a anterior. Um leitor que já tem o driver do Capítulo 21 funcionando pode aplicar as alterações do Capítulo 22 de forma incremental e chegar ao `1.5-power` sem precisar reescrever nenhum código anterior.

### Um Teste de Integração Final em Hardware Real

Se o leitor tiver acesso a hardware real (uma máquina com implementação S3 funcional), o driver do Capítulo 22 pode ser exercitado por meio de uma suspensão completa do sistema:

```sh
sudo kldload ./myfirst.ko
sudo sh ./ch22-suspend-resume-cycle.sh
sudo acpiconf -s 3
# [laptop sleeps; user opens lid]
# After resume, the DMA test should still work.
sudo sysctl dev.myfirst.0.dma_test_read=1
```

Na maioria das plataformas onde o ACPI S3 funciona, o driver sobrevive ao ciclo completo. A saída do `dmesg` mostra as linhas de suspensão e retomada exatamente como `devctl` acionaria, confirmando que o mesmo código do método é executado em ambos os contextos.

Se o teste de sistema completo falhar onde o teste por dispositivo teve sucesso, o trabalho adicional que a suspensão do sistema realiza (transições de estado de suspensão do ACPI, estacionamento de CPUs, auto-atualização de RAM) expôs algo que o teste por dispositivo deixou passar. Os culpados habituais são valores de registradores específicos do dispositivo que o estado de baixo consumo do sistema redefine, mas o D3 por dispositivo não. Um driver testado apenas com `devctl` pode deixar isso passar; um driver testado com `acpiconf -s 3` pelo menos uma vez antes de declarar sua correção é mais confiável.

### O Código do Capítulo 22 em Um Só Lugar

Um resumo compacto do que o driver do Estágio 4 adicionou:

- **Um novo arquivo**: `myfirst_power.c`, com cerca de trezentas linhas.
- **Um novo cabeçalho**: `myfirst_power.h`, com cerca de trinta linhas.
- **Um novo documento markdown**: `POWER.md`, com cerca de duzentas linhas.
- **Cinco novos campos no softc**: `suspended`, `saved_intr_mask`, `power_suspend_count`, `power_resume_count`, `power_shutdown_count`, além dos campos de PM em tempo de execução quando esse recurso está habilitado.
- **Três novas linhas `DEVMETHOD`**: `device_suspend`, `device_resume`, `device_shutdown`.
- **Três novas funções auxiliares**: `myfirst_mask_interrupts`, `myfirst_drain_dma`, `myfirst_drain_workers`.
- **Dois novos pontos de entrada do subsistema**: `myfirst_power_setup`, `myfirst_power_teardown`.
- **Três novas funções de transição**: `myfirst_power_suspend`, `myfirst_power_resume`, `myfirst_power_shutdown`.
- **Seis novos sysctls**: os nós de contadores e o flag de suspensão.
- **Vários novos scripts de laboratório**: cycle, stress, transfer-across-cycle, runtime-PM.

O incremento total é de cerca de setecentas linhas de código, mais algumas centenas de linhas de documentação e script. Para a capacidade que o capítulo adicionou (um driver que lida corretamente com cada transição de energia que o kernel pode provocar), esse é um investimento proporcional.

### Encerrando a Seção 8

A Seção 8 concluiu a construção do driver do Capítulo 22 ao separar o código de energia em seu próprio arquivo, atualizar a versão para `1.5-power`, adicionar um documento `POWER.md` e conectar o teste de regressão final. O padrão era familiar dos Capítulos 20 e 21: pegar o código inline, extraí-lo em um subsistema com uma API pequena, documentar o subsistema e integrá-lo ao restante do driver por meio de chamadas de função em vez de acesso direto aos campos.

O driver resultante é ciente de energia em todos os sentidos que o capítulo introduziu: ele lida com `DEVICE_SUSPEND`, `DEVICE_RESUME` e `DEVICE_SHUTDOWN`; silencia o dispositivo de forma limpa; restaura o estado corretamente na retomada; implementa opcionalmente o gerenciamento de energia em tempo de execução; expõe seu estado por meio de sysctls; tem um teste de regressão; e sobrevive à suspensão completa do sistema em hardware real quando a plataforma suporta isso.



## Análise Detalhada: Gerenciamento de Energia em /usr/src/sys/dev/re/if_re.c

As NICs Gigabit Realtek 8169 e compatíveis são gerenciadas pelo driver `re(4)`. É um driver esclarecedor para os propósitos do Capítulo 22, pois implementa o trio completo de suspend-resume-shutdown com suporte a wake-on-LAN, e seu código é estável o suficiente para representar um padrão canônico do FreeBSD. Um leitor que concluiu o Capítulo 22 pode abrir `/usr/src/sys/dev/re/if_re.c` e reconhecer a estrutura imediatamente.

> **Como ler este percurso.** As listagens emparelhadas de `re_suspend()` e `re_resume()` nas subseções abaixo foram extraídas de `/usr/src/sys/dev/re/if_re.c`, e o trecho da tabela de métodos abrevia o array completo `re_methods[]` com um comentário `/* ... other methods ... */` para que as três entradas `DEVMETHOD` relacionadas à energia se destaquem. Mantivemos as assinaturas, o padrão de aquisição e liberação de lock, e a ordem das chamadas específicas do dispositivo (`re_stop`, `re_setwol`, `re_clrwol`, `re_init_locked`) intactos; a tabela de métodos real tem muito mais entradas, e o arquivo ao redor traz as implementações auxiliares. Todo símbolo nomeado nas listagens é um identificador real do FreeBSD em `if_re.c` que você pode encontrar com uma busca de símbolo.

### A Tabela de Métodos

A tabela de métodos do driver `re(4)` inclui os três métodos de energia próximos ao início:

```c
static device_method_t re_methods[] = {
        DEVMETHOD(device_probe,     re_probe),
        DEVMETHOD(device_attach,    re_attach),
        DEVMETHOD(device_detach,    re_detach),
        DEVMETHOD(device_suspend,   re_suspend),
        DEVMETHOD(device_resume,    re_resume),
        DEVMETHOD(device_shutdown,  re_shutdown),
        /* ... other methods ... */
};
```

Este é exatamente o padrão que o Capítulo 22 ensina. A tabela de métodos do driver `myfirst` tem a mesma aparência.

### re_suspend

A função de suspensão tem cerca de uma dúzia de linhas:

```c
static int
re_suspend(device_t dev)
{
        struct rl_softc *sc;

        sc = device_get_softc(dev);

        RL_LOCK(sc);
        re_stop(sc);
        re_setwol(sc);
        sc->suspended = 1;
        RL_UNLOCK(sc);

        return (0);
}
```

Três chamadas fazem o trabalho: `re_stop` silencia a NIC (desativa interrupções, para o DMA, interrompe os motores de RX e TX), `re_setwol` programa a lógica de wake-on-LAN e chama `pci_enable_pme` se o WoL estiver habilitado, e `sc->suspended = 1` define o flag no softc.

Compare com `myfirst_power_suspend`:

```c
int
myfirst_power_suspend(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "suspend: starting\n");
        err = myfirst_quiesce(sc);
        /* ... error handling ... */
        atomic_add_64(&sc->power_suspend_count, 1);
        return (0);
}
```

A estrutura é idêntica. `re_stop` e `re_setwol` juntos são equivalentes a `myfirst_quiesce`; o driver do capítulo não tem wake-on-X, portanto não há análogo para `re_setwol`.

### re_resume

A função de retomada tem cerca de trinta linhas:

```c
static int
re_resume(device_t dev)
{
        struct rl_softc *sc;
        if_t ifp;

        sc = device_get_softc(dev);

        RL_LOCK(sc);

        ifp = sc->rl_ifp;
        /* Take controller out of sleep mode. */
        if ((sc->rl_flags & RL_FLAG_MACSLEEP) != 0) {
                if ((CSR_READ_1(sc, RL_MACDBG) & 0x80) == 0x80)
                        CSR_WRITE_1(sc, RL_GPIO,
                            CSR_READ_1(sc, RL_GPIO) | 0x01);
        }

        /*
         * Clear WOL matching such that normal Rx filtering
         * wouldn't interfere with WOL patterns.
         */
        re_clrwol(sc);

        /* reinitialize interface if necessary */
        if (if_getflags(ifp) & IFF_UP)
                re_init_locked(sc);

        sc->suspended = 0;
        RL_UNLOCK(sc);

        return (0);
}
```

Os passos mapeiam claramente a disciplina do Capítulo 22:

1. **Retirar o controlador do modo de suspensão** (bit de suspensão do MAC em alguns componentes Realtek). Este é um passo de restauração específico do dispositivo.
2. **Limpar os padrões WOL** via `re_clrwol`, que reverte o que `re_setwol` fez. Isso também chama `pci_clear_pme` implicitamente por meio da limpeza.
3. **Reinicializar a interface** se ela estava ativa antes da suspensão. `re_init_locked` é a mesma função que attach chama para ativar a NIC; ela reprograma o MAC, redefine os anéis de descritores, habilita as interrupções e inicia os motores de DMA.
4. **Limpar o flag de suspensão** sob o lock.

O equivalente em `myfirst_power_resume`:

```c
int
myfirst_power_resume(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "resume: starting\n");
        err = myfirst_restore(sc);
        /* ... */
        atomic_add_64(&sc->power_resume_count, 1);
        return (0);
}
```

Novamente, a estrutura é idêntica. `myfirst_restore` corresponde à combinação da saída do modo de suspensão do MAC, `re_clrwol`, `re_init_locked` e a limpeza do flag.

### re_shutdown

A função de desligamento é:

```c
static int
re_shutdown(device_t dev)
{
        struct rl_softc *sc;

        sc = device_get_softc(dev);

        RL_LOCK(sc);
        re_stop(sc);
        /*
         * Mark interface as down since otherwise we will panic if
         * interrupt comes in later on, which can happen in some
         * cases.
         */
        if_setflagbits(sc->rl_ifp, 0, IFF_UP);
        re_setwol(sc);
        RL_UNLOCK(sc);

        return (0);
}
```

Semelhante a `re_suspend`, acrescida da limpeza do flag de interface (o desligamento é definitivo; marcar a interface como inativa evita atividade espúria). O padrão é quase idêntico; `re_shutdown` é essencialmente uma versão mais defensiva de `re_suspend`.

### re_setwol

A configuração de wake-on-LAN vale a pena examinar porque mostra como um driver real chama as APIs de PM do PCI:

```c
static void
re_setwol(struct rl_softc *sc)
{
        if_t ifp;
        uint8_t v;

        RL_LOCK_ASSERT(sc);

        if (!pci_has_pm(sc->rl_dev))
                return;

        /* ... programs device-specific wake registers ... */

        /* Request PME if WOL is requested. */
        if ((if_getcapenable(ifp) & IFCAP_WOL) != 0)
                pci_enable_pme(sc->rl_dev);
}
```

Três padrões-chave aparecem aqui que valem a pena copiar em qualquer driver ciente de energia que suporte wake-on-X:

1. **Guarda `pci_has_pm(dev)`.** A função retorna cedo se o dispositivo não suportar gerenciamento de energia. Isso evita escritas em registradores que não existem.
2. **Programação de wake específica do dispositivo.** A maior parte da função escreve registradores específicos da Realtek via `CSR_WRITE_1`. Um driver para um dispositivo diferente escreveria registradores diferentes, mas o posicionamento (dentro do caminho de suspensão, antes de `pci_enable_pme`) é o mesmo.
3. **`pci_enable_pme` condicional.** Habilitar PME# somente se o usuário tiver de fato pedido wake-on-X. Se não, a função ainda define os bits de configuração relevantes (por consistência com as capacidades de interface do driver), mas não chama `pci_enable_pme`.

O inverso é `re_clrwol`:

```c
static void
re_clrwol(struct rl_softc *sc)
{
        uint8_t v;

        RL_LOCK_ASSERT(sc);

        if (!pci_has_pm(sc->rl_dev))
                return;

        /* ... clears the wake-related config bits ... */
}
```

Observe que `re_clrwol` não chama `pci_clear_pme` explicitamente; a camada PCI já chamou `pci_resume_child` antes do `DEVICE_RESUME` do driver. `re_clrwol` é responsável por desfazer o lado visível ao driver da configuração de WoL, não o status de PME visível ao kernel.

### O Que a Análise Detalhada Revela

O driver da Realtek é mais complexo que o `myfirst` por qualquer medida (mais registradores, mais estado, mais variantes de dispositivo), e ainda assim sua disciplina de gerenciamento de energia é menos complexa. Isso ocorre porque a complexidade do *dispositivo* não se mapeia diretamente para a complexidade do *código de gerenciamento de energia*. A disciplina do Capítulo 22 escala para baixo tanto quanto escala para cima: um dispositivo simples tem um caminho de energia simples; um dispositivo complexo tem um caminho de energia moderadamente mais complexo. A estrutura é a mesma.

Um leitor que concluiu o Capítulo 22 pode agora abrir `if_re.c`, reconhecer cada função e cada padrão, e entender por que cada um existe. Essa compreensão se transfere: o mesmo reconhecimento se aplica a `if_xl.c`, `virtio_blk.c` e centenas de outros drivers do FreeBSD. O Capítulo 22 não está ensinando uma API específica do `myfirst`; está ensinando o idioma de gerenciamento de energia do FreeBSD, e o driver `myfirst` é o veículo que o tornou concreto.



## Análise Detalhada: Padrões Mais Simples em if_xl.c e virtio_blk.c

Por contraste, dois outros drivers do FreeBSD implementam o gerenciamento de energia de maneiras ainda mais simples.

### if_xl.c: Shutdown Chama Suspend

O driver 3Com EtherLink III em `/usr/src/sys/dev/xl/if_xl.c` tem a configuração mínima com três métodos:

```c
static int
xl_shutdown(device_t dev)
{
        return (xl_suspend(dev));
}

static int
xl_suspend(device_t dev)
{
        struct xl_softc *sc;

        sc = device_get_softc(dev);

        XL_LOCK(sc);
        xl_stop(sc);
        xl_setwol(sc);
        XL_UNLOCK(sc);

        return (0);
}

static int
xl_resume(device_t dev)
{
        struct xl_softc *sc;
        if_t ifp;

        sc = device_get_softc(dev);
        ifp = sc->xl_ifp;

        XL_LOCK(sc);

        if (if_getflags(ifp) & IFF_UP) {
                if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
                xl_init_locked(sc);
        }

        XL_UNLOCK(sc);

        return (0);
}
```

Dois aspectos se destacam:

1. `xl_shutdown` tem uma linha: ela apenas chama `xl_suspend`. Para este driver, shutdown e suspend fazem o mesmo trabalho, e o código não precisa de duas cópias.
2. Não há flag `suspended` no softc. O driver assume o ciclo de vida normal de attach → execução → suspend → resume, e usa o flag `IFF_DRV_RUNNING` (que o caminho de TX já verifica) como equivalente. Essa é uma abordagem perfeitamente válida para uma NIC cujo principal estado visível ao usuário é o estado de execução da interface.

Para o driver `myfirst`, o flag `suspended` explícito é preferido porque o driver não tem equivalente natural para `IFF_DRV_RUNNING`. Um driver de NIC pode reutilizar o que já tem; um driver de aprendizado declara o que precisa.

### virtio_blk.c: Quiesce Mínimo

O driver de bloco virtio em `/usr/src/sys/dev/virtio/block/virtio_blk.c` tem um caminho de suspensão ainda mais curto:

```c
static int
vtblk_suspend(device_t dev)
{
        struct vtblk_softc *sc;
        int error;

        sc = device_get_softc(dev);

        VTBLK_LOCK(sc);
        sc->vtblk_flags |= VTBLK_FLAG_SUSPEND;
        /* XXX BMV: virtio_stop(), etc needed here? */
        error = vtblk_quiesce(sc);
        if (error)
                sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
        VTBLK_UNLOCK(sc);

        return (error);
}

static int
vtblk_resume(device_t dev)
{
        struct vtblk_softc *sc;

        sc = device_get_softc(dev);

        VTBLK_LOCK(sc);
        sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
        vtblk_startio(sc);
        VTBLK_UNLOCK(sc);

        return (0);
}
```

O comentário `/* XXX BMV: virtio_stop(), etc needed here? */` é um reconhecimento honesto de que o autor não tinha certeza de quão completo deveria ser o quiesce. O código existente define um flag, aguarda o esvaziamento da fila (é isso que `vtblk_quiesce` faz) e retorna. Na retomada, ele limpa o flag e reinicia o I/O.

Para um dispositivo de bloco virtio, isso é suficiente porque o host virtio (o hypervisor) implementa seu próprio quiesce quando o guest informa que está suspendendo. O driver só precisa parar de enviar novas requisições; o host cuida do resto.

Isso ilustra um padrão importante: **a profundidade do quiesce do driver depende de quanta responsabilidade o driver tem sobre o estado do hardware**. Um driver bare-metal (como o `re(4)`) precisa programar os registradores de hardware com cuidado, pois o hardware não tem nenhum outro aliado. Um driver virtio tem o hypervisor como aliado; o host pode cuidar da maior parte do estado em nome do guest. O driver `myfirst`, executando sobre um backend simulado, está em posição semelhante: a simulação é uma aliada, e o quiesce do driver pode ser correspondentemente mais simples.

### O Que a Comparação Revela

Ler o código de gerenciamento de energia de vários drivers lado a lado é uma das melhores formas de desenvolver fluência. Cada driver adapta o padrão do Capítulo 22 ao seu contexto: `re(4)` trata o wake-on-LAN, `xl(4)` reutiliza `xl_shutdown = xl_suspend`, `virtio_blk(4)` confia no hypervisor. O fio condutor é a estrutura: parar a atividade, salvar o estado, sinalizar a suspensão, retornar 0 no suspend; ao retomar, limpar o sinalizador, restaurar o estado, reiniciar a atividade, retornar 0.

Um leitor que tem o Capítulo 22 em mente pode abrir qualquer driver FreeBSD, encontrar seu `device_suspend` e `device_resume` na tabela de métodos e ler as duas funções. Em poucos minutos, a política de energia do driver fica clara. Essa habilidade se transfere para todos os drivers com os quais você trabalhará; é o aprendizado mais valioso do capítulo.



## Análise Aprofundada: Estados de Sleep do ACPI em Maior Detalhe

A Seção 1 apresentou os S-states do ACPI como uma lista. Vale a pena revisitá-los com foco no ponto de vista do driver, pois o driver percebe coisas ligeiramente diferentes dependendo de qual S-state o kernel está entrando.

### S0: Em Operação

S0 é o estado em que você trabalhou ao longo dos Capítulos 16 a 21. O CPU está executando, a RAM está sendo atualizada, os links PCIe estão ativos. Do ponto de vista do driver, S0 é contínuo; tudo está normal.

Dentro do S0, no entanto, ainda podem ocorrer transições de energia granulares. O CPU pode entrar em estados ociosos (C1, C2, C3, etc.) entre os ticks do escalonador. O link PCIe pode entrar em L0s ou L1 com base no ASPM. Dispositivos podem entrar em D3 com base no runtime PM. Nenhuma dessas transições exige que o driver faça algo além de sua própria lógica de runtime PM; elas são transparentes.

### S1: Standby

S1 é historicamente o estado de sleep mais leve. O CPU para de executar, mas seus registradores são preservados; a RAM permanece alimentada; a energia do dispositivo permanece em D0 ou D1. A latência de wake é rápida (menos de um segundo).

Em hardware moderno, S1 raramente é suportado. O BIOS da plataforma anuncia apenas S3 e estados mais profundos. Se a plataforma anuncia S1 e o usuário entra nesse estado, o `DEVICE_SUSPEND` do driver ainda é chamado; o driver realiza seu quiesce habitual. A diferença é que a camada PCI normalmente não faz a transição para D3 no S1 (porque o barramento permanece alimentado), portanto o dispositivo permanece em D0 durante a transição. O save e o restore do driver ficam em grande parte sem uso.

Um driver que suporta S1 corretamente também suporta S3, pois o trabalho do lado do driver é um subconjunto. Nenhum driver escrito para o Capítulo 22 precisa tratar S1 de forma especial.

### S2: Reservado

S2 é definido na especificação ACPI, mas quase nunca é implementado. Um driver pode ignorá-lo com segurança; a camada ACPI do FreeBSD trata S2 como S1 ou S3, dependendo do suporte da plataforma.

### S3: Suspend to RAM

S3 é o estado de sleep canônico que o Capítulo 22 tem como alvo. Quando o usuário entra em S3:

1. A sequência de suspend do kernel percorre a árvore de dispositivos, chamando `DEVICE_SUSPEND` em cada driver.
2. O `pci_suspend_child` da camada PCI armazena em cache o espaço de configuração de cada dispositivo PCI.
3. A camada PCI faz a transição de cada dispositivo PCI para D3hot.
4. Subsistemas de nível superior (ACPI, a maquinaria de ociosidade do CPU) entram em seus próprios estados de sleep.
5. O contexto do CPU é salvo na RAM; o CPU para.
6. A RAM entra em auto-atualização; o controlador de memória mantém o conteúdo com consumo mínimo de energia.
7. O circuito de wake da plataforma é armado: o botão de energia, a chave da tampa e quaisquer fontes de wake configuradas.
8. O sistema aguarda um evento de wake.

Quando um evento de wake chega:

1. O CPU retoma; seu contexto é restaurado da RAM.
2. Subsistemas de nível superior retomam.
3. A camada PCI percorre a árvore de dispositivos e chama `pci_resume_child` para cada dispositivo.
4. Cada dispositivo é transferido para D0; sua configuração é restaurada; o PME# pendente é limpo.
5. O `DEVICE_RESUME` de cada driver é chamado.
6. O espaço do usuário é descongelado.

O driver vê apenas os passos 1 (suspend) e 5 (resume) de cada sequência. O restante é maquinaria do kernel e da plataforma.

Um ponto sutil: durante o S3, a RAM é atualizada, mas o kernel não está em execução. Isso significa que qualquer estado do lado do kernel (o softc, o buffer de DMA, as tarefas pendentes) sobrevive ao S3 inalterado. A única coisa que pode ser perdida é o estado do hardware: registradores de configuração no dispositivo podem ser redefinidos; registradores mapeados via BAR podem retornar aos valores padrão. O trabalho do driver no resume é reprogramar o hardware a partir do estado do kernel preservado.

### S4: Suspend to Disk (Hibernate)

S4 é o estado de "hibernate". O kernel grava o conteúdo completo da RAM em uma imagem de disco e, em seguida, entra em S5. Ao acordar, a plataforma inicializa, o kernel lê a imagem de volta e o sistema continua de onde parou.

No FreeBSD, S4 historicamente tem sido parcial. O kernel pode produzir a imagem de hibernação em algumas plataformas, mas o caminho de restauração não é tão maduro quanto o do Linux. Para fins de driver, S4 é o mesmo que S3: os métodos `DEVICE_SUSPEND` e `DEVICE_RESUME` são chamados; os caminhos de quiesce e restore do driver funcionam sem alteração. O trabalho extra no nível da plataforma (gravação da imagem) é transparente.

A única diferença que o driver pode notar é que, após o resume do S4, o espaço de configuração PCI é sempre restaurado do zero (a plataforma reiniciou completamente), portanto mesmo que o driver dependesse de `hw.pci.do_power_suspend` igual a 0 para manter o dispositivo em D0, após o S4 o dispositivo ainda terá passado por um ciclo de energia completo. Isso importa apenas para drivers que fazem truques específicos da plataforma durante o suspend; a maioria dos drivers não percebe.

### S5: Soft Off

S5 é o desligamento do sistema. O botão de energia, a bateria (se houver) e o circuito de wake ainda recebem energia; todo o resto está desligado.

Do ponto de vista do driver, S5 se parece com um shutdown: `DEVICE_SHUTDOWN` é chamado (não `DEVICE_SUSPEND`), o driver coloca o dispositivo em um estado seguro para o desligamento e o sistema para. Não há resume correspondente ao S5; se o usuário pressionar o botão de energia, o sistema inicializa do zero.

O shutdown não é uma transição de energia no sentido reversível; é um encerramento. O método `DEVICE_SHUTDOWN` do driver é chamado uma vez, e o driver não espera executar novamente até o próximo boot. O `myfirst_power_shutdown` do capítulo trata isso corretamente ao fazer o quiesce do dispositivo (igual ao suspend) e não tentar salvar nenhum estado (pois não há resume correspondente).

### Observando Quais Estados a Plataforma Suporta

Em qualquer sistema FreeBSD 14.3 com ACPI, os estados suportados são expostos por um sysctl:

```sh
sysctl hw.acpi.supported_sleep_state
```

Saídas típicas:

- Um laptop moderno: `S3 S4 S5`
- Um servidor: `S5` (suspend não é suportado em muitas plataformas de servidor)
- Uma VM no bhyve: varia; geralmente apenas `S5`
- Uma VM no QEMU/KVM com `-machine q35`: geralmente `S3 S4 S5`

Se um driver se destina a funcionar em uma plataforma específica, a lista de estados suportados informa quais transições você precisa testar. Um driver que só roda em servidores não precisa de testes de S3; um driver voltado para laptops precisa.

### O Que Testar

Para os fins do Capítulo 22, o teste mínimo é:

- `devctl suspend` / `devctl resume`: sempre possível; testa o caminho de código do lado do driver.
- `acpiconf -s 3` (se suportado): testa o suspend completo do sistema.
- Shutdown do sistema (`shutdown -p now`): testa o método `DEVICE_SHUTDOWN`.

S4 e runtime PM são opcionais; eles exercitam caminhos de código menos utilizados. Um driver que passa no teste mínimo em uma plataforma que suporta S3 está em boa forma; as extensões são um bônus.

### Mapeando Estados de Sleep para os Métodos do Driver

Uma tabela compacta de qual método kobj é chamado para cada transição:

| Transição           | Método             | Ação do Driver                                                  |
|---------------------|--------------------|------------------------------------------------------------------|
| S0 → S1             | DEVICE_SUSPEND     | Quiesce; salvar estado                                           |
| S0 → S3             | DEVICE_SUSPEND     | Quiesce; salvar estado (dispositivo provavelmente vai para D3)   |
| S0 → S4             | DEVICE_SUSPEND     | Quiesce; salvar estado (seguido de hibernate)                    |
| S0 → S5 (shutdown)  | DEVICE_SHUTDOWN    | Quiesce; deixar em estado seguro para desligamento               |
| S1/S3 → S0          | DEVICE_RESUME      | Restaurar estado; desmascarar interrupções                       |
| S4 → S0 (resume)    | (attach a partir do boot) | attach normal, pois o kernel inicializou do zero           |
| devctl suspend      | DEVICE_SUSPEND     | Quiesce; salvar estado (dispositivo vai para D3)                 |
| devctl resume       | DEVICE_RESUME      | Restaurar estado; desmascarar interrupções                       |

O driver não distingue S1, S3 e S4 a partir do seu próprio código; ele sempre realiza o mesmo trabalho. As diferenças estão nos níveis da plataforma e do kernel. Essa uniformidade é o que torna o padrão escalável: um caminho de suspend, um caminho de resume, múltiplos contextos.



## Análise Aprofundada: Estados de Link PCIe e ASPM em Ação

A Seção 1 esboçou os estados de link PCIe (L0, L0s, L1, L1.1, L1.2, L2). Vale a pena ver como eles se comportam na prática, pois compreendê-los ajuda o desenvolvedor de drivers a interpretar medições de latência e observações de energia.

### Por Que o Link Tem Seus Próprios Estados

Um link PCIe é um par de lanes diferenciais de alta velocidade entre dois endpoints (root complex e dispositivo, ou root complex e switch). Cada lane tem um transmissor e um receptor; o transmissor de cada lane consome energia para manter o canal em um estado conhecido. Quando o tráfego é baixo, os transmissores podem ser desligados em vários graus, e o link pode ser restabelecido rapidamente quando o tráfego recomeçar. Os L-states descrevem esses graus.

O estado do link é separado do D-state do dispositivo. Um dispositivo em D0 pode ter seu link em L1 (o link está ocioso; o dispositivo não está transmitindo ou recebendo). Um dispositivo em D3 tem seu link em L2 ou similar (o link está desligado). Um dispositivo em D0 com um link ocupado está em L0.

### L0: Ativo

L0 é o estado de operação normal. Ambos os lados do link estão ativos; os dados podem fluir em qualquer direção; a latência está no mínimo (algumas centenas de nanossegundos no round-trip em um PCIe moderno).

Quando uma transferência DMA está em execução ou uma leitura MMIO está pendente, o link está em L0. A própria lógica do dispositivo e o PCIe host bridge exigem L0 para a transação.

### L0s: Standby do Transmissor

L0s é um estado de baixo consumo em que o transmissor de um lado do link é desligado. O receptor permanece ligado; o link pode ser restabelecido para L0 em menos de um microssegundo.

L0s é ativado automaticamente pela lógica do link quando nenhum tráfego foi enviado por alguns microssegundos. O PCIe host bridge da plataforma e a interface PCIe do dispositivo cooperam: quando o FIFO de transmissão está vazio e o ASPM está habilitado, o transmissor desliga. Quando novo tráfego chega, o transmissor volta a funcionar.

L0s é "assimétrico": cada lado entra e sai do estado de forma independente. O transmissor de um dispositivo pode estar em L0s enquanto o transmissor do root complex está em L0. Isso é útil porque o tráfego é tipicamente em rajadas: o CPU envia um gatilho de DMA e depois não envia mais nada por um tempo; o transmissor do CPU entra em L0s rapidamente, enquanto o transmissor do dispositivo permanece em L0 porque está enviando ativamente a resposta de DMA.

### L1: Standby de Ambos os Lados

L1 é um estado mais profundo em que ambos os transmissores estão desligados. Nenhum dos lados pode enviar nada até que o link seja restabelecido para L0; a latência é medida em microssegundos (de 5 a 65, dependendo da plataforma).

L1 é ativado após um período de ociosidade maior do que L0s. O limiar exato é configurável através das configurações do ASPM; os valores típicos são dezenas de microssegundos de inatividade. L1 economiza mais energia do que L0s, mas tem um custo maior para sair.

### L1.1 e L1.2: Sub-Estados Mais Profundos de L1

PCIe 3.0 e versões posteriores definem sub-estados de L1 que desligam partes adicionais da camada física. L1.1 (também chamado de "L1 PM Substate 1") mantém o clock em funcionamento, mas desliga mais circuitos; L1.2 também desliga o clock. As latências de wake aumentam (dezenas de microssegundos para L1.1; centenas para L1.2), mas o consumo de energia em ociosidade diminui.

A maioria dos laptops modernos usa L1.1 e L1.2 de forma agressiva para prolongar a vida da bateria. Um laptop que permanece em L1.2 na maior parte do tempo ocioso pode ter um consumo de energia PCIe na casa de um dígito em milivatts, em comparação com centenas de milivatts em L0.

### L2: Quase Desligado

L2 é o estado que o link assume quando o dispositivo está em D3cold. O link está efetivamente desligado; restabelecê-lo exige uma sequência completa de link-training (dezenas de milissegundos). L2 é atingido como parte da sequência de suspensão do sistema inteiro; o driver não o gerencia diretamente.

### Quem Controla o ASPM

O ASPM é um recurso por link configurado por meio dos registradores PCIe Link Capability e Link Control tanto no root complex quanto no dispositivo. A configuração especifica:

- Se o L0s está habilitado (campo de um bit).
- Se o L1 está habilitado (campo de um bit).
- Os limiares de latência de saída que a plataforma considera aceitáveis.

No FreeBSD, o ASPM é geralmente controlado pelo firmware da plataforma por meio do método `_OSC` do ACPI. O firmware informa ao sistema operacional quais capacidades gerenciar; se o firmware mantém o controle do ASPM, o sistema operacional não o toca. Se o firmware cede o controle, o sistema operacional pode habilitar ou desabilitar o ASPM por link com base em política.

Para o driver `myfirst` do Capítulo 22, o ASPM é responsabilidade da plataforma. O driver não configura o ASPM; ele não precisa saber se o link está em L0 ou L1 em nenhum momento. O estado do link é invisível para o driver do ponto de vista funcional (a latência é o único efeito observável).

### Quando o ASPM Importa para o Driver

Há situações específicas em que um driver precisa se preocupar com o ASPM:

1. **Errata conhecida.** Alguns dispositivos PCIe têm bugs em sua implementação de ASPM que causam o travamento do link ou produzem transações corrompidas. O driver pode precisar desabilitar explicitamente o ASPM para esses dispositivos. O kernel fornece acesso ao registrador PCIe Link Control por meio de `pcie_read_config` e `pcie_write_config` para essa finalidade.

2. **Dispositivos sensíveis à latência.** Um dispositivo de áudio ou vídeo em tempo real pode não tolerar a latência na escala de microssegundos do L1. O driver pode desabilitar o L1 mantendo o L0s habilitado.

3. **Dispositivos sensíveis ao consumo de energia.** Um dispositivo alimentado por bateria pode querer o L1.2 sempre habilitado. O driver pode forçar o L1.2 se o padrão da plataforma for menos agressivo.

Para o driver `myfirst`, nenhum desses casos se aplica. O dispositivo simulado não possui um link; o link PCIe real (se houver) é gerenciado pela plataforma. O capítulo menciona o ASPM por completude e segue em frente.

### Observando os Estados do Link

Em um sistema onde a plataforma suporta observação do ASPM, o estado do link é exposto por meio de `pciconf -lvbc`:

```sh
pciconf -lvbc | grep -A 20 myfirst
```

Procure por linhas como:

```text
cap 10[ac] = PCI-Express 2 endpoint max data 128(512) FLR NS
             link x1(x1) speed 5.0(5.0)
             ASPM disabled(L0s/L1)
             exit latency L0s 1us/<1us L1 8us/8us
             slot 0
```

O trecho "ASPM disabled" nessa linha indica que o ASPM não está ativo no momento. "disabled(L0s/L1)" indica que o dispositivo suporta L0s e L1, mas nenhum dos dois está habilitado. Em um sistema com ASPM agressivo, a linha exibiria "ASPM L1" ou algo semelhante.

As latências de saída informam ao driver quanto tempo leva a transição de volta ao L0; um driver sensível à latência pode decidir se o L1 é tolerável analisando esse número.

### Estado do Link e Consumo de Energia

Uma tabela aproximada de consumo de energia PCIe (valores típicos; os valores reais dependem da implementação):

| Estado | Consumo (link x1) | Latência de Saída |
|--------|-------------------|-------------------|
| L0     | 100-200 mW        | 0                 |
| L0s    | 50-100 mW         | <1 µs             |
| L1     | 10-30 mW          | 5-65 µs           |
| L1.1   | 1-5 mW            | 10-100 µs         |
| L1.2   | <1 mW             | 50-500 µs         |
| L2     | perto de 0        | 1-100 ms          |

Para um laptop com uma dúzia de links PCIe todos em L1.2 durante o modo ocioso, a economia agregada em relação ao estado all-L0 pode chegar a vários watts. Para um servidor com links de alta taxa de transferência sempre em L0, o ASPM é desabilitado e a economia de energia é zero.

O Capítulo 22 não implementa ASPM para o `myfirst`. O capítulo o menciona porque entender a máquina de estados do link faz parte da compreensão completa do gerenciamento de energia. Um leitor que futuramente trabalhar em um driver com errata de ASPM conhecida saberá onde procurar.



## Análise Detalhada: Fontes de Wake Explicadas

Fontes de wake são os mecanismos que trazem um sistema ou dispositivo suspenso de volta ao estado ativo. O Capítulo 1 as mencionou brevemente; esta análise mais aprofundada percorre as mais comuns.

### PME# no PCIe

A especificação PCI define o sinal `PME#` (Power Management Event). Quando ativado, ele informa ao root complex upstream que o dispositivo possui um evento que justifica o despertar do sistema. O root complex converte o PME# em um GPE ou interrupção ACPI, que o kernel trata.

Um dispositivo que suporta PME# possui uma capability de gerenciamento de energia PCI (verificada via `pci_has_pm`). O registrador de controle da capability inclui:

- **PME_En** (bit 8): habilita a geração de PME#.
- **PME_Status** (bit 15): definido pelo dispositivo quando PME# é ativado, limpo por software.
- **PME_Support** (somente leitura, bits 11-15 no registrador PMC): em quais D-states o dispositivo pode ativar PME# (D0, D1, D2, D3hot, D3cold).

A tarefa do driver é definir PME_En no momento certo (geralmente antes da suspensão) e limpar PME_Status no momento certo (geralmente após a retomada). Os helpers `pci_enable_pme(dev)` e `pci_clear_pme(dev)` realizam ambas as tarefas.

Em um laptop típico, o root complex roteia o PME# para um GPE ACPI, que o driver ACPI do kernel captura como um evento de wake. A cadeia tem a seguinte aparência:

```text
device asserts PME#
  → root complex receives PME
  → root complex sets GPE status bit
  → ACPI hardware interrupts CPU
  → kernel wakes from S3
  → kernel's ACPI driver services the GPE
  → eventually: DEVICE_RESUME on the device that woke
```

Toda a cadeia leva de um a três segundos. O papel do driver é mínimo: ele habilitou PME# antes da suspensão e limpará PME_Status após a retomada. Todo o restante é responsabilidade da plataforma.

### USB Remote Wakeup

O USB possui seu próprio mecanismo de wake chamado "remote wakeup". Um dispositivo USB solicita a capacidade de wake por meio de seu descritor padrão; o controlador host habilita a capacidade no momento da enumeração; quando o dispositivo ativa um sinal de resume em sua porta upstream, o controlador host o propaga.

Do ponto de vista de um driver FreeBSD, o remote wakeup USB é quase inteiramente tratado pelo driver do controlador host USB (`xhci`, `ohci`, `uhci`). Drivers individuais de dispositivos USB (para teclados, armazenamento, áudio, etc.) participam por meio dos callbacks de suspend e resume do framework USB, mas não lidam com PME# diretamente. O próprio PME# do controlador host USB é o que efetivamente desperta o sistema.

Para os fins do Capítulo 22, o wake USB é uma caixa preta que funciona por meio do driver do controlador host USB. Um leitor que futuramente escrever um driver de dispositivo USB aprenderá as convenções do framework naquele momento.

### Wake Baseado em GPIO em Plataformas Embarcadas

Em plataformas embarcadas (arm64, RISC-V), as fontes de wake são tipicamente pinos GPIO conectados à lógica de wake do SoC. A device tree descreve quais pinos são fontes de wake por meio de propriedades `wakeup-source` e `interrupts-extended` apontando para o controlador de wake.

O framework GPIO intr do FreeBSD trata esses casos. Um driver de dispositivo cujo hardware suporta wake lê a propriedade `wakeup-source` da device tree durante o attach, registra o GPIO como fonte de wake no framework e o framework faz o restante. O mecanismo é muito diferente do PCIe PME#, mas a API do lado do driver (marcar wake habilitado, limpar status de wake) é conceitualmente semelhante.

O Capítulo 22 não exercita o wake por GPIO; o driver `myfirst` é um dispositivo PCI. A Parte 7 revisita as plataformas embarcadas e aborda o caminho GPIO em detalhes.

### Wake on LAN (WoL)

Wake on LAN é um padrão de implementação específico para um controlador de rede. O controlador monitora os pacotes recebidos em busca de um "magic packet" (um padrão específico contendo o endereço MAC do controlador repetido muitas vezes) ou de padrões configurados pelo usuário. Quando uma correspondência é detectada, o controlador ativa PME# upstream.

Do ponto de vista do driver, o WoL requer:

1. Configurar a lógica de wake da NIC (filtro de magic packet, filtros de padrão) antes da suspensão.
2. Habilitar PME# via `pci_enable_pme`.
3. Na retomada, desabilitar a lógica de wake (porque o processamento normal de pacotes seria influenciado pelos filtros caso contrário).

A função `re_setwol` do driver `re(4)` é o exemplo canônico no FreeBSD. Um leitor que estiver construindo um driver de NIC copia sua estrutura e adapta a programação de registradores específica do dispositivo.

### Wake por Tampa, Botão de Energia e Outros

O sensor de abertura da tampa do laptop, o botão de energia, o teclado (em alguns casos) e outras entradas da plataforma são conectados à lógica de wake da plataforma por meio do ACPI. O driver ACPI trata o wake; drivers individuais de dispositivos não estão envolvidos.

O método `_PRW` do ACPI no objeto de um dispositivo no namespace ACPI declara qual GPE o evento de wake daquele dispositivo utiliza. O sistema operacional lê `_PRW` durante o boot para configurar o roteamento de wake. O driver `myfirst`, como um endpoint PCI simples sem fonte de wake específica da plataforma, não possui um método `_PRW`; sua capacidade de wake (se houver) é puramente por meio de PME#.

### Quando o Driver Deve Habilitar o Wake

Uma heurística simples: o driver deve habilitar o wake se o usuário o solicitou (por meio de um flag de capacidade de interface como `IFCAP_WOL` para NICs) e o hardware o suporta (`pci_has_pm` retorna verdadeiro, a própria lógica de wake do dispositivo está operacional). Caso contrário, o driver mantém o wake desabilitado.

Um driver que habilita o wake para cada dispositivo por padrão desperdiça energia da plataforma; o circuito de wake e o roteamento PME# consomem alguns milivatts continuamente. Um driver que nunca habilita o wake frustra usuários que querem que seu laptop desperte com um pacote de rede. A política é "habilitar somente quando solicitado".

As capacidades de interface do FreeBSD (definidas via `ifconfig em0 wol wol_magic`) são a forma padrão que os usuários expressam esse desejo. O driver da NIC lê os flags e configura o WoL de acordo.

### Testando Fontes de Wake

Testar o wake é mais difícil do que testar suspend e resume, porque exige que o sistema realmente entre em suspensão e depois que um evento externo o desperte. Abordagens comuns:

- **Magic packet de outra máquina.** Envie um magic packet WoL para o endereço MAC da máquina suspensa. Se o WoL estiver funcionando, a máquina desperta em alguns segundos.
- **Sensor de tampa.** Feche a tampa, aguarde, abra a tampa. Se o roteamento de wake da plataforma estiver funcionando, a máquina desperta ao abrir.
- **Botão de energia.** Pressione brevemente o botão de energia enquanto a máquina está suspensa. A máquina deve despertar.

Para um driver didático como `myfirst`, não há uma fonte de wake significativa para testar. O capítulo menciona a mecânica de wake por completude pedagógica, não porque o driver a exercita.



## Análise Detalhada: O Tunable `hw.pci.do_power_suspend`

Um dos tunables mais importantes para depuração de gerenciamento de energia é `hw.pci.do_power_suspend`. Ele controla se a camada PCI transita automaticamente os dispositivos para D3 durante a suspensão do sistema. Entender o que ele faz e quando alterá-lo vale uma análise dedicada.

### O Que o Padrão Faz

Com `hw.pci.do_power_suspend=1` (o padrão), o helper `pci_suspend_child` da camada PCI, após chamar o `DEVICE_SUSPEND` do driver, transita o dispositivo para D3hot chamando `pci_set_power_child(dev, child, PCI_POWERSTATE_D3)`. Na retomada, `pci_resume_child` transita de volta para D0.

Este é o modo "power-save". Um dispositivo que suporta D3 usa seu estado ocioso de menor consumo durante a suspensão. Um laptop se beneficia porque a duração da bateria durante o sono é estendida; um dispositivo que pode dormir a alguns milivatts em vez de algumas centenas de milivatts justifica a transição adicional de D-state.

### O Que `hw.pci.do_power_suspend=0` Faz

Com o tunable definido como 0, a camada PCI não transita o dispositivo para D3. O dispositivo permanece em D0 durante toda a suspensão. O `DEVICE_SUSPEND` do driver é executado; o driver encerra a atividade; o dispositivo permanece energizado.

Do ponto de vista de economia de energia, isso é pior: o dispositivo continua consumindo seu orçamento de energia D0 durante o sono. Do ponto de vista de corretude, pode ser melhor para alguns dispositivos:

- Um dispositivo com implementação D3 defeituosa pode se comportar incorretamente quando transitado. Permanecer em D0 evita o bug de transição.
- Um dispositivo cujo contexto é custoso para salvar e restaurar pode preferir permanecer em D0 durante uma suspensão breve. Se a suspensão durar apenas alguns segundos, o custo de salvar o contexto supera o benefício de economia de energia.
- Um dispositivo crítico para a função principal da máquina (um teclado de console, por exemplo) pode precisar permanecer alerta mesmo durante a suspensão.

### Quando Alterá-lo

Para desenvolvimento e depuração, definir `hw.pci.do_power_suspend=0` pode isolar bugs:

- Se um bug de retomada aparecer apenas com o tunable em 1, o bug está na transição de D3 para D0 (seja na restauração de configuração da camada PCI, seja no tratamento pelo driver de um dispositivo que foi reiniciado).
- Se um bug de retomada aparecer também com o tunable em 0, o bug está no código `DEVICE_SUSPEND` ou `DEVICE_RESUME` do driver, não na maquinaria de D-state.

Em produção, o valor padrão (1) é quase sempre o correto. Alterá-lo globalmente afeta todos os dispositivos PCI do sistema; a abordagem mais adequada é fazer uma substituição por dispositivo quando isso for necessário, e essa lógica normalmente fica no próprio driver.

### Verificando se o Tunable Está em Vigor

Uma forma rápida de verificar é checar o estado de energia do dispositivo com `pciconf` antes e depois de um suspend:

```sh
# Before suspend (device should be in D0):
pciconf -lvbc | grep -A 5 myfirst

# With hw.pci.do_power_suspend=1 (default):
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 5 myfirst
# "powerspec" should show D3

# With hw.pci.do_power_suspend=0:
sudo sysctl hw.pci.do_power_suspend=0
sudo devctl resume myfirst0
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 5 myfirst
# "powerspec" should show D0

# Reset to default.
sudo sysctl hw.pci.do_power_suspend=1
sudo devctl resume myfirst0
```

A linha `powerspec` na saída de `pciconf -lvbc` mostra o estado de energia atual. Observar a mudança entre D0 e D3 confirma que a transição automática está acontecendo.

### Interação com pci_save_state

Quando `hw.pci.do_power_suspend` é 1, a camada PCI chama `pci_cfg_save` automaticamente antes de fazer a transição para D3. Quando é 0, a camada PCI não chama `pci_cfg_save`.

Isso tem uma implicação sutil: se o driver quiser salvar a configuração explicitamente no caso 0, ele mesmo precisa chamar `pci_save_state`. O padrão do Capítulo 22 assume o valor padrão (1) e não chama `pci_save_state` explicitamente; um driver que precise dar suporte aos dois modos precisaria de lógica adicional.

### O Tunable Afeta o Suspend do Sistema ou o suspend via devctl?

Ambos. `pci_suspend_child` é chamado tanto para `acpiconf -s 3` quanto para `devctl suspend`, e o tunable controla a transição de estado D nos dois casos. Um leitor depurando com `devctl suspend` verá o mesmo comportamento que com um suspend completo do sistema, descontado o restante do trabalho de plataforma (estacionamento das CPUs, entrada no estado de sleep do ACPI).

### Um Cenário Concreto de Depuração

Suponha que o método resume do driver `myfirst` falhe de forma intermitente: às vezes funciona, às vezes `dma_test_read` após o resume retorna EIO. Os contadores estão consistentes (suspend count = resume count), os logs mostram que ambos os métodos foram executados, mas o DMA pós-resume falha.

**Hipótese 1.** A transição de D3 para D0 está produzindo um estado inconsistente no dispositivo. Verifique definindo `hw.pci.do_power_suspend=0` e repetindo o teste.

Se o bug desaparecer com o tunable em 0, a maquinaria de estado D está envolvida. A correção pode estar no caminho de resume do driver (adicionar um atraso após a transição para deixar o dispositivo estabilizar), na restauração de configuração feita pela camada PCI, ou no próprio dispositivo.

**Hipótese 2.** O bug está no código de suspend/resume do próprio driver, independentemente do D3. Verifique definindo o tunable como 0 e repetindo o teste.

Se o bug persistir com o tunable em 0, o código do driver é o problema. A transição D3 não tem culpa.

Esse tipo de bisseção é comum na depuração de gerenciamento de energia. O tunable é a ferramenta que permite isolar a variável.



## Análise Detalhada: DEVICE_QUIESCE e Quando Você Precisa Dele

A Seção 2 mencionou brevemente `DEVICE_QUIESCE` como o terceiro método de gerenciamento de energia, ao lado de `DEVICE_SUSPEND` e `DEVICE_SHUTDOWN`. Ele raramente é implementado explicitamente em drivers FreeBSD; uma busca em `/usr/src/sys/dev/` mostra apenas um punhado de drivers que definem seu próprio `device_quiesce`. Vale dedicar uma seção curta para entender quando você de fato precisa dele e quando não precisa.

### Para Que Serve o DEVICE_QUIESCE

O wrapper `device_quiesce` em `/usr/src/sys/kern/subr_bus.c` é chamado em vários lugares:

- `devclass_driver_deleted`: quando um driver está sendo descarregado, o framework chama `device_quiesce` em cada instância antes de chamar `device_detach`.
- `DEV_DETACH` via devctl: quando o usuário executa `devctl detach myfirst0`, o kernel chama `device_quiesce` antes de `device_detach`, a menos que o flag `-f` (forçar) seja fornecido.
- `DEV_DISABLE` via devctl: quando o usuário executa `devctl disable myfirst0`, o kernel chama `device_quiesce` de forma similar.

Em cada caso, o quiesce é uma pré-verificação: "o driver consegue parar com segurança o que está fazendo?". Um driver que retorna EBUSY de `DEVICE_QUIESCE` impede o detach ou disable subsequente. O usuário recebe um erro e o driver permanece anexado.

### O Que o Comportamento Padrão Faz

Se um driver não implementa `DEVICE_QUIESCE`, o padrão (`null_quiesce` em `device_if.m`) retorna 0 incondicionalmente. O kernel prossegue com o detach ou disable.

Para a maioria dos drivers, isso é suficiente. O caminho de detach do driver lida com qualquer trabalho em andamento, de modo que não há nada que o quiesce faria que o detach também não faça.

### Quando Você Implementaria Isso

Um driver implementa `DEVICE_QUIESCE` explicitamente quando:

1. **Retornar EBUSY é mais informativo do que esperar.** Se o driver tem um conceito de "ocupado" (uma transferência em curso, uma contagem de descritores de arquivo abertos, uma montagem de sistema de arquivos), e o usuário pode aguardar até que ele fique disponível, o driver pode recusar o quiesce enquanto a condição de ocupado for verdadeira. `DEVICE_QUIESCE` retornando EBUSY informa ao usuário: "o dispositivo está ocupado; aguarde e tente novamente".

2. **O quiesce pode ser feito mais rapidamente do que um detach completo.** Se o detach é caro (libera grandes tabelas de recursos, drena filas lentas) mas o dispositivo pode ser parado de forma econômica, `DEVICE_QUIESCE` permite que o kernel verifique a prontidão sem pagar o custo do detach.

3. **O driver quer distinguir quiesce de suspend.** Se o driver quer parar a atividade sem salvar o estado (porque não haverá um resume), implementar quiesce separado do suspend é uma forma de expressar essa distinção em código.

Para o driver `myfirst`, nenhuma dessas situações se aplica. O caminho de detach do Capítulo 21 já lida com o trabalho em andamento; o caminho de suspend do Capítulo 22 cuida do quiesce no sentido do gerenciamento de energia. Adicionar um `DEVICE_QUIESCE` separado seria redundante.

### Um Exemplo do bce(4)

O driver Broadcom NetXtreme em `/usr/src/sys/dev/bce/if_bce.c` possui uma entrada `DEVMETHOD(device_quiesce, bce_quiesce)` comentada em sua tabela de métodos. O comentário sugere que o autor considerou implementar o quiesce, mas não o fez. Isso é comum: muitos drivers mantêm a linha comentada como um TODO que nunca é implementado, porque o comportamento padrão já atende ao seu caso de uso.

A implementação, caso o driver a habilitasse, pararia os caminhos de TX e RX da NIC sem liberar os recursos de hardware. Um `device_detach` subsequente faria a liberação de fato. A separação entre "parar" e "liberar" é o que `DEVICE_QUIESCE` expressaria.

### Relação com DEVICE_SUSPEND

`DEVICE_QUIESCE` e `DEVICE_SUSPEND` fazem coisas semelhantes: param a atividade do dispositivo. As diferenças:

- **Ciclo de vida**: o quiesce fica entre o estado em execução e o detach; o suspend fica entre o estado em execução e o eventual resume.
- **Recursos**: o quiesce não exige que o driver salve nenhum estado; o suspend exige.
- **Capacidade de vetar**: ambos podem retornar EBUSY; as consequências diferem (quiesce impede o detach; suspend impede a transição de energia).

Um driver que implementa os dois geralmente compartilha código: `foo_quiesce` pode fazer "parar atividade" e `foo_suspend` pode fazer "chamar quiesce; salvar estado; retornar". O helper `myfirst_quiesce` do driver `myfirst` é o código compartilhado; o capítulo não o conecta a um método `DEVICE_QUIESCE`, mas fazer isso seria uma pequena adição.

### Uma Adição Opcional ao myfirst

Como exercício desafio, você pode adicionar `DEVICE_QUIESCE` ao `myfirst`:

```c
static int
myfirst_pci_quiesce(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "quiesce: starting\n");
        (void)myfirst_quiesce(sc);
        atomic_add_64(&sc->power_quiesce_count, 1);
        device_printf(dev, "quiesce: complete\n");
        return (0);
}
```

E a entrada correspondente na tabela de métodos:

```c
DEVMETHOD(device_quiesce, myfirst_pci_quiesce),
```

Para testar: `devctl detach myfirst0` chama o quiesce antes do detach; você pode verificar isso lendo `dev.myfirst.0.power_quiesce_count` imediatamente antes de o detach entrar em vigor.

O exercício é curto e não altera a estrutura geral do driver; apenas conecta mais um método. O Stage 4 consolidado do Capítulo 22 não o inclui por padrão, mas quem quiser o método pode adicioná-lo em poucas linhas.



## Laboratórios Práticos

O Capítulo 22 inclui três laboratórios práticos que exercitam o caminho de gerenciamento de energia de formas progressivamente mais exigentes. Cada laboratório possui um script em `examples/part-04/ch22-power/labs/` que você pode executar diretamente, além de sugestões de extensão.

### Laboratório 1: Ciclo Único de Suspend-Resume

O primeiro laboratório é o mais simples: um ciclo limpo de suspend-resume com verificação dos contadores.

**Preparação.** Carregue o driver Stage 4 do Capítulo 22:

```sh
cd examples/part-04/ch22-power/stage4-final
make clean && make
sudo kldload ./myfirst.ko
```

Verifique o attach:

```sh
sysctl dev.myfirst.0.%driver
# Should return: myfirst
sysctl dev.myfirst.0.suspended
# Should return: 0
```

**Execução.** Execute o script do ciclo:

```sh
sudo sh ../labs/ch22-suspend-resume-cycle.sh
```

Saída esperada:

```text
PASS: one suspend-resume cycle completed cleanly
```

**Verificação.** Inspecione os contadores:

```sh
sysctl dev.myfirst.0.power_suspend_count
# Should return: 1
sysctl dev.myfirst.0.power_resume_count
# Should return: 1
```

Verifique o `dmesg`:

```sh
dmesg | tail -6
```

Devem aparecer quatro linhas (início do suspend, suspend completo, início do resume, resume completo) mais as linhas de log de transferência antes e depois do ciclo.

**Extensão.** Modifique o script do ciclo para executar dois ciclos de suspend-resume em vez de um, e verifique que os contadores incrementam exatamente 2 em cada um.

### Laboratório 2: Stress de Cem Ciclos

O segundo laboratório executa o script do ciclo cem vezes seguidas e verifica que nada se desvia.

**Execução.**

```sh
sudo sh ../labs/ch22-suspend-stress.sh
```

Saída esperada após alguns segundos:

```text
PASS: 100 cycles
```

**Verificação.** Após o stress, os contadores devem ser cada um igual a 100 (ou 100 mais o que havia antes):

```sh
sysctl dev.myfirst.0.power_suspend_count
# 100 (or however many cycles were added)
```

**Observações a fazer.**

- Quanto tempo leva um ciclo? Na simulação, deve ser alguns milissegundos. Em hardware real com transições de estado D, espere de algumas centenas de microssegundos a alguns milissegundos.
- O load average do sistema muda durante o stress? A simulação é barata; cem ciclos em uma máquina moderna mal devem ser perceptíveis.
- O que acontece se você executar o teste de DMA durante o stress? (`sudo sysctl dev.myfirst.0.dma_test_read=1` simultaneamente ao loop de ciclos.) Um driver bem escrito deve lidar com isso de forma elegante; o teste de DMA tem sucesso se ocorrer durante uma janela `RUNNING` e falha com EBUSY ou similar se ocorrer durante uma transição.

**Extensão.** Execute o script de stress com `dmesg -c` antes para limpar o log, depois verifique:

```sh
dmesg | wc -l
```

Deve ser próximo de 400 (quatro linhas de log por ciclo, vezes 100 ciclos). Uma contagem de linhas de log por ciclo permite verificar que cada ciclo de fato foi executado pelo driver.

### Laboratório 3: Transferência Durante um Ciclo

O terceiro laboratório é o mais difícil: ele inicia uma transferência DMA e imediatamente suspende no meio dela, depois resume e verifica se o driver se recupera.

**Preparação.** O script do laboratório é `ch22-transfer-across-cycle.sh`. Ele executa uma transferência DMA em background, dorme alguns milissegundos, chama `devctl suspend`, dorme novamente, chama `devctl resume` e então inicia outra transferência.

**Execução.**

```sh
sudo sh ../labs/ch22-transfer-across-cycle.sh
```

**Observações a fazer.**

- A primeira transferência é concluída, retorna erro, ou atinge timeout? O comportamento esperado é que o quiesce a aborte de forma limpa; a transferência reporta EIO ou ETIMEDOUT.
- O contador `dma_errors` ou `dma_timeouts` é incrementado? Um deles deve ser.
- `dma_in_flight` volta para false após o suspend?
- A transferência pós-resume tem sucesso normalmente? Se sim, o estado do driver é consistente e o ciclo funcionou.

**Extensão.** Reduza o sleep entre o início da transferência e o suspend para atingir o caso de borda em que a transferência está sendo executada no momento exato do suspend. É aí que vivem as condições de corrida; um driver que passa nesse teste sob timing agressivo tem uma implementação de quiesce sólida.

### Laboratório 4: PM em Tempo de Execução (Opcional)

Para leitores que compilam com `MYFIRST_ENABLE_RUNTIME_PM`, um quarto laboratório exercita o caminho de PM em tempo de execução.

**Preparação.** Recompile com o PM em tempo de execução habilitado:

```sh
cd examples/part-04/ch22-power/stage4-final
# Uncomment the CFLAGS line in the Makefile:
#   CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM
make clean && make
sudo kldload ./myfirst.ko
```

**Execução.**

```sh
sudo sh ../labs/ch22-runtime-pm.sh
```

O script:

1. Define o limiar de ociosidade para 3 segundos (em vez dos 5 segundos padrão).
2. Registra os contadores de linha de base.
3. Aguarda 5 segundos sem nenhuma atividade.
4. Verifica que `runtime_state` é `RUNTIME_SUSPENDED`.
5. Aciona uma transferência DMA.
6. Verifica que `runtime_state` voltou para `RUNNING`.
7. Exibe PASS.

**Observações a fazer.**

- Durante a espera de ociosidade, o `dmesg` deve mostrar a linha de log "runtime suspend" aproximadamente 3 segundos depois.
- `runtime_suspend_count` e `runtime_resume_count` devem ser cada um igual a 1 ao final.
- A transferência DMA deve ter sucesso normalmente após o runtime resume.

**Extensão.** Defina o limiar de ociosidade para 1 segundo. Execute o teste de DMA repetidamente em um loop apertado. Você não deve ver nenhuma transição de runtime-suspend durante o loop (porque cada teste reinicia o timer de ociosidade), mas assim que o loop parar, o runtime suspend será acionado.

### Notas sobre os Laboratórios

Todos os laboratórios assumem que o driver está carregado e o sistema está ocioso o suficiente para que as transições aconteçam sob demanda. Se outro processo estiver usando o dispositivo ativamente (improvável para o `myfirst`, mas comum em configurações reais), os contadores se desviam por valores inesperados e as verificações de incremento exato dos scripts falham. Os scripts foram projetados para um ambiente de teste tranquilo, não para um ambiente com muito uso concorrente.

Para testes realistas com o driver `re(4)` ou outros drivers de produção, a mesma estrutura de script se aplica com o nome do dispositivo ajustado. A sequência `devctl suspend`/`devctl resume` funciona para qualquer dispositivo PCI que o kernel gerencia.

## Exercícios Desafio

Os exercícios desafio do Capítulo 22 levam o leitor além do driver de base e exploram território que drivers do mundo real eventualmente precisam lidar. Cada exercício foi dimensionado para ser alcançável com o material do capítulo e algumas horas de trabalho.

### Desafio 1: Implemente um Mecanismo de Wake via Sysctl

Estenda o driver `myfirst` com uma fonte de wake simulada. A simulação já possui um callout que pode disparar; adicione um novo recurso à simulação que define um bit de "wake" no dispositivo enquanto ele está em D3, e faça com que o caminho `DEVICE_RESUME` do driver registre o evento de wake.

**Dicas.**

- Adicione um registrador `MYFIRST_REG_WAKE_STATUS` ao backend de simulação.
- Adicione um registrador `MYFIRST_REG_WAKE_ENABLE` que o driver escreve durante o suspend.
- Faça o callout da simulação definir o bit de status de wake após um atraso aleatório.
- No resume, o driver lê o registrador e registra se um wake foi observado.

**Verificação.** Após `devctl suspend; sleep 1; devctl resume`, o log deve exibir o status de wake. Uma chamada subsequente a `sysctl dev.myfirst.0.wake_events` deve incrementar o contador.

**Por que isso é importante.** O tratamento de fontes de wake é uma das partes mais delicadas do gerenciamento de energia em hardware real. Incorporá-lo à simulação permite que o leitor exercite o contrato completo sem precisar de hardware físico.

### Desafio 2: Salve e Restaure um Anel de Descritores

A simulação do Capítulo 21 ainda não utiliza um anel de descritores (as transferências ocorrem uma de cada vez). Estenda a simulação com um pequeno anel de descritores, programe seu endereço base por meio de um registrador no attach e faça o caminho de suspend salvar o endereço base do anel no estado do softc. Faça o caminho de resume escrever de volta o endereço base salvo.

**Dicas.**

- O endereço base do anel é um `bus_addr_t` mantido no softc.
- O registrador é `MYFIRST_REG_RING_BASE_LOW`/`_HIGH`.
- Salvar e restaurar é trivial; o objetivo é verificar que *não* salvar e restaurar quebraria o funcionamento.

**Verificação.** Após suspend-resume, o registrador de endereço base do anel deve conter o mesmo valor de antes. Sem a restauração, ele deve conter zero.

**Por que isso é importante.** Anéis de descritores são o que drivers de alto throughput reais utilizam; um driver com suporte a energia que possui um anel precisa restaurar o endereço base a cada resume. Este exercício é um degrau em direção ao tipo de gerenciamento de estado que drivers de produção como `re(4)` e `em(4)` realizam.

### Desafio 3: Implemente uma Política de Veto

Estenda o caminho de suspend com um controle de política que permite ao usuário especificar se o driver deve vetar um suspend quando o dispositivo estiver ocupado. Especificamente:

- Adicione `dev.myfirst.0.suspend_veto_if_busy` como um sysctl de leitura e escrita.
- Se o sysctl for 1 e uma transferência DMA estiver em andamento, `myfirst_power_suspend` retorna EBUSY sem executar o quiesce.
- Se o sysctl for 0 (padrão), o suspend sempre é bem-sucedido.

**Dicas.** Defina `suspend_veto_if_busy` como 1. Inicie uma transferência DMA longa (adicione um `DELAY` ao engine da simulação para fazê-la durar um ou dois segundos). Chame `devctl suspend myfirst0` durante a transferência. Verifique que o suspend retorna um erro e que `dev.myfirst.0.suspended` permanece 0.

**Verificação.** O caminho de desfazimento do kernel é executado; o driver permanece em `RUNNING`; a transferência é concluída normalmente.

**Por que isso é importante.** O veto é uma ferramenta eficaz e também perigosa. As decisões de política do mundo real sobre vetar ou não são repletas de nuances (drivers de armazenamento costumam vetar; drivers de NIC geralmente não). Implementar o mecanismo torna a questão de política concreta e tangível.

### Desafio 4: Adicione um Autoteste Pós-Resume

Após o resume, execute um teste mínimo e viável do dispositivo: escreva um padrão conhecido no buffer de DMA, acione uma transferência de escrita, releia-o com uma transferência de leitura e verifique. Se o teste falhar, marque o dispositivo como defeituoso e faça as operações subsequentes falharem.

**Dicas.**

- Adicione o autoteste como uma função auxiliar que é executada a partir de `myfirst_power_resume` após `myfirst_restore`.
- Use um padrão bem conhecido, como `0xDEADBEEF`.
- Use o caminho DMA existente; o autoteste é apenas uma escrita e uma leitura.

**Verificação.** Em operação normal, o autoteste sempre é aprovado. Para verificar que ele detecta falhas, adicione um mecanismo artificial de "falha única" à simulação e acione-o; o driver deve registrar a falha e se marcar como defeituoso.

**Por que isso é importante.** Autotestes são uma forma leve de engenharia de confiabilidade. Um driver que detecta suas próprias falhas em pontos bem definidos é mais fácil de depurar do que um que corrompe dados silenciosamente até que um usuário perceba.

### Desafio 5: Implemente pci_save_state / pci_restore_state de Forma Manual

A maioria dos drivers deixa a camada PCI cuidar do salvamento e da restauração do espaço de configuração automaticamente. Estenda o driver do Capítulo 22 para fazer isso manualmente de forma opcional, controlado pelo sysctl `dev.myfirst.0.manual_pci_save`.

**Dicas.**

- Leia `hw.pci.do_power_suspend` e `hw.pci.do_power_resume` e defina-os como 0 quando o modo manual estiver ativado.
- Chame `pci_save_state` explicitamente no caminho de suspend e `pci_restore_state` no caminho de resume.
- Verifique que o dispositivo ainda funciona após suspend-resume.

**Verificação.** O dispositivo deve funcionar de forma idêntica independentemente de o modo manual estar ativado ou não. Defina o sysctl antes de um teste de estresse e verifique que não há divergências.

**Por que isso é importante.** Alguns drivers reais precisam de salvamento e restauração manual porque o tratamento automático da camada PCI interfere com peculiaridades específicas do dispositivo. Saber quando e como assumir o controle do salvamento e restauração é uma habilidade intermediária valiosa.



## Referência para Solução de Problemas

Esta seção reúne os problemas mais comuns que o leitor pode encontrar ao trabalhar no Capítulo 22, com um diagnóstico curto e uma correção para cada um. A lista foi projetada para ser percorrida rapidamente; se um problema corresponder ao seu caso, vá diretamente à entrada correspondente.

### "devctl: DEV_SUSPEND failed: Operation not supported"

O driver não implementa `DEVICE_SUSPEND`. Ou a tabela de métodos está sem a linha `DEVMETHOD(device_suspend, ...)`, ou o driver não foi recompilado e recarregado.

**Correção.** Verifique a tabela de métodos. Recompile com `make clean && make`. Descarregue e recarregue.

### "devctl: DEV_SUSPEND failed: Device busy"

O driver retornou `EBUSY` a partir de `DEVICE_SUSPEND`, provavelmente por causa da lógica de veto do Desafio 3, ou porque o dispositivo está genuinamente ocupado (DMA em andamento, tarefa em execução) e o driver optou por vetar.

**Correção.** Verifique se o controle `suspend_veto_if_busy` está definido. Verifique `dma_in_flight`. Aguarde a conclusão da atividade antes de suspender.

### "devctl: DEV_RESUME failed"

`DEVICE_RESUME` retornou um valor diferente de zero. O log deve conter mais detalhes.

**Correção.** Verifique `dmesg | tail`. A linha de log do resume deve indicar o que falhou. Geralmente é uma etapa de inicialização específica do hardware que não foi concluída com sucesso.

### O dispositivo está suspenso, mas `dev.myfirst.0.suspended` mostra 0

O flag do driver está fora de sincronia com o estado do kernel. Provavelmente é um bug no caminho de quiesce: o flag nunca foi definido, ou foi limpo prematuramente.

**Correção.** Adicione um `KASSERT(sc->suspended == true)` no início do caminho de resume; execute com `INVARIANTS` para capturar o bug.

### `power_suspend_count != power_resume_count`

Um ciclo obteve um lado mas não o outro. Verifique o `dmesg` em busca de erros; o log deve mostrar onde a sequência foi interrompida.

**Correção.** Corrija o caminho de código que está faltando. Geralmente é um retorno antecipado sem a atualização do contador.

### Transferências DMA falham após o resume

O caminho de restore não reinicializou o engine de DMA. Verifique o registrador INTR_MASK, os registradores de controle de DMA e o valor de `saved_intr_mask`. Ative o log detalhado para ver a sequência de restauração do caminho de resume.

**Correção.** Adicione a escrita de registrador ausente em `myfirst_restore`.

### WITNESS reclama de um lock mantido durante o suspend

O caminho de suspend adquiriu um lock e depois chamou uma função que dorme ou tenta adquirir outro lock. Leia a mensagem do WITNESS para identificar os nomes dos locks problemáticos.

**Correção.** Libere o lock antes da chamada que dorme, ou reestruture o código para que o lock seja adquirido apenas quando necessário.

### O sistema não acorda do S3

Um driver abaixo de `myfirst` está bloqueando o resume. É improvável que seja o próprio `myfirst`, a menos que os logs mostrem um erro especificamente desse driver.

**Correção.** Inicialize no modo single-user, ou carregue menos drivers, e faça uma bisseção. Verifique o `dmesg` no sistema em execução para identificar o driver problemático.

### Runtime PM nunca dispara

O callout de monitoramento de ociosidade não está sendo executado, ou o timestamp `last_activity` está sendo atualizado com muita frequência.

**Correção.** Verifique se `callout_reset` está sendo chamado a partir do caminho de attach. Verifique se `myfirst_mark_active` não está sendo chamado a partir de caminhos de código inesperados. Adicione logging ao callback do callout para confirmar que ele dispara.

### Kernel panic durante o suspend

Um KASSERT falhou (em um kernel com `INVARIANTS`) ou um lock está sendo mantido incorretamente. A mensagem de pânico identifica o arquivo e a linha problemáticos.

**Correção.** Leia a mensagem de pânico. Localize o arquivo e a linha no código. A correção geralmente é direta uma vez que o local é identificado.



## Encerrando

O Capítulo 22 encerra a Parte 4 conferindo ao driver `myfirst` a disciplina do gerenciamento de energia. No início, o `myfirst` na versão `1.4-dma` era um driver capaz: ele se anexava a um dispositivo PCI, tratava interrupções multi-vetor, movia dados via DMA e liberava seus recursos no detach. O que lhe faltava era a capacidade de participar das transições de energia do sistema. Ele travaria, vazaria memória ou falharia silenciosamente se o usuário fechasse a tampa do notebook ou pedisse ao kernel para suspender o dispositivo. Ao final, o `myfirst` na versão `1.5-power` trata cada transição de energia que o kernel pode impor: suspend do sistema para S3 ou S4, suspend por dispositivo via `devctl`, shutdown do sistema e gerenciamento de energia em tempo de execução opcional.

As oito seções percorreram a progressão completa. A Seção 1 estabeleceu o panorama geral: por que um driver se preocupa com energia, o que são os S-states do ACPI e os D-states do PCI, o que os L-states do PCIe e o ASPM acrescentam, e como são as fontes de wake. A Seção 2 apresentou as APIs concretas do FreeBSD: os métodos `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN` e `DEVICE_QUIESCE`, os auxiliares `bus_generic_suspend` e `bus_generic_resume`, e o salvamento e restauração automática do espaço de configuração pela camada PCI. O esqueleto do Estágio 1 fez os métodos registrarem e contarem transições sem executar nenhum trabalho real. A Seção 3 transformou o esqueleto de suspend em um quiesce real: mascarar interrupções, drenar DMA, drenar workers, nessa ordem, com funções auxiliares compartilhadas entre suspend e detach. A Seção 4 escreveu o caminho de resume correspondente: reativar o bus-master, restaurar o estado específico do dispositivo, limpar o flag de suspended e desmascarar interrupções. A Seção 5 adicionou gerenciamento de energia em tempo de execução opcional com um callout de monitoramento de ociosidade e transições explícitas de `pci_set_powerstate`. A Seção 6 examinou a interface do espaço do usuário: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v` e os sysctls correspondentes. A Seção 7 catalogou os modos de falha característicos e seus padrões de depuração. A Seção 8 refatorou o código em `myfirst_power.c`, atualizou a versão para `1.5-power`, adicionou `POWER.md` e conectou o teste de regressão final.

O que o Capítulo 22 não abordou foi o gerenciamento de energia com scatter-gather para drivers multi-fila (esse é um tópico da Parte 6, Capítulo 28), a integração com hotplug e remoção inesperada (um tópico da Parte 7), os domínios de energia em plataformas embarcadas (novamente a Parte 7) ou os internos do interpretador AML do ACPI (nunca abordados neste livro). Cada um desses é uma extensão natural construída sobre as primitivas do Capítulo 22, e cada um pertence a um capítulo posterior onde o escopo é compatível. A fundação está estabelecida; as especializações acrescentam vocabulário sem precisar de uma nova base.

O layout de arquivos cresceu: 16 arquivos de código-fonte (incluindo `cbuf`), 8 arquivos de documentação (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, `POWER.md`) e uma suíte de regressão estendida que cobre todos os subsistemas. O driver é estruturalmente paralelo aos drivers de produção do FreeBSD; um leitor que tenha estudado os Capítulos 16 a 22 pode abrir `if_re.c`, `if_xl.c` ou `virtio_blk.c` e reconhecer cada parte arquitetural: acessores de registradores, backend de simulação, PCI attach, filtro de interrupção e task, maquinário por vetor, setup e teardown de DMA, disciplina de sincronização, power suspend, power resume, detach limpo.

### Uma Reflexão Antes do Capítulo 23

O Capítulo 22 é o último capítulo da Parte 4, e a Parte 4 é a parte que ensinou ao leitor como um driver conversa com o hardware. Os Capítulos 16 a 21 introduziram as primitivas: MMIO, simulação, PCI, interrupções, interrupções multi-vetor, DMA. O Capítulo 22 introduziu a disciplina: como essas primitivas sobrevivem às transições de energia. Juntos, os sete capítulos levam o leitor de "nenhuma ideia do que é um driver" a "um driver multi-subsistema funcional que lida com todos os eventos de hardware que o kernel pode lançar contra ele".

O ensinamento do Capítulo 22 se generaliza. Um leitor que internalizou o padrão suspend-quiesce-save-restore, a interação entre o driver e a camada PCI, a máquina de estados do runtime-PM e os padrões de depuração encontrará formas semelhantes em todo driver FreeBSD com consciência de energia. O hardware específico difere; a estrutura não. Um driver para um NIC, um controlador de armazenamento, uma GPU ou um controlador host USB aplica o mesmo vocabulário ao seu próprio hardware.

A Parte 5, que começa com o Capítulo 23, muda o foco. A Parte 4 tratava da direção driver-para-hardware: como o driver conversa com o dispositivo. A Parte 5 é sobre a direção driver-para-kernel: como o driver é depurado, rastreado, instrumentado e estressado pelas pessoas que o mantêm. O Capítulo 23 inicia essa mudança com técnicas de depuração e rastreamento que se aplicam a todos os subsistemas de drivers.

### O Que Fazer Se Você Estiver Travado

Três sugestões.

Primeiro, concentre-se nos caminhos de suspend do Estágio 2 e de resume do Estágio 3. Se `devctl suspend myfirst0` seguido de `devctl resume myfirst0` tiver sucesso e uma transferência DMA subsequente funcionar, o núcleo do capítulo está funcionando. Todas as outras partes do capítulo são opcionais no sentido de que decoram o pipeline, mas se o pipeline falhar, o capítulo não está funcionando e a Seção 3 ou a Seção 4 é o lugar certo para fazer o diagnóstico.

Segundo, abra `/usr/src/sys/dev/re/if_re.c` e releia `re_suspend`, `re_resume` e `re_setwol`. Cada função tem cerca de trinta linhas. Cada linha corresponde a um conceito do Capítulo 22. Lê-las uma vez após concluir o capítulo deve parecer território familiar; os padrões do driver real parecerão elaborações dos padrões mais simples do capítulo.

Terceiro, pule os desafios na primeira passagem. Os laboratórios são calibrados para o ritmo do Capítulo 22; os desafios pressupõem que o material do capítulo esteja sólido. Volte a eles depois do Capítulo 23 se parecerem fora de alcance agora.

O objetivo do Capítulo 22 era dar ao driver a disciplina de gerenciamento de energia. Se isso foi alcançado, a maquinaria de depuração e rastreamento do Capítulo 23 se torna uma generalização do que você já faz instintivamente, e não um novo tópico.

## Ponto de Verificação da Parte 4

A Parte 4 foi o trecho mais longo e denso do livro até agora. Sete capítulos cobriram recursos de hardware, I/O de registradores, attach PCI, interrupções, MSI e MSI-X, DMA e gerenciamento de energia. Antes de a Parte 5 mudar o modo de "escrever drivers" para "depurá-los e rastreá-los", confirme que a história voltada ao hardware está internalizada.

Ao final da Parte 4, você deve ser capaz de fazer cada uma das seguintes coisas sem precisar pesquisar:

- Reivindicar um recurso de hardware com `bus_alloc_resource_any` ou `bus_alloc_resource_anywhere`, acessá-lo por meio das primitivas de leitura/escrita e barreira do `bus_space(9)`, e liberá-lo corretamente no detach.
- Ler e escrever registradores de dispositivo por meio da abstração `bus_space(9)` em vez de desreferências de ponteiro diretas, com a disciplina correta de barreira em torno de sequências que não devem ser reordenadas.
- Identificar um dispositivo PCI por IDs de vendor, device, subvendor e subdevice; reivindicar seus BARs; e sobreviver a um detach forçado sem vazar recursos.
- Registrar um filtro de metade superior junto com uma tarefa de metade inferior ou ithread via `bus_setup_intr`, na ordem que o kernel exige, e desmontá-los na ordem inversa durante o detach.
- Configurar vetores MSI ou MSI-X com uma escada de fallback elegante de MSI-X para MSI e depois para INTx legado, e vincular vetores a CPUs específicas quando a carga de trabalho exigir.
- Alocar, mapear, sincronizar e liberar buffers de DMA usando `bus_dma(9)`, incluindo o caso de bounce-buffer.
- Implementar `device_suspend` e `device_resume` com salvamento e restauração de registradores, quiesce de I/O e um autoteste pós-resume.

Se algum deles ainda exigir uma pesquisa, os laboratórios a revisitar são:

- Registradores e barreiras: Laboratório 1 (Observe a Dança dos Registradores) e Laboratório 8 (O Cenário do Watchdog com Registradores) no Capítulo 16.
- Hardware simulado sob carga: Laboratório 6 (Injete Stuck-Busy e Observe o Driver Aguardar) e Laboratório 10 (Injete um Ataque de Corrupção de Memória) no Capítulo 17.
- Attach e detach PCI: Laboratório 4 (Reivindique o BAR e Leia um Registrador) e Laboratório 5 (Exercite o cdev e Verifique a Limpeza no Detach) no Capítulo 18.
- Tratamento de interrupções: Laboratório 3 (Estágio 2, Filtro Real e Tarefa Diferida) no Capítulo 19.
- MSI e MSI-X: Laboratório 4 (Estágio 3, MSI-X com Vinculação de CPU) no Capítulo 20.
- DMA: Laboratório 4 (Estágio 3, Conclusão Orientada por Interrupção) e Laboratório 5 (Estágio 4, Refatoração e Regressão) no Capítulo 21.
- Gerenciamento de energia: Laboratório 2 (Estresse de Cem Ciclos) e Laboratório 3 (Transferência Através de um Ciclo) no Capítulo 22.

A Parte 5 esperará o seguinte como base:

- Um driver capaz de interagir com hardware com observabilidade já embutida: contadores, sysctls e chamadas `devctl_notify` nas transições importantes. A maquinaria de depuração do Capítulo 23 funciona melhor quando o driver já reporta sobre si mesmo.
- Um script de regressão que possa ciclar o driver de forma confiável, já que a Parte 5 transforma a reprodutibilidade em uma habilidade de primeira classe.
- Um kernel construído com `INVARIANTS` e `WITNESS`. A Parte 5 depende de ambos ainda mais do que a Parte 4, especialmente no Capítulo 23.
- A compreensão de que um bug no código do driver é um bug no código do kernel, o que significa que depuradores no espaço do usuário sozinhos não serão suficientes e a Parte 5 ensinará as ferramentas do espaço do kernel.

Se isso se aplica, a Parte 5 está pronta para você. Se algum ponto ainda parecer instável, uma passagem rápida pelo laboratório relevante compensará o tempo investido várias vezes.

## Ponte para o Capítulo 23

O Capítulo 23 tem o título *Depuração e Rastreamento*. Seu escopo é a prática profissional de encontrar bugs em drivers: ferramentas como `ktrace`, `ddb`, `kgdb`, `dtrace` e `procstat`; técnicas para analisar panics, deadlocks e corrupção de dados; estratégias para transformar relatos vagos de usuários em casos de teste reproduzíveis; e a mentalidade de um desenvolvedor de drivers que precisa depurar código em execução no espaço do kernel com visibilidade limitada.

O Capítulo 22 preparou o terreno de quatro maneiras específicas.

Primeiro, **você tem contadores de observabilidade em todo lugar**. O driver do Capítulo 22 expõe contadores de suspend, resume, shutdown e runtime-PM por meio de sysctls. As técnicas de depuração do Capítulo 23 dependem de observabilidade; um driver que já rastreia seu próprio estado é muito mais fácil de depurar do que um que não rastreia.

Segundo, **você tem um teste de regressão**. Os scripts de ciclo e estresse da Seção 6 são um primeiro gosto do que o Capítulo 23 expande: a capacidade de reproduzir um bug sob demanda. Um bug que você não consegue reproduzir é um bug que você não consegue corrigir; os scripts do Capítulo 22 são uma base para os testes mais pesados que o Capítulo 23 adiciona.

Terceiro, **você tem um kernel de debug com INVARIANTS / WITNESS funcionando**. O Capítulo 22 dependeu de ambos ao longo de todo o capítulo; o Capítulo 23 se baseia no mesmo kernel para sessões com `ddb`, análise post-mortem e reprodução de crashes do kernel.

Quarto, **você entende que bugs no código do driver são bugs no código do kernel**. O Capítulo 22 encontrou travamentos, dispositivos congelados, interrupções perdidas e reclamações do WITNESS. Cada um desses é um bug do kernel no sentido visível ao usuário; cada um requer uma abordagem de depuração no espaço do kernel. O Capítulo 23 ensina essa abordagem de forma sistemática.

Tópicos específicos que o Capítulo 23 cobrirá:

- Usar `ktrace` e `kdump` para observar o rastreamento de chamadas de sistema de um processo em tempo real.
- Usar `ddb` para entrar no depurador do kernel para análise post-mortem ou inspeção ao vivo.
- Usar `kgdb` com um core dump para recuperar o estado de um kernel que travou.
- Usar `dtrace` para rastreamento dentro do kernel sem modificar o código-fonte.
- Usar `procstat`, `top`, `pmcstat` e ferramentas relacionadas para observação de desempenho.
- Estratégias para minimizar um bug: reduzir um reprodutor, fazer bisect de uma regressão, formular hipóteses e testá-las.
- Padrões para instrumentar um driver em produção sem perturbar o comportamento.

Você não precisa ler com antecedência. O Capítulo 22 é preparação suficiente. Traga seu driver `myfirst` na versão `1.5-power`, seu `LOCKING.md`, seu `INTERRUPTS.md`, seu `MSIX.md`, seu `DMA.md`, seu `POWER.md`, seu kernel habilitado com `WITNESS` e seu script de regressão. O Capítulo 23 começa onde o Capítulo 22 terminou.

A Parte 4 está completa. O Capítulo 23 abre a Parte 5 adicionando a disciplina de observabilidade e depuração que separa um driver que você escreveu na semana passada de um driver que você pode manter por anos.

O vocabulário é seu; a estrutura é sua; a disciplina é sua. O Capítulo 23 adiciona a próxima peça que faltava: a capacidade de encontrar e corrigir bugs que só aparecem em produção.



## Referência: Cartão de Referência Rápida do Capítulo 22

Um resumo compacto do vocabulário, APIs, flags e procedimentos que o Capítulo 22 introduziu.

### Vocabulário

- **Suspend:** uma transição de D0 (operação plena) para um estado de menor consumo a partir do qual o dispositivo pode ser restaurado.
- **Resume:** a transição de volta do estado de menor consumo para D0.
- **Shutdown:** a transição para um estado final do qual o dispositivo não retornará.
- **Quiesce:** levar um dispositivo a um estado sem atividade e sem trabalho pendente.
- **Estado de sono do sistema (S0, S1, S3, S4, S5):** níveis de energia do sistema definidos pelo ACPI.
- **Estado de energia do dispositivo (D0, D1, D2, D3hot, D3cold):** níveis de energia do dispositivo definidos pelo PCI.
- **Estado do link (L0, L0s, L1, L1.1, L1.2, L2):** níveis de energia do link definidos pelo PCIe.
- **ASPM (Active-State Power Management):** transições automáticas entre L0 e L0s/L1.
- **PME# (Power Management Event):** um sinal que um dispositivo gera quando quer acordar o sistema.
- **Fonte de acordar (wake source):** um mecanismo pelo qual um dispositivo suspenso pode solicitar a retomada.
- **Runtime PM:** economia de energia no nível do dispositivo enquanto o sistema permanece em S0.

### Métodos Kobj Essenciais

- `DEVMETHOD(device_suspend, foo_suspend)`: chamado para fazer quiesce do dispositivo antes de uma transição de energia.
- `DEVMETHOD(device_resume, foo_resume)`: chamado para restaurar o dispositivo após a transição de energia.
- `DEVMETHOD(device_shutdown, foo_shutdown)`: chamado para deixar o dispositivo em um estado seguro para reinicialização.
- `DEVMETHOD(device_quiesce, foo_quiesce)`: chamado para parar a atividade sem desmontar os recursos.

### APIs PCI Essenciais

- `pci_has_pm(dev)`: verdadeiro se o dispositivo tiver capacidade de gerenciamento de energia.
- `pci_set_powerstate(dev, state)`: transição para `PCI_POWERSTATE_D0`, `D1`, `D2` ou `D3`.
- `pci_get_powerstate(dev)`: estado de energia atual.
- `pci_save_state(dev)`: armazena em cache o espaço de configuração.
- `pci_restore_state(dev)`: escreve de volta o espaço de configuração armazenado em cache.
- `pci_enable_pme(dev)`: habilita a geração de PME#.
- `pci_clear_pme(dev)`: limpa o status PME pendente.
- `pci_enable_busmaster(dev)`: reabilita o bus-master após um reset.

### Funções Auxiliares de Bus Essenciais

- `bus_generic_suspend(dev)`: suspende todos os filhos em ordem inversa.
- `bus_generic_resume(dev)`: retoma todos os filhos em ordem direta.
- `device_quiesce(dev)`: chama o `DEVICE_QUIESCE` do driver.

### Sysctls Essenciais

- `hw.acpi.supported_sleep_state`: lista dos S-states que a plataforma suporta.
- `hw.acpi.suspend_state`: S-state padrão para `zzz`.
- `hw.pci.do_power_suspend`: transição automática de D0 para D3 no suspend.
- `hw.pci.do_power_resume`: transição automática de D3 para D0 no resume.
- `dev.N.M.suspended`: flag de suspended do próprio driver.
- `dev.N.M.power_suspend_count`, `power_resume_count`, `power_shutdown_count`.
- `dev.N.M.runtime_state`, `runtime_suspend_count`, `runtime_resume_count`.

### Comandos Úteis

- `acpiconf -s 3`: entrar em S3.
- `zzz`: wrapper em torno de `acpiconf`.
- `devctl suspend <device>`: suspend por dispositivo.
- `devctl resume <device>`: resume por dispositivo.
- `devinfo -v`: árvore de dispositivos com estado.
- `pciconf -lvbc`: dispositivos PCI com estado de energia.
- `sysctl -a | grep acpi`: todas as variáveis relacionadas ao ACPI.

### Procedimentos Comuns

**Adição na tabela de métodos:**

```c
DEVMETHOD(device_suspend,  foo_suspend),
DEVMETHOD(device_resume,   foo_resume),
DEVMETHOD(device_shutdown, foo_shutdown),
```

**Esqueleto de suspend:**

```c
int foo_suspend(device_t dev) {
    struct foo_softc *sc = device_get_softc(dev);
    FOO_LOCK(sc);
    sc->suspended = true;
    FOO_UNLOCK(sc);
    foo_mask_interrupts(sc);
    foo_drain_dma(sc);
    foo_drain_workers(sc);
    return (0);
}
```

**Esqueleto de resume:**

```c
int foo_resume(device_t dev) {
    struct foo_softc *sc = device_get_softc(dev);
    pci_enable_busmaster(dev);
    foo_restore_registers(sc);
    FOO_LOCK(sc);
    sc->suspended = false;
    FOO_UNLOCK(sc);
    foo_unmask_interrupts(sc);
    return (0);
}
```

**Helper de Runtime-PM:**

```c
int foo_runtime_suspend(struct foo_softc *sc) {
    foo_quiesce(sc);
    pci_save_state(sc->dev);
    return (pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3));
}

int foo_runtime_resume(struct foo_softc *sc) {
    pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
    pci_restore_state(sc->dev);
    return (foo_restore(sc));
}
```

### Arquivos para Manter nos Favoritos

- `/usr/src/sys/kern/device_if.m`: as definições de métodos kobj.
- `/usr/src/sys/kern/subr_bus.c`: `bus_generic_suspend`, `bus_generic_resume`, `device_quiesce`.
- `/usr/src/sys/dev/pci/pci.c`: `pci_suspend_child`, `pci_resume_child`, `pci_save_state`, `pci_restore_state`.
- `/usr/src/sys/dev/pci/pcivar.h`: constantes `PCI_POWERSTATE_*` e API inline.
- `/usr/src/sys/dev/re/if_re.c`: referência de produção para suspend/resume com WoL.
- `/usr/src/sys/dev/xl/if_xl.c`: padrão mínimo de suspend/resume.
- `/usr/src/sys/dev/virtio/block/virtio_blk.c`: quiesce no estilo virtio.



## Referência: Glossário dos Termos do Capítulo 22

Um breve glossário dos novos termos do capítulo.

- **ACPI (Advanced Configuration and Power Interface):** a interface padrão da indústria entre o sistema operacional e o firmware da plataforma para gerenciamento de energia.
- **ASPM (Active-State Power Management):** transições automáticas de estado de link PCIe.
- **D-state:** um estado de energia do dispositivo (D0 a D3cold).
- **DEVICE_QUIESCE:** o método kobj que interrompe a atividade sem liberar os recursos.
- **DEVICE_RESUME:** o método kobj chamado para restaurar um dispositivo à operação.
- **DEVICE_SHUTDOWN:** o método kobj chamado no desligamento do sistema.
- **DEVICE_SUSPEND:** o método kobj chamado para realizar o quiesce de um dispositivo antes de uma transição de energia.
- **GPE (General-Purpose Event):** uma fonte de eventos de wake do ACPI.
- **L-state:** um estado de energia de link PCIe.
- **Link state machine:** as transições automáticas entre L0 e L0s/L1.
- **PME# (Power Management Event):** o sinal PCI que um dispositivo emite para solicitar wake.
- **Power management capability:** a estrutura de capability PCI que contém os registradores de PM.
- **Quiesce:** levar um dispositivo a um estado sem atividade e sem trabalho pendente.
- **Runtime PM:** economia de energia no nível do dispositivo enquanto o sistema permanece em S0.
- **S-state:** um estado de sleep do sistema ACPI (S0 a S5).
- **Shutdown:** desligamento final, normalmente levando a reboot ou desligamento completo.
- **Sleep state:** veja S-state.
- **Suspend:** desligamento temporário do qual o sistema ou dispositivo pode retornar.
- **Suspended flag:** um flag local do driver indicando que o dispositivo está em estado suspenso.
- **Wake source:** um mecanismo pelo qual um sistema ou dispositivo suspenso pode ser acordado.
- **WoL (Wake on LAN):** uma fonte de wake acionada por um pacote de rede.



## Referência: Uma Nota Final sobre a Filosofia de Gerenciamento de Energia

Um parágrafo para encerrar o capítulo.

O gerenciamento de energia é a disciplina que separa um protótipo de driver de um driver de produção. Antes do gerenciamento de energia, um driver assume que seu dispositivo está sempre ligado e sempre disponível. Após o gerenciamento de energia, o driver sabe que o dispositivo pode entrar em sleep e sabe como fazê-lo corretamente, e o driver pode ser confiado nos tipos de ambientes que usuários reais utilizam: laptops que fecham e abrem dezenas de vezes ao dia, servidores que suspendem dispositivos ociosos para economizar energia, VMs que migram entre hosts, sistemas embarcados que desligam domínios de energia inteiros para estender a vida útil da bateria.

A lição do Capítulo 22 é que o gerenciamento de energia é disciplinado, não mágico. O kernel do FreeBSD oferece ao driver um contrato específico (os quatro métodos kobj, a ordem de invocação, a interação com a camada PCI), e seguir o contrato é a maior parte do trabalho. O restante é específico do hardware: entender quais registradores o dispositivo perde em uma transição de D-state, quais fontes de wake o hardware suporta, qual política o driver deve aplicar para runtime PM. O padrão é o mesmo em todos os drivers com suporte a energia no FreeBSD; internalizá-lo uma vez rende dividendos em dezenas de capítulos posteriores e em milhares de linhas de código de driver real.

Para você e para os futuros leitores deste livro, o padrão de gerenciamento de energia do Capítulo 22 é uma parte permanente da arquitetura do driver `myfirst` e uma ferramenta permanente no arsenal do leitor. O Capítulo 23 pressupõe isso: depurar um driver pressupõe que o driver tenha os contadores de observabilidade e o ciclo de vida estruturado que o Capítulo 22 introduziu. Os capítulos de especialização da Parte 6 pressupõem isso: todo driver de estilo produção tem um caminho de energia. Os capítulos de desempenho da Parte 7 (Capítulo 33) pressupõem isso: toda medição de ajuste precisa levar em conta as transições de estado de energia. O vocabulário é o vocabulário que todo driver de produção FreeBSD compartilha; os padrões são os padrões pelos quais os drivers de produção se guiam; a disciplina é a disciplina que mantém as plataformas com suporte a energia corretas.

A habilidade que o Capítulo 22 ensina não é "como adicionar métodos de suspend e resume a um único driver PCI". É "como pensar sobre o ciclo de vida de um driver como attach, run, quiesce, sleep, wake, run e, eventualmente, detach, em vez de apenas attach, run, detach". Essa habilidade se aplica a todos os drivers com que o leitor venha a trabalhar.

A Parte 4 está completa. O driver `myfirst` está na versão `1.5-power`, estruturalmente paralelo a um driver FreeBSD de produção, e pronto para os capítulos de depuração, ferramentas e especialização que se seguem nas Partes 5 e 6. O Capítulo 23 começa aí.
