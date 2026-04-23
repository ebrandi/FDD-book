---
title: "Configurando Seu Laboratório"
description: "Este capítulo guia você na configuração de um laboratório FreeBSD seguro e pronto para o desenvolvimento de drivers."
partNumber: 1
partName: "Foundations: FreeBSD, C, and the Kernel"
chapter: 2
lastUpdated: "2025-08-24"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "Tradução para Português do Brasil assistida por IA usando o modelo qwen3.6:35b-a3b-bf16"
estimatedReadTime: 60
language: "pt-BR"
---
# Configurando Seu Laboratório

Antes de começarmos a escrever código ou explorar os internos do FreeBSD, precisamos de um lugar onde seja seguro experimentar, cometer erros e aprender. Esse lugar é o seu **ambiente de laboratório**. Neste capítulo, vamos criar a base que você usará ao longo de todo o restante do livro: um sistema FreeBSD configurado para o desenvolvimento de drivers.

Pense neste capítulo como a preparação da sua **oficina**. Assim como um carpinteiro precisa da bancada certa, das ferramentas adequadas e dos equipamentos de segurança antes de construir móveis, você precisa de uma instalação confiável do FreeBSD, dos utilitários de desenvolvimento necessários e de uma forma de se recuperar rapidamente quando algo der errado. A programação do kernel é implacável; um pequeno erro no seu driver pode travar o sistema inteiro. Ter um laboratório dedicado significa que essas travadas se tornam parte do processo de aprendizado, não catástrofes.

Ao terminar este capítulo, você será capaz de:

- Entender a importância de isolar seus experimentos do seu computador principal.
- Escolher entre usar uma máquina virtual ou uma instalação em hardware físico.
- Instalar o FreeBSD 14.3 passo a passo.
- Configurar o sistema com as ferramentas e o código-fonte necessários para o desenvolvimento de drivers.
- Aprender a tirar snapshots, gerenciar backups e usar controle de versão para que seu progresso nunca seja perdido.

Ao longo do caminho, vamos pausar para **laboratórios práticos** para que você não apenas leia sobre como configurar as coisas, mas realmente as faça. Quando terminar, você terá um laboratório FreeBSD que é seguro, reproduzível e pronto para tudo que construiremos juntos nos capítulos seguintes.

### Orientações ao Leitor: Como Usar Este Capítulo

Este capítulo é mais prático do que teórico. Pense nele como um manual passo a passo para configurar seu laboratório FreeBSD antes de começar os experimentos de verdade. Você será solicitado a fazer escolhas (máquina virtual ou hardware físico), seguir etapas de instalação e configurar seu sistema FreeBSD.

A melhor forma de usar este capítulo é **executar os passos conforme os lê**. Não apenas folheie as páginas, instale o FreeBSD de verdade, tire os snapshots, anote suas escolhas no seu diário de laboratório e experimente os exercícios. Cada seção se baseia na anterior, de modo que, ao final, você terá um ambiente completo que corresponde aos exemplos do restante deste livro.

Se você já sabe como instalar e configurar o FreeBSD, pode percorrer ou pular partes deste capítulo, mas não pule os laboratórios; eles garantem que sua configuração corresponda ao que usaremos ao longo do livro.

Acima de tudo, lembre-se: erros aqui não são fracassos, são parte do processo. Este é o seu lugar seguro para experimentar e aprender.

**Tempo estimado para concluir este capítulo:** 1 a 2 horas, dependendo da sua escolha entre máquina virtual ou instalação em hardware físico, e se você já tem experiência instalando sistemas operacionais.

## Por Que um Ambiente de Laboratório É Importante

Antes de começarmos a digitar comandos e escrever nossos primeiros trechos de código, precisamos pausar por um momento e pensar em *onde* vamos realizar todo esse trabalho. A programação do kernel e o desenvolvimento de drivers de dispositivo não são como escrever um script simples ou uma página web. Quando você experimenta com o kernel, está experimentando com o **coração do sistema operacional**. Um pequeno erro no seu código pode fazer sua máquina travar, reiniciar inesperadamente ou até corromper dados se você não tiver cuidado.

Isso não significa que o desenvolvimento de drivers seja perigoso; significa que precisamos tomar precauções e configurar um **ambiente seguro** onde erros são esperados, recuperáveis e até incentivados como parte do processo de aprendizado. Esse ambiente é o que chamaremos de seu **laboratório**.

Seu laboratório deve ser um **ambiente dedicado e isolado**. Assim como um químico não conduziria um experimento na mesa de jantar da família sem equipamentos de proteção, você não deve executar código do kernel inacabado no mesmo computador onde guarda suas fotos pessoais, documentos de trabalho ou projetos importantes da faculdade. Você precisa de um espaço projetado para a exploração e para o fracasso, porque é pelo fracasso que você vai aprender.

### Por Que Não Usar Seu Computador Principal?

É tentador pensar: *"Já tenho um computador rodando FreeBSD (ou Linux, ou Windows), por que não usar esse mesmo?"* A resposta curta: porque seu computador principal é para produtividade, não para experimentos. Se você acidentalmente causar um kernel panic ao testar seu driver, não vai querer perder trabalho não salvo, derrubar sua conexão de rede durante uma reunião online ou até danificar um sistema de arquivos com dados corrompidos.

Seu laboratório lhe dá liberdade: você pode quebrar coisas, reiniciar e se recuperar em minutos sem estresse. Essa liberdade é essencial para o aprendizado.

### Máquinas Virtuais: O Melhor Amigo do Iniciante

A maioria dos iniciantes (e até muitos desenvolvedores experientes) começa com **máquinas virtuais (VMs)**. Uma VM é como um computador em sandbox rodando dentro do seu computador real. Ela funciona exatamente como uma máquina física, mas se algo der errado, você pode redefini-la, tirar um snapshot ou reinstalar o FreeBSD em minutos. Você não precisa de um laptop ou servidor sobressalente para começar a desenvolver drivers; seu computador atual pode hospedar seu laboratório.

Vamos abordar a virtualização com mais detalhes na próxima seção, mas aqui estão os pontos principais:

- **Experimentos seguros**: Se o seu driver travar o kernel, apenas a VM cai, não o seu computador host.
- **Recuperação fácil**: Snapshots permitem salvar o estado da VM e reverter instantaneamente se você quebrar algo.
- **Custo baixo**: Sem necessidade de hardware dedicado.
- **Portabilidade**: Você pode mover sua VM entre computadores.

### Hardware Físico: Quando Você Precisa do Real

Há momentos em que apenas o hardware real vai servir, por exemplo, se você quiser desenvolver um driver para uma placa PCIe ou um dispositivo USB que exige acesso direto ao barramento da máquina. Nesses casos, testar em uma VM pode não ser suficiente porque nem todas as soluções de virtualização conseguem fazer o passthrough de hardware de forma confiável.

Se você tiver um PC antigo disponível, instalar o FreeBSD nele pode lhe dar o ambiente mais próximo da realidade para os testes. Mas lembre-se: configurações em hardware físico não têm a mesma rede de segurança que as VMs. Se você travar o kernel, sua máquina vai reiniciar e você precisará se recuperar manualmente. Por isso recomendo começar em uma VM, mesmo que você eventualmente migre para hardware físico em projetos específicos de hardware.

### Exemplo do Mundo Real

Para tornar isso mais concreto: imagine que você está escrevendo um driver simples que, por engano, desreferencia um ponteiro NULL (não se preocupe se isso soar técnico agora, você aprenderá tudo sobre isso mais tarde). Em uma VM, seu sistema pode travar, mas com uma reinicialização e uma reversão de snapshot, você está de volta em ação em minutos. No hardware físico, o mesmo erro poderia causar corrupção no sistema de arquivos, exigindo um processo de recuperação demorado. É por isso que um ambiente de laboratório seguro é tão valioso.

### Laboratório Prático: Preparando a Mentalidade para o Laboratório

Antes mesmo de instalar o FreeBSD, vamos fazer um exercício simples para entrar na mentalidade certa.

1. Pegue um caderno (físico ou digital). Este será o seu **diário de laboratório**.
2. Anote:
   - A data de hoje
   - A máquina que você vai usar para o seu laboratório FreeBSD (VM ou física)
   - Por que você escolheu essa opção (segurança, conveniência, acesso a hardware real, etc.)
3. Faça uma primeira entrada: *"Configuração do laboratório iniciada. Objetivo: construir um ambiente seguro para experimentos com drivers FreeBSD."*

Isso pode parecer desnecessário, mas manter um **diário de laboratório** vai ajudá-lo a acompanhar seu progresso, repetir configurações bem-sucedidas no futuro e depurar quando algo der errado. No desenvolvimento profissional de drivers, os engenheiros mantêm anotações muito detalhadas; começar esse hábito agora vai fazer você pensar e trabalhar como um desenvolvedor de sistemas de verdade.

### Encerrando

Apresentamos a ideia do seu **ambiente de laboratório** e por que ele é tão importante para um desenvolvimento seguro de drivers. Seja qual for sua escolha, uma máquina virtual ou um computador físico sobressalente, o ponto central é ter um lugar dedicado onde seja aceitável cometer erros.

Na próxima seção, vamos analisar com mais detalhes os **prós e contras das máquinas virtuais versus o hardware físico**. Ao final dessa seção, você saberá exatamente qual configuração faz mais sentido para começar seu trabalho com FreeBSD.

## Escolhendo Sua Configuração: Máquina Virtual ou Hardware Físico

Agora que você entende por que um ambiente de laboratório dedicado é importante, a próxima pergunta é: **onde você deve construí-lo?** O FreeBSD pode ser executado de duas maneiras principais para seus experimentos:

1. **Dentro de uma máquina virtual (VM)**, rodando sobre seu sistema operacional existente.
2. **Diretamente no hardware físico** (frequentemente chamado de *bare metal*).

Ambas as opções funcionam, e ambas são amplamente usadas no desenvolvimento real com FreeBSD. A escolha certa depende dos seus objetivos, do seu hardware e do seu nível de familiaridade. Vamos compará-las lado a lado.

### Máquinas Virtuais: Sua Sandbox em Uma Caixa

Uma **máquina virtual** é um software que permite executar o FreeBSD como se fosse um computador separado, mas dentro do seu computador existente. As soluções de VM mais populares incluem:

- **VirtualBox** (gratuito e multiplataforma, ótimo para iniciantes).
- **VMware Workstation / Fusion** (comercial, polido, amplamente usado).
- **bhyve** (o hypervisor nativo do FreeBSD, ideal se você quiser rodar FreeBSD *em cima de* FreeBSD).

Por que os desenvolvedores adoram VMs para trabalho com o kernel:

- **Snapshots salvam o dia**: Antes de testar código arriscado, você tira um snapshot. Se o sistema entrar em panic ou quebrar, você restaura em segundos.
- **Múltiplos laboratórios em uma máquina**: Você pode criar várias instâncias do FreeBSD, cada uma para um projeto diferente.
- **Fácil de compartilhar**: Você pode exportar uma imagem de VM e compartilhá-la com um colega.

**Quando preferir VMs:**

- Você está começando agora e quer o ambiente mais seguro possível.
- Você não tem hardware sobressalente para dedicar.
- Você espera travar o kernel com frequência enquanto aprende (e vai travar).

### Hardware Físico: O Negócio de Verdade

Rodar o FreeBSD **diretamente no hardware** é o mais próximo que você pode chegar do "mundo real". Isso significa que o FreeBSD inicializa como o único sistema operacional na máquina, conversando diretamente com a CPU, a memória, o armazenamento e os periféricos.

Vantagens:

- **Testes com hardware real**: Essencial ao desenvolver drivers para PCIe, USB ou outros dispositivos físicos.
- **Desempenho**: Sem a sobrecarga da VM. Você tem acesso total aos recursos do sistema.
- **Precisão**: Alguns bugs só aparecem no hardware físico, especialmente problemas relacionados a timing.

Desvantagens:

- **Sem rede de segurança**: Se o kernel travar, toda a máquina cai.
- **A recuperação leva tempo**: Se você corromper o sistema operacional, pode precisar reinstalar o FreeBSD.
- **Hardware dedicado necessário**: Você vai precisar de um PC ou laptop sobressalente que possa dedicar inteiramente aos experimentos.

**Quando preferir hardware físico:**

- Você planeja desenvolver um driver para hardware que não funciona bem em VMs.
- Você já tem uma máquina sobressalente que pode dedicar inteiramente ao FreeBSD.
- Você quer o máximo de realismo, mesmo que isso signifique mais risco.

### Estratégia Híbrida

Muitos desenvolvedores profissionais usam **ambas as abordagens**. Eles fazem a maior parte de sua experimentação e prototipagem em uma VM, onde é seguro e rápido, e só migram para hardware físico quando o driver está estável o suficiente para ser testado com hardware real. Você não precisa se comprometer com uma opção para sempre; pode começar com uma VM hoje e adicionar uma máquina física mais tarde, se precisar.

### Tabela de Comparação Rápida

| Recurso                  | Máquina Virtual                         | Hardware Físico                      |
| ------------------------ | --------------------------------------- | ------------------------------------ |
| **Segurança**            | Muito alta (snapshots, reversão)        | Baixa (recuperação manual necessária)|
| **Desempenho**           | Ligeiramente menor (sobrecarga)         | Desempenho total do sistema          |
| **Acesso ao hardware**   | Limitado / dispositivos emulados        | Hardware real completo               |
| **Dificuldade de config.**| Fácil e rápida                         | Moderada (instalação completa)       |
| **Custo**                | Nenhum (roda no seu PC)                 | Requer máquina dedicada              |
| **Melhor para**          | Iniciantes, aprendizado seguro          | Testes avançados de hardware         |

### Laboratório Prático: Decidindo Seu Caminho

1. Observe seus recursos atuais. Você tem um notebook ou desktop sobrando que possa dedicar a experimentos com FreeBSD?
   - Se sim -> Bare metal é uma opção para você.
   - Se não -> Uma VM é o ponto de partida ideal.
2. No seu **caderno de laboratório**, anote:
   - Qual opção você usará (VM ou bare metal).
   - Por que você a escolheu.
   - Quaisquer limitações que você já antecipa (por exemplo, "Usando uma VM, pode não ser possível testar USB passthrough ainda").
3. Se você escolher VM, anote qual hypervisor usará (VirtualBox, VMware, bhyve, etc.).

Essa decisão não é definitiva. Você sempre pode acrescentar um segundo ambiente mais adiante. O objetivo agora é começar com uma configuração segura e confiável.

### Encerrando

Comparamos as máquinas virtuais e as configurações de bare metal e vimos os pontos fortes e as trocas de cada abordagem. Para a maioria dos iniciantes, começar com uma VM é o melhor equilíbrio entre segurança, praticidade e flexibilidade. Se mais tarde você precisar interagir com hardware real, pode adicionar um sistema bare metal ao seu conjunto de ferramentas.

Na próxima seção, vamos arregaçar as mangas e realizar a **instalação do FreeBSD 14.3**, primeiro em uma VM, e depois abordaremos os pontos principais para instalações em bare metal. É aqui que seu laboratório começa a tomar forma de verdade.

## Instalando o FreeBSD (VM e Bare Metal)

Neste ponto, você já escolheu se vai instalar o FreeBSD em uma **máquina virtual** ou em **bare metal**. Agora é hora de instalar de fato o sistema operacional que servirá de base para todos os nossos experimentos. Vamos focar no **FreeBSD 14.3**, a versão estável mais recente no momento da escrita, para que tudo o que você fizer corresponda aos exemplos deste livro.

O instalador do FreeBSD é baseado em texto, mas não deixe isso intimidar você. É direto ao ponto, e em menos de 20 minutos você terá um sistema funcionando e pronto para o desenvolvimento.

### Baixando a ISO do FreeBSD

1. Acesse a página oficial de downloads do FreeBSD:
    https://www.freebsd.org/where
2. Escolha a imagem **14.3-RELEASE**.
   - Se você está instalando em uma VM, baixe a **ISO amd64 Disk1** (`FreeBSD-14.3-RELEASE-amd64-disc1.iso`).
   - Se você está instalando em hardware real, a mesma ISO funciona, embora você também possa considerar a **imagem memstick** se quiser gravá-la em um pendrive USB.

### Instalando o FreeBSD no VirtualBox (Passo a Passo)

Se você ainda não tem o **VirtualBox** instalado, precisará configurá-lo antes de criar sua VM FreeBSD. O VirtualBox está disponível para hosts Windows, macOS, Linux e até Solaris. Baixe a versão mais recente no site oficial:

https://www.virtualbox.org/wiki/Downloads

Escolha o pacote que corresponde ao seu sistema operacional host (por exemplo, hosts Windows ou macOS), baixe-o e siga o instalador. A instalação é simples e leva apenas alguns minutos. Assim que concluída, abra o VirtualBox e você estará pronto para criar sua primeira máquina virtual FreeBSD.

Agora que você está pronto, vamos percorrer o processo no VirtualBox, já que esse é o ponto de entrada mais fácil para a maioria dos leitores. Os passos são similares no VMware ou no bhyve.

Para começar, execute o aplicativo VirtualBox no seu computador. Na tela principal, selecione **Home** na coluna da esquerda e clique em **New**. Em seguida, siga os passos abaixo:

1. **Crie uma nova VM** no VirtualBox:

   - Nome da VM: `FreeBSD Lab`
   
   - Pasta da VM: Escolha um diretório para armazenar sua VM FreeBSD
   
   - Imagem ISO: Escolha o arquivo ISO do FreeBSD que você baixou acima
   
   - Edição do SO: Deixe em branco
   
   - SO: Escolha `BSD`
   
   - Distribuição do SO: Escolha `FreeBSD`
   
   - Versão do SO: Escolha `FreeBSD (64-bit)`
   
     Clique em **Next** para continuar

![image-20250823183742036](https://freebsd.edsonbrandi.com/images/image-20250823183742036.png)

2. **Aloque recursos**:

   - Memória Base: pelo menos 2 GB (4 GB recomendado).

   - Número de CPUs: 2 ou mais, se disponível.

   - Tamanho do Disco: 30 GB ou mais.
   
     Clique em **Next** para continuar

![image-20250823183937505](https://freebsd.edsonbrandi.com/images/image-20250823183937505.png)

3. **Revise suas opções**: Se estiver satisfeito com o resumo, clique em **Finish** para criar a VM.

![image-20250823184925505](https://freebsd.edsonbrandi.com/images/image-20250823184925505.png)

4. **Inicie a máquina virtual**: Clique no botão verde **Start**. Ao iniciar a VM, ela fará o boot usando o disco de instalação do FreeBSD que você especificou ao criá-la.

![image-20250823185259010](https://freebsd.edsonbrandi.com/images/image-20250823185259010.png)

5. **Faça o boot da VM**: A VM mostrará o boot loader do FreeBSD. Pressione **1** para continuar o boot no FreeBSD.

![image-20250823185756980](https://freebsd.edsonbrandi.com/images/image-20250823185756980.png)

6. **Execute o instalador**: Durante o processo de boot, o instalador será iniciado automaticamente. Escolha **[ Install ]** para continuar.

![image-20250823190016799](https://freebsd.edsonbrandi.com/images/image-20250823190016799.png)

7. **Layout do teclado**: Escolha o idioma e o layout de teclado de sua preferência. O padrão é o layout US. Pressione **Enter** para continuar.

![image-20250823190046619](https://freebsd.edsonbrandi.com/images/image-20250823190046619.png)

8. **Hostname**: Digite o hostname para o seu laboratório. No exemplo, escolhi `fbsd-lab`. Pressione **Enter** para continuar.

![image-20250823190129010](https://freebsd.edsonbrandi.com/images/image-20250823190129010.png)

9. **Seleção de Distribuição**: Mantenha os padrões (sistema base, kernel). Pressione **Enter** para continuar.

![image-20250823190234155](https://freebsd.edsonbrandi.com/images/image-20250823190234155.png)

10. **Particionamento**: Escolha *Auto (UFS)*, a menos que queira aprender ZFS mais tarde. Pressione **Enter** para continuar.

![image-20250823190350815](https://freebsd.edsonbrandi.com/images/image-20250823190350815.png)

11. **Partição**: Escolha **[ Entire Disk ]**. Pressione **Enter** para continuar.

![image-20250823190450571](https://freebsd.edsonbrandi.com/images/image-20250823190450571.png)

12. **Esquema de Partição**: Escolha **GPT GUID Partition Table**. Pressione **Enter** para continuar.

![image-20250823190622981](https://freebsd.edsonbrandi.com/images/image-20250823190622981.png)

13. **Editor de Partições**: Aceite o padrão e escolha **[Finish]**. Pressione **Enter** para continuar.

![image-20250823190742861](https://freebsd.edsonbrandi.com/images/image-20250823190742861.png)

14. **Confirmação**: Nesta tela você confirmará que deseja prosseguir com a instalação do FreeBSD. Após essa confirmação, o instalador começará a gravar dados no seu disco rígido. Para prosseguir com a instalação, escolha **[Commit]** e pressione **Enter** para continuar.

![image-20250823190903913](https://freebsd.edsonbrandi.com/images/image-20250823190903913.png)

15. **Verificação de Checksum**: No início do processo, o instalador do FreeBSD verificará a integridade dos arquivos de instalação.

![image-20250823191020839](https://freebsd.edsonbrandi.com/images/image-20250823191020839.png)

16. **Extração dos Arquivos**: Após a validação dos arquivos, o instalador os extrairá para o seu disco rígido.

![image-20250823191053163](https://freebsd.edsonbrandi.com/images/image-20250823191053163.png)

17. **Senha do Root**: Quando o instalador terminar de extrair os arquivos, você precisará escolher uma senha para o acesso root. Escolha uma que você se lembrará. Pressione **Enter** para continuar.

![image-20250823191405000](https://freebsd.edsonbrandi.com/images/image-20250823191405000.png)

18. **Configuração de Rede**: Escolha a interface de rede (**em0**) que deseja usar e pressione **Enter** para continuar.

![image-20250823191520068](https://freebsd.edsonbrandi.com/images/image-20250823191520068.png)

19. **Configuração de Rede**: Escolha **[ Yes ]** para habilitar **IPv4** na sua interface de rede e pressione **Enter** para continuar.

![image-20250823191559429](https://freebsd.edsonbrandi.com/images/image-20250823191559429.png)

20. **Configuração de Rede**: Escolha **[ Yes ]** para habilitar **DHCP** na sua interface de rede. Se preferir usar um endereço IP estático, escolha **[ No ]**. Pressione **Enter** para continuar.

![image-20250823191626027](https://freebsd.edsonbrandi.com/images/image-20250823191626027.png)

21. **Configuração de Rede**: Escolha **[ No ]** para desativar **IPv6** na sua interface de rede e pressione **Enter** para continuar.

![image-20250823191705347](https://freebsd.edsonbrandi.com/images/image-20250823191705347.png)

22. **Configuração de Rede**: Digite o endereço IP dos seus servidores DNS preferidos. No exemplo, estou usando o DNS do Google. Pressione **Enter** para continuar.

![image-20250823191748088](https://freebsd.edsonbrandi.com/images/image-20250823191748088.png)

23. **Seletor de Fuso Horário**: Escolha o fuso horário desejado para o seu sistema FreeBSD. Para este exemplo, estou usando **UTC**. Pressione **Enter** para continuar.

![image-20250823191820859](https://freebsd.edsonbrandi.com/images/image-20250823191820859.png)

24. **Confirmar Fuso Horário**: Confirme o fuso horário que deseja usar. Escolha **[ YES ]** e pressione **Enter** para continuar.

![image-20250823191849469](https://freebsd.edsonbrandi.com/images/image-20250823191849469.png)

25. **Data e Hora**: O instalador dará a você a opção de ajustar manualmente a data e a hora. Normalmente é seguro escolher **[ Skip ]**. Pressione **Enter** para continuar.

![image-20250823191926758](https://freebsd.edsonbrandi.com/images/image-20250823191926758.png)

![image-20250823191957558](https://freebsd.edsonbrandi.com/images/image-20250823191957558.png)

26. **Configuração do Sistema**: O instalador dará a você a opção de escolher alguns serviços para iniciar no boot. Selecione **ntpd** e pressione **Enter** para continuar.

![image-20250823192055299](https://freebsd.edsonbrandi.com/images/image-20250823192055299.png)

27. **Hardening do sistema**: O instalador dará a você a opção de habilitar algumas medidas de segurança aplicadas no boot. Por enquanto, aceite o padrão e pressione **Enter** para continuar.

![image-20250823192128039](https://freebsd.edsonbrandi.com/images/image-20250823192128039.png)

28. **Verificação de Firmware**: O instalador verificará se algum componente de hardware precisa de um firmware específico para funcionar corretamente e o instalará se necessário. Pressione **Enter** para continuar.

![image-20250823192211024](https://freebsd.edsonbrandi.com/images/image-20250823192211024.png)

29. **Adicionar Contas de Usuário**: O instalador dará a você a oportunidade de adicionar um usuário normal ao seu sistema. Escolha **[ Yes ]** e pressione **Enter** para continuar.

![image-20250823192233281](https://freebsd.edsonbrandi.com/images/image-20250823192233281.png)

30. **Criar um Usuário**: O instalador pedirá que você informe os dados do usuário e responda algumas perguntas básicas. Você deve escolher o **nome de usuário** e a **senha** desejados. Você pode **aceitar as respostas padrão** para todas as perguntas, exceto para a pergunta ***"Invite USER into other groups?"***. Para essa pergunta, você precisa responder "**wheel**". Esse é o grupo no FreeBSD que permitirá que você use o comando `su` para se tornar root durante uma sessão normal.

![image-20250823192452683](https://freebsd.edsonbrandi.com/images/image-20250823192452683.png)

31. **Criar um Usuário**: Depois de responder todas as perguntas e criar seu usuário, o instalador do FreeBSD perguntará se você quer adicionar outro usuário. Apenas pressione **Enter** para aceitar a resposta padrão (não) e ir para o menu de Configuração Final.

![image-20250823192600794](https://freebsd.edsonbrandi.com/images/image-20250823192600794.png)

32. **Configuração Final**: Neste ponto, você já concluiu a instalação do FreeBSD. Esse menu final permite revisar e alterar as opções feitas nas etapas anteriores. Selecione **Exit** para sair do instalador e pressione **Enter**.

![image-20250823192642433](https://freebsd.edsonbrandi.com/images/image-20250823192642433.png)

33. **Configuração Manual**: O instalador perguntará se você deseja abrir um shell para fazer configurações manuais no sistema recém-instalado. Escolha **[ No ]** e pressione **Enter**.

![image-20250823192704460](https://freebsd.edsonbrandi.com/images/image-20250823192704460.png)

34. **Ejetar o Disco de Instalação**: Antes de reiniciar a VM, precisamos ejetar o disco virtual que usamos para a instalação. Para isso, clique com o botão esquerdo do mouse no ícone de CD/DVD na barra de status inferior da janela da VM no VirtualBox e, em seguida, clique com o botão direito no menu "Remove Disk From Virtual Drive".

![image-20250823193213602](https://freebsd.edsonbrandi.com/images/image-20250823193213602.png)

Se por algum motivo você receber uma mensagem informando que o disco óptico virtual está em uso e não pode ser ejetado, clique no botão "Force Unmount". Depois disso, você pode prosseguir com a reinicialização.

![image-20250823193252804](https://freebsd.edsonbrandi.com/images/image-20250823193252804.png)

35. **Reinicie sua VM**: Pressione **Enter** neste menu para reiniciar sua VM FreeBSD.

![image-20250823192732830](https://freebsd.edsonbrandi.com/images/image-20250823192732830.png)

### Instalando o FreeBSD em Bare Metal

Se você estiver usando um PC ou laptop reservado, precisará instalar o FreeBSD diretamente a partir de um pen drive inicializável. Veja como:

#### Passo 1: Prepare um Pen Drive

- Você precisará de um pen drive com pelo menos **2 GB de capacidade**.
- Certifique-se de fazer backup de todos os dados nele; o processo apagará tudo.

#### Passo 2: Faça o Download da Imagem Correta

- Para instalações via USB, faça o download da **imagem memstick** (`FreeBSD-14.3-RELEASE-amd64-memstick.img`).

#### Passo 3: Crie o Pen Drive Inicializável (Instruções para Windows)

No Windows, a ferramenta mais simples é o **Rufus**:

1. Faça o download do Rufus em https://rufus.ie.
2. Insira seu pen drive.
3. Abra o Rufus e selecione:
   - **Device**: seu pen drive.
   - **Boot selection**: o arquivo `.img` do memstick do FreeBSD que você baixou.
   - **Partition scheme**: MBR
   - **Target System**: BIOS (ou UEFI-CSM)
   - **File system**: deixe o padrão.
4. Clique em *Start*. O Rufus avisará que todos os dados serão destruídos; aceite.
5. Aguarde até o processo terminar. Seu pen drive agora está inicializável.

![image-20250823210622431](https://freebsd.edsonbrandi.com/images/image-20250823210622431.png)

Se você já tiver um sistema semelhante ao UNIX, pode criar o pen drive a partir do terminal usando o comando `dd`:

```console
% sudo dd if=FreeBSD-14.3-RELEASE-amd64-memstick.img of=/dev/da0 bs=1M
```

Substitua `/dev/da0` pelo caminho do seu dispositivo USB.

#### Passo 4: Inicialize pelo USB

1. Insira o pen drive na máquina de destino.
2. Entre no menu de boot do BIOS/UEFI (normalmente pressionando F12, Esc ou Del durante a inicialização).
3. Selecione o pen drive como dispositivo de boot.

#### Passo 5: Execute o Instalador

Assim que o FreeBSD inicializar, siga os mesmos passos do instalador descritos anteriormente, onde escolhemos o layout do teclado, o Hostname, a Distribuição etc.

Após a conclusão da instalação, remova o pen drive e reinicie. O FreeBSD agora inicializará pelo disco rígido.

### Primeira Inicialização

Após a instalação, você verá o menu de boot do FreeBSD:

![image-20250823213050882](https://freebsd.edsonbrandi.com/images/image-20250823213050882.png)

Seguido pelo prompt de login:

![image-20250823212856938](https://freebsd.edsonbrandi.com/images/image-20250823212856938.png)

Parabéns! Sua máquina de laboratório FreeBSD já está funcionando e pronta para configuração.

### Encerrando

Você acaba de concluir um dos marcos mais importantes: instalar o FreeBSD 14.3 no seu ambiente de laboratório dedicado. Seja em uma VM ou em bare metal, você agora tem um sistema limpo que pode quebrar, corrigir e reconstruir com segurança enquanto aprende.

Na próxima seção, percorreremos a **configuração inicial** que você deve realizar logo após a instalação: configurar a rede, habilitar os serviços essenciais e preparar o sistema para o trabalho de desenvolvimento.

## Primeira Inicialização e Configuração Inicial

Quando o seu sistema FreeBSD termina seu primeiro boot após a instalação, você se depara com algo muito diferente do Windows ou do macOS. Não há uma área de trabalho elaborada, nem ícones, nem um assistente de "primeiros passos". Em vez disso, você é levado direto a um **prompt de login**.

Não se preocupe, isso é normal e intencional. O FreeBSD é um sistema semelhante ao UNIX, projetado para estabilidade e flexibilidade, não para causar boa impressão na primeira vez. O ambiente padrão é deliberadamente mínimo para que você, o administrador, mantenha controle total. Pense nisso como a primeira vez que você se senta diante de uma máquina de laboratório recém-provisionada: o shell está vazio, as ferramentas ainda não estão instaladas, mas o sistema está pronto para ser moldado exatamente conforme o seu trabalho exige.

Nesta seção, realizaremos os **primeiros passos essenciais** para tornar seu laboratório FreeBSD confortável, seguro e pronto para o desenvolvimento de drivers.

### Fazendo Login

No prompt de login:

- Digite o nome de usuário que você criou durante a instalação.
- Digite sua senha (lembre-se de que sistemas UNIX não exibem `*` ao digitar senhas).

Você está agora dentro do FreeBSD como um usuário comum.

![image-20250823212710535](https://freebsd.edsonbrandi.com/images/image-20250823212710535.png)

### Alternando para o Usuário Root

Algumas tarefas, como instalar software ou editar arquivos do sistema, requerem **privilégios de root**. Você deve evitar ficar logado como root o tempo todo (é arriscado demais se você digitar um comando errado), mas é uma boa prática alternar temporariamente para root quando necessário:

```console
% su -
Password:
```

Digite a senha de root que você definiu durante a instalação. O prompt mudará de `%` para `#`, o que significa que você agora é root.

![image-20250823213238499](https://freebsd.edsonbrandi.com/images/image-20250823213238499.png)

### Configurando o Hostname e o Horário

Seu sistema precisa de um nome e de configurações de horário corretas.

- Para verificar o hostname:

  ```
  % hostname
  ```

  Se quiser alterá-lo, edite o `/etc/rc.conf`:

  ```
  # ee /etc/rc.conf
  ```

  Adicione ou ajuste esta linha:

  ```
  hostname="fbsd-lab"
  ```

- Para sincronizar o horário, certifique-se de que o NTP está habilitado (normalmente já está, se você o selecionou durante a instalação). Você pode testar com:

  ```
  % date
  ```

  Se o horário estiver errado, corrija-o manualmente por enquanto:

  ```
  # date 202508231530
  ```

  (Isso define a data/hora para 23 ago 2025, 15:30. O formato é `YYYYMMDDhhmm`).

### Noções Básicas de Rede

A maioria das instalações com DHCP "simplesmente funciona". Para verificar:

```console
% ifconfig
```

Você deverá ver uma interface (como `em0`, `re0` ou `vtnet0` em VMs) com um endereço IP. Se não aparecer, pode ser necessário habilitar o DHCP no `/etc/rc.conf`:

```ini
ifconfig_em0="DHCP"
```

Substitua `em0` pelo nome real da sua interface, conforme mostrado pelo `ifconfig`.

![image-20250823213433266](https://freebsd.edsonbrandi.com/images/image-20250823213433266.png)

### Instalando e Configurando o `sudo`

Como boa prática, você deve usar `sudo` em vez de alternar para root em cada comando privilegiado.

1. Instale o sudo:

   ```
   # pkg install sudo
   ```

2. Adicione seu usuário ao grupo `wheel` (caso não tenha feito isso ao criá-lo):

   ```
   # pw groupmod wheel -m yourusername
   ```

3. Agora, vamos habilitar o grupo `wheel` para usar o `sudo`.

   Execute o comando `visudo` e procure estas linhas no editor de arquivo que será aberto:

	```sh
	##
	## User privilege specification
	##
	root ALL=(ALL:ALL) ALL

	## Uncomment to allow members of group wheel to execute any command
	# %wheel ALL=(ALL:ALL) ALL

	## Same thing without a password
	#%wheel ALL=(ALL:ALL) NOPASSWD: ALL
	```

Remova o `#` da linha `#%wheel ALL=(ALL:ALL) NOPASSWD: ALL`, posicione o cursor no caractere que deseja excluir usando as teclas de seta e pressione **x**. Para salvar o arquivo e sair do editor, pressione **ESC** e depois digite **:wq** e pressione **Enter**.

4. Para verificar se está funcionando conforme esperado, saia e faça login novamente e então execute:

   ```
   % sudo whoami
   root
   ```

Agora seu usuário pode realizar tarefas administrativas com segurança sem permanecer logado como root.

### Atualizando o Sistema

Antes de instalar as ferramentas de desenvolvimento, atualize seu sistema:

```console
# freebsd-update fetch install
# pkg update
# pkg upgrade
```

Isso garante que você esteja executando as últimas correções de segurança.

![image-20250823215034288](https://freebsd.edsonbrandi.com/images/image-20250823215034288.png)

### Criando um Ambiente Confortável

Mesmo pequenos ajustes tornam seu trabalho diário mais fluido:

- **Habilite o histórico e o autocompletar de comandos** (se você estiver usando `tcsh`, o shell padrão para usuários, isso já está incluído).

- **Edite o `.cshrc`** em seu diretório home para adicionar aliases úteis:

  ```
  alias ll 'ls -lh'
  alias cls 'clear'
  ```

- **Instale um editor mais amigável** (opcional):

  ```
  # pkg install nano
  ```

### Proteção Básica para o Seu Laboratório

Mesmo sendo um **ambiente de laboratório**, é importante adicionar algumas camadas de proteção. Isso é especialmente verdadeiro se você habilitar o **SSH**, seja executando o FreeBSD dentro de uma VM no seu laptop ou em uma máquina física reservada. Com o SSH ativado, seu sistema aceita logins remotos e isso significa que você deve tomar algumas precauções.

Você tem duas abordagens simples. Escolha a que preferir; ambas são adequadas para um laboratório.

#### Opção A: Regras mínimas de `pf` (bloquear tudo de entrada exceto SSH)

1. Habilite o `pf` e crie um conjunto pequeno de regras:

   ```
   # sysrc pf_enable="YES"
   # nano /etc/pf.conf
   ```

   Coloque isso no `/etc/pf.conf` (substitua `vtnet0`/`em0` pela sua interface):

   ```sh
   set skip on lo
   
   ext_if = "em0"           # VM often uses vtnet0; on bare metal you may see em0/re0/igb0, etc.
   tcp_services = "{ ssh }"
   
   block in all
   pass out all keep state
   pass in on $ext_if proto tcp to (self) port $tcp_services keep state
   ```

2. Inicie o `pf` (e ele persistirá entre reinicializações):

   ```
   # service pf start
   ```

**Nota para VM:** Se sua VM usa NAT, talvez seja necessário configurar o **redirecionamento de porta** no seu hypervisor (por exemplo, VirtualBox: Porta do Host 2222 -> Porta do Guest 22) e então conectar por SSH a `localhost -p 2222`. A regra do `pf` acima ainda se aplica **dentro** do guest.

#### Opção B: Use os presets integrados do `ipfw` (muito amigável para iniciantes)

1. Habilite o `ipfw` com o preset `workstation` e abra o SSH:

   ```
   # sysrc firewall_enable="YES"
   # sysrc firewall_type="workstation"
   # sysrc firewall_myservices="22/tcp"
   # sysrc firewall_logdeny="YES"
   # service ipfw start
   ```

   - `workstation` fornece um conjunto de regras stateful que "protege esta máquina" e é fácil de começar.
   - `firewall_myservices` lista os serviços de entrada que você deseja permitir; aqui habilitamos SSH na TCP/22.
   - Você pode alternar para outros presets mais tarde (por exemplo, `client`, `simple`) conforme suas necessidades evoluem.

**Dica:** Escolha **ou** o `pf` **ou** o `ipfw`, não os dois. Para um primeiro laboratório, o preset do `ipfw` é o caminho mais rápido; o pequeno conjunto de regras do `pf` é igualmente adequado e muito explícito.

#### Mantenha o Sistema Atualizado

Execute estes comandos regularmente para se manter atualizado:

```console
% sudo freebsd-update fetch install
% sudo pkg update && pkg upgrade
```

**Por que se preocupar em uma VM?** Porque uma VM ainda é uma máquina real na sua rede. Bons hábitos aqui o preparam para ambientes de produção no futuro.

### Encerrando

Seu sistema FreeBSD não é mais um esqueleto vazio. Agora ele tem um hostname, rede funcionando, um sistema base atualizado e uma conta de usuário com acesso ao `sudo`. Você também aplicou uma camada de proteção pequena, mas significativa: um firewall simples que ainda permite SSH e atualizações regulares. Esses não são apenas ajustes opcionais, são o tipo de hábitos que fazem de você um desenvolvedor de sistemas responsável.

Na próxima seção, instalaremos as **ferramentas de desenvolvimento** necessárias para a programação de drivers, incluindo compiladores, depuradores, editores e a própria árvore de código-fonte do FreeBSD. É aqui que seu laboratório se transforma de uma tela em branco em uma estação de trabalho de desenvolvimento real.

## Preparando o Sistema para Desenvolvimento

Agora que seu laboratório FreeBSD está instalado, atualizado e com uma proteção básica, é hora de transformá-lo em um **ambiente de desenvolvimento de drivers** adequado. Esta etapa adiciona as peças necessárias para construir, depurar e versionar código do kernel: o compilador, o depurador, um sistema de controle de versão e a árvore de código-fonte do FreeBSD. Sem elas, você não conseguirá construir ou testar o código que escreveremos nos capítulos seguintes.

A boa notícia é que o FreeBSD já inclui a maior parte do que precisamos. Nesta seção, instalaremos as peças que faltam, verificaremos que tudo funciona e executaremos um pequeno teste de "hello module" para provar que seu laboratório está pronto para o desenvolvimento de drivers.

### Instalando as Ferramentas de Desenvolvimento

O FreeBSD vem com o **Clang/LLVM** no sistema base. Para confirmar:

```console
% cc --version
FreeBSD clang version 19.1.7 (...)
```

Se você vir uma string de versão como a acima, você está pronto para compilar código C.

Ainda assim, você precisará de algumas ferramentas adicionais:

```console
# pkg install git gmake gdb
```

- `git`: sistema de controle de versão.
- `gmake`: GNU make (alguns projetos o exigem além do `make` nativo do FreeBSD).
- `gdb`: o depurador GNU.

### Escolhendo um Editor

Todo desenvolvedor tem seu editor favorito. O FreeBSD inclui o `vi` por padrão, capaz mas com uma curva de aprendizado íngreme. Se você está começando do zero, pode começar com segurança pelo **`ee` (Easy Editor)**, que oferece ajuda na tela, ou instalar o **`nano`**, que tem atalhos mais simples como Ctrl+O para salvar e Ctrl+X para sair:

```console
% sudo pkg install nano
```

Mas cedo ou tarde, você vai querer aprender o **`vim`**, a versão aprimorada do `vi`. Ele é rápido, altamente configurável e amplamente usado no desenvolvimento para FreeBSD. Uma de suas grandes vantagens é o **realce de sintaxe**, que torna o código C muito mais fácil de ler.

#### Configurando o Vim para Realce de Sintaxe

1. Instale o vim:

   ```
   # pkg install vim
   ```

2. Crie um arquivo de configuração no seu diretório home:

   ```
   % ee ~/.vimrc
   ```

3. Adicione estas linhas:

   ```
   syntax on
   set number
   set tabstop=8
   set shiftwidth=8
   set expandtab
   set autoindent
   set background=dark
   ```

   - `syntax on` -> habilita o realce de sintaxe.
   - `set number` -> exibe números de linha.
   - As configurações de tabulação/indentação seguem o **estilo de código do FreeBSD** (tabulações de 8 espaços, não 4).
   - `set background=dark` -> torna as cores legíveis em um terminal escuro.

4. Salve o arquivo e abra um programa C:

   ```
   % vim hello.c
   ```

   Agora você deverá ver palavras-chave, strings e comentários coloridos.

#### Realce de Sintaxe no Nano

Se você preferir o `nano`, ele também oferece suporte a realce de sintaxe. A configuração fica armazenada em `/usr/local/share/nano/`. Para habilitá-lo para C:

```console
% cp /usr/local/share/nano/c.nanorc ~/.nanorc
```

Agora abra um arquivo `.c` com o `nano` e você verá o realce básico de sintaxe.

#### Easy Editor (ee)

O `ee` é a opção mais simples: sem realce de sintaxe, apenas texto puro. É seguro para iniciantes e ótimo para editar arquivos de configuração rapidamente, mas você provavelmente vai precisar de algo mais robusto à medida que avançar no desenvolvimento de drivers.

### Acessando a Documentação

As **páginas de manual** são a sua biblioteca de referência embutida. Experimente:

```console
% man 9 malloc
```

Isso abre a página de manual da função de kernel `malloc(9)`. O número de seção `(9)` indica que ela faz parte das **interfaces do kernel**, onde passaremos a maior parte do nosso tempo mais adiante.

Outros comandos úteis:

- `man 1 ls` -> documentação de comandos do usuário.
- `man 5 rc.conf` -> formato de arquivos de configuração.
- `man 9 intro` -> visão geral das interfaces de programação do kernel.

### Instalando a Árvore de Código-Fonte do FreeBSD

A maior parte do desenvolvimento de drivers exige acesso ao código-fonte do kernel do FreeBSD. Você o armazenará em `/usr/src`.

A partir deste ponto, sempre que este livro citar um arquivo como `/usr/src/sys/kern/kern_module.c`, ele se refere a um arquivo real dentro da árvore de código-fonte que você está prestes a clonar. `/usr/src` é o local convencional para a árvore de código-fonte do FreeBSD em um sistema FreeBSD, e todo caminho no formato `/usr/src/...` nos capítulos seguintes corresponde diretamente a um arquivo dentro do checkout `src` abaixo. Os capítulos posteriores não vão reexplicar essa convenção; eles simplesmente citarão o caminho e esperarão que você o encontre nesse local.

Clone com Git:

```console
% sudo git clone --branch releng/14.3 --depth 1 https://git.FreeBSD.org/src.git src /usr/src
```

Isso levará alguns minutos e fará o download de vários gigabytes. Quando terminar, você terá a árvore completa de código-fonte do kernel disponível.

Verifique com:

```console
% ls /usr/src/sys
```

Você deverá ver diretórios como `dev`, `kern`, `net` e `vm`. É onde o kernel do FreeBSD vive.

#### Atenção: mantenha seus cabeçalhos sincronizados com o kernel em execução.

O FreeBSD é muito rigoroso quanto à compilação de módulos do kernel carregáveis em relação ao conjunto exato de cabeçalhos que corresponde ao kernel em execução. Se o seu kernel foi compilado a partir do 14.3-RELEASE, mas `/usr/src` aponta para um branch ou versão diferente, você pode se deparar com erros confusos de compilação ou de carregamento. Para evitar problemas nos exercícios apresentados neste livro, certifique-se de ter a árvore de código-fonte do **FreeBSD 14.3** instalada em `/usr/src` e de que ela corresponde ao seu kernel em execução. Uma verificação rápida é `freebsd-version -k`, que deve exibir `14.3-RELEASE`, e o seu `/usr/src` deve estar no branch `releng/14.3` conforme instruído acima.

**Dica**: se `/usr/src` já existir e apontar para outro lugar, você pode redirecioná-lo:

```console
% sudo git -C /usr/src fetch --all --tags
% sudo git -C /usr/src checkout releng/14.3
% sudo git -C /usr/src pull --ff-only
```

Com o kernel e os cabeçalhos alinhados, o seu módulo de exemplo será compilado e carregado sem problemas.

### Testando seu Ambiente: um "Hello Kernel Module"

Para confirmar que tudo funciona, vamos compilar e carregar um módulo do kernel bem simples. Ainda não é um driver, mas prova que o seu laboratório consegue construir e interagir com o kernel.

1. Crie um arquivo chamado `hello_world.c`:

```c
/*
 * hello_world.c - Simple FreeBSD kernel module
 * Prints messages when loaded and unloaded
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

/*
 * Load handler - called when the module is loaded
 */
static int
hello_world_load(module_t mod, int cmd, void *arg)
{
    int error = 0;

    switch (cmd) {
    case MOD_LOAD:
        printf("Hello World! Kernel module loaded.\n");
        break;
    case MOD_UNLOAD:
        printf("Goodbye World! Kernel module unloaded.\n");
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }

    return (error);
}

/*
 * Module declaration
 */
static moduledata_t hello_world_mod = {
    "hello_world",      /* module name */
    hello_world_load,   /* event handler */
    NULL                /* extra data */
};

/*
 * Register the module with the kernel
 * DECLARE_MODULE(name, data, sub-system, order)
 */
DECLARE_MODULE(hello_world, hello_world_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(hello_world, 1);
```

1. Crie um `Makefile`:

```console
# Makefile for hello_world kernel module

KMOD=   hello_world
SRCS=   hello_world.c

.include <bsd.kmod.mk>
```

1. Construa o módulo:

```console
# make
```

Isso deve criar um arquivo `hello.ko`.

1. Carregue o módulo:

```console
# kldload ./hello_world.ko
```

Verifique a mensagem no log do sistema:

```console
% dmesg | tail -n 5
```

Você deverá ver:

`Hello World! Kernel module loaded.`

1. Descarregue o módulo:

```console
# kldunload hello_world.ko
```

Verifique novamente:

```console
% dmesg | tail -n 5
```

Você deverá ver:

`Goodbye World! Kernel module unloaded.`

### Laboratório Prático: Verificando seu Ambiente de Desenvolvimento

1. Instale `git`, `gmake` e `gdb`.
2. Verifique se o Clang está funcionando com `% cc --version`.
3. Instale e configure o `vim` com realce de sintaxe, ou configure o `nano` se preferir.
4. Clone a árvore de código-fonte do FreeBSD 14.3 em `/usr/src`.
5. Escreva, compile e carregue o módulo do kernel `hello_world`.
6. Registre os resultados (você viu a mensagem "Hello, kernel world!"?) no seu **diário de laboratório**.

### Encerrando

Você agora equipou seu laboratório FreeBSD com as ferramentas essenciais: compilador, depurador, controle de versão, documentação e o código-fonte do kernel. Você até construiu e carregou seu primeiro módulo do kernel, comprovando que o ambiente funciona de ponta a ponta.

Na próxima seção, veremos como **usar snapshots e backups** para que você possa experimentar livremente sem medo de perder o seu progresso. Isso lhe dará confiança para assumir riscos maiores e se recuperar rapidamente quando as coisas derem errado.

## Usando Snapshots e Backups

Uma das maiores vantagens de configurar um **ambiente de laboratório** é que você pode experimentar sem medo. Ao escrever código para o kernel, erros são inevitáveis: um ponteiro errado, um loop infinito ou uma rotina de descarregamento defeituosa podem travar todo o sistema operacional. Em vez de se preocupar, você pode encarar os crashes como parte do aprendizado, *desde que* tenha uma forma de se recuperar rapidamente.

É aqui que entram os **snapshots e backups**. Snapshots permitem que você "congele" seu laboratório FreeBSD em um ponto seguro e, em seguida, reverta instantaneamente se algo der errado. Backups protegem seus arquivos importantes, como seu código ou anotações de laboratório, caso você precise reinstalar o sistema.

Nesta seção, exploraremos os dois.

### Snapshots em Máquinas Virtuais

Se você estiver executando o FreeBSD em uma VM (VirtualBox, VMware, bhyve), você tem uma grande rede de segurança: os **snapshots**.

- No **VirtualBox** ou no **VMware**, os snapshots são gerenciados pela interface gráfica. Você pode salvá-los, restaurá-los e excluí-los com alguns cliques.
- No **bhyve**, os snapshots são gerenciados pelo **backend de armazenamento**, geralmente ZFS. Você tira um snapshot do dataset que armazena a imagem de disco da VM e o reverte quando necessário.

#### Exemplo de fluxo de trabalho no VirtualBox

1. Desligue sua VM FreeBSD após concluir a configuração inicial.

2. No VirtualBox Manager, selecione sua VM -> **Snapshots** -> clique em **Take**.

3. Nomeie-o: `Clean FreeBSD 14.3 Install`.

   ![image-20250823231838089](https://freebsd.edsonbrandi.com/images/image-20250823231838089.png)

   ![image-20250823231940246](https://freebsd.edsonbrandi.com/images/image-20250823231940246.png)

4. Mais tarde, antes de testar código de kernel arriscado, tire outro snapshot: `Before Hello Driver`.

   ![image-20250823232320392](https://freebsd.edsonbrandi.com/images/image-20250823232320392.png)

5. Se o sistema travar ou você quebrar a rede, basta restaurar o snapshot.

![image-20250823232420760](https://freebsd.edsonbrandi.com/images/image-20250823232420760.png)

#### Exemplo de fluxo de trabalho no bhyve (com ZFS)

Se o disco da sua VM estiver armazenado em um dataset ZFS, por exemplo `/zroot/vm/freebsd.img`:

1. Crie um snapshot antes dos experimentos:

   ```
   # zfs snapshot zroot/vm@clean-install
   ```

2. Faça mudanças, teste o código ou até mesmo provoque um crash no kernel.

3. Reverta instantaneamente:

   ```
   # zfs rollback zroot/vm@clean-install
   ```

### Snapshots em Hardware Real

Se você estiver executando o FreeBSD diretamente em hardware físico, não terá o luxo dos snapshots pela interface gráfica. Mas se você instalou o FreeBSD com **ZFS**, ainda tem acesso às mesmas ferramentas de snapshot.

Com ZFS:

```console
# zfs snapshot -r zroot@clean-install
```

- Isso cria um snapshot do seu sistema de arquivos raiz.

- Se algo der errado, você pode reverter:

  ```
  # zfs rollback -r zroot@clean-install
  ```

Snapshots ZFS são instantâneos e não duplicam dados: eles apenas rastreiam as alterações. Para laboratórios sérios em hardware real, o ZFS é altamente recomendado.

Se você instalou com **UFS** em vez de ZFS, não terá snapshots. Nesse caso, confie em **backups** (veja abaixo) e talvez considere reinstalar com ZFS mais tarde se quiser essa rede de segurança.

### Fazendo Backup do seu Trabalho

Snapshots protegem o **estado do sistema**, mas você também precisa proteger o seu **trabalho**: o código dos seus drivers, anotações e repositórios Git.

Estratégias simples:

- **Git**: se você está usando Git (e deveria estar), envie seu código para um serviço remoto como GitHub ou GitLab. Esse é o melhor backup.

- **Tarballs**: crie um arquivo comprimido do seu projeto:

  ```
  % tar czf mydriver-backup.tar.gz mydriver/
  ```

- **Copiar para o host**: se estiver usando uma VM, copie os arquivos do guest para o host (pastas compartilhadas do VirtualBox, ou `scp` via SSH).

**Observação**: pense na sua VM como descartável, mas o seu **código é precioso**. Sempre faça backup antes de testar mudanças arriscadas.

### Laboratório Prático: Quebrando e Consertando

1. Se você estiver no VirtualBox/VMware:

   - Crie um snapshot chamado `Before Break`.
   - Como root, execute algo inofensivo, mas destrutivo (por exemplo, apague `/tmp/*`).
   - Restaure o snapshot e confirme que `/tmp` voltou ao normal.

2. Se você estiver no bhyve com armazenamento ZFS:

   - Tire um snapshot do dataset da sua VM.
   - Apague um arquivo de teste dentro do guest.
   - Reverta o snapshot ZFS.

3. Se você estiver em hardware real com ZFS:

   - Tire um snapshot `zroot@before-break`.
   - Apague um arquivo de teste.
   - Reverta com `zfs rollback` e confirme que o arquivo foi restaurado.

4. Faça backup do código-fonte do seu módulo `hello_world` com:

   ```
   % tar czf hello-backup.tar.gz hello_world/
   ```

Registre no seu **diário de laboratório**: qual método você usou, quanto tempo levou e o quanto você se sente confiante agora para experimentar.

### Encerrando

Ao aprender a usar **snapshots e backups**, você adicionou uma das redes de segurança mais importantes ao seu laboratório. Agora você pode causar crashes, quebrar ou configurar mal o FreeBSD e se recuperar em minutos. Essa liberdade é o que torna um laboratório tão valioso: ela permite que você se concentre no aprendizado, sem medo de cometer erros.

Na próxima seção, vamos configurar o **controle de versão com Git** para que você possa acompanhar seu progresso, gerenciar seus experimentos e compartilhar seus drivers.

## Configurando o Controle de Versão

Até agora, você preparou seu laboratório FreeBSD, instalou as ferramentas e até mesmo construiu seu primeiro módulo do kernel. Mas imagine o seguinte: você faz uma alteração no seu driver, testa e, de repente, nada funciona mais. Você gostaria de poder voltar à última versão que funcionava. Ou talvez queira manter dois experimentos diferentes sem misturá-los.

É exatamente para isso que os desenvolvedores usam **sistemas de controle de versão**: ferramentas que registram o histórico do seu trabalho, permitem reverter para estados anteriores e facilitam o compartilhamento de código com outras pessoas. No mundo FreeBSD (e na maioria dos projetos de código aberto), o padrão é o **Git**.

Nesta seção, você aprenderá a usar o Git para gerenciar seus drivers desde o primeiro dia.

### Por que o Controle de Versão é Importante

- **Acompanhe suas mudanças**: cada experimento, cada correção, cada erro fica salvo.
- **Desfaça com segurança**: se o seu código parar de funcionar, você pode reverter para uma versão que funcionava.
- **Organize experimentos**: você pode trabalhar em novas ideias em "branches" sem quebrar o código principal.
- **Compartilhe seu trabalho**: se você quiser feedback de outras pessoas ou publicar seus drivers, o Git facilita tudo.
- **Hábito profissional**: todo projeto de software sério (incluindo o próprio FreeBSD) usa controle de versão.

Pense no Git como o **diário de laboratório do seu código**, só que mais inteligente: ele não apenas registra o que você fez, mas também pode restaurar seu código a qualquer ponto do passado.

### Instalando o Git

Se você ainda não instalou o Git na seção 2.5, faça isso agora:

```console
# pkg install git
```

Verifique a versão:

```console
% git --version
git version 2.45.2
```

### Configurando o Git (Sua Identidade)

Antes de usar o Git, configure sua identidade para que seus commits sejam rotulados corretamente:

```console
% git config --global user.name "Your Name"
% git config --global user.email "you@example.com"
```

Não precisa ser seu nome ou e-mail real se você estiver apenas experimentando localmente, mas se algum dia você compartilhar código publicamente, é melhor usar algo consistente.

Você pode verificar suas configurações com:

```console
% git config --list
```

### Criando seu Primeiro Repositório

Vamos colocar o seu módulo `hello_world` sob controle de versão.

1. Navegue até o diretório onde você criou `hello_world.c` e o `Makefile`.

2. Inicialize um repositório Git:

   ```
   % git init
   ```

   Isso cria um diretório oculto `.git` onde o Git armazena seu histórico.

3. Adicione seus arquivos:

   ```
   % git add hello_world.c Makefile
   ```

4. Faça seu primeiro commit:

   ```
   % git commit -m "Initial commit: hello_world kernel module"
   ```

5. Verifique o histórico:

   ```
   % git log
   ```

   Você deverá ver o seu commit listado.

### Boas Práticas para Commits

- **Escreva mensagens de commit claras**: descreva o que mudou e por quê.

  - Ruim: `fix stuff`
  - Bom: `Fix null pointer dereference in hello_loader()`

- **Faça commits com frequência**: commits pequenos são mais fáceis de entender e de reverter.

- **Mantenha experimentos separados**: se você quiser testar uma nova ideia, crie um branch:

  ```
  % git checkout -b experiment-null-fix
  ```

Mesmo que você nunca compartilhe seu código, esses hábitos vão ajudá-lo a depurar e aprender mais rápido.

### Usando Repositórios Remotos (Opcional)

Por enquanto, você pode manter tudo localmente. Mas se quiser sincronizar seu código entre máquinas ou compartilhá-lo publicamente, você pode enviá-lo para um serviço remoto como **GitHub** ou **GitLab**.

Fluxo básico:

```console
% git remote add origin git@github.com:yourname/mydriver.git
% git push -u origin main
```

Isso é opcional no laboratório, mas muito útil se você quiser fazer backup do seu trabalho na nuvem.

### Laboratório Prático: Controle de Versão para o Seu Driver

1. Inicialize um repositório Git no diretório do seu módulo `hello`.

2. Faça o seu primeiro commit.

3. Edite `hello_world.c` (por exemplo, altere o texto da mensagem).

4. Execute:

   ```
   % git diff
   ```

   para ver exatamente o que mudou.

5. Faça o commit da alteração com uma mensagem clara.

6. Registre no seu **diário de laboratório**:

   - Quantos commits você fez.
   - O que cada commit fez.
   - Como você reverteria as mudanças se algo quebrasse.

### Encerrando

Você deu agora os primeiros passos com o Git, uma das ferramentas mais importantes no seu kit de ferramentas de desenvolvedor. A partir de agora, todo driver que você escrever neste livro deve viver em seu próprio repositório Git. Assim, você nunca perderá o seu progresso e sempre terá um registro dos seus experimentos.

Na próxima seção, vamos discutir **documentar o seu trabalho**, outro hábito fundamental de desenvolvedores profissionais. Um README bem escrito ou uma mensagem de commit clara pode ser a diferença entre um código que você entende um ano depois e um código que você precisa reescrever do zero.

## Documentando o Seu Trabalho

O desenvolvimento de software não é apenas escrever código: é também garantir que *você* (e às vezes outras pessoas) consiga entender esse código mais tarde. Ao trabalhar com drivers FreeBSD, você frequentemente voltará a um projeto semanas ou meses depois e se perguntará: *"Por que escrevi isso? O que eu estava testando? O que mudei?"*

Sem documentação, você vai perder horas redescobindo o seu próprio raciocínio. Com boas anotações, você consegue retomar exatamente de onde parou.

Pense na documentação como a **memória do seu laboratório**. Assim como cientistas mantêm cadernos de laboratório detalhados, desenvolvedores devem manter anotações claras, READMEs e mensagens de commit.

### Por Que a Documentação Importa

- **O você do futuro vai agradecer**: Os detalhes que parecem óbvios hoje serão esquecidos em um mês.
- **A depuração fica mais fácil**: Quando algo quebra, as anotações ajudam a entender o que mudou.
- **O compartilhamento fica mais tranquilo**: Se você publicar o seu driver, outras pessoas poderão aprender com o seu README.
- **Hábito profissional**: O FreeBSD é famoso pela alta qualidade da sua documentação; seguir essa tradição faz com que o seu trabalho se encaixe naturalmente no ecossistema.

### Escrevendo um README Simples

Todo projeto deve começar com um arquivo `README.md`. No mínimo, inclua:

1. **Nome do projeto**:

   ```
   Hello Kernel Module
   ```

2. **Descrição**:

   ```
   A simple "Hello, kernel world!" module for FreeBSD 14.3.
   ```

3. **Como construir**:

   ```
   % make
   ```

4. **Como carregar/descarregar**:

   ```
   # kldload ./hello_world.ko
   # kldunload hello_world
   ```

5. **Observações**:

   ```
   This was created as part of my driver development lab, Chapter 2.
   ```

### Usando Mensagens de Commit como Documentação

As mensagens de commit do Git são uma forma de documentação. Juntas, elas contam a história do seu projeto. Siga estas dicas:

- Escreva as mensagens de commit no presente ("Add feature", não "Added feature").
- Mantenha a primeira linha curta (50 caracteres ou menos).
- Se necessário, adicione uma linha em branco seguida de uma explicação mais longa.

Exemplo:

```text
Fix panic when unloading hello module

The handler did not check for NULL before freeing resources,
causing a panic when unloading. Added a guard condition.
```

### Mantendo um Diário de Laboratório

Na Seção 2.1, sugerimos que você começasse um diário de laboratório. Agora é um bom momento para tornar isso um hábito. Mantenha um arquivo de texto (por exemplo, `LABLOG.md`) na raiz do seu repositório Git. Cada vez que você experimentar algo novo, adicione uma entrada curta:

```text
2025-08-23
- Built hello module successfully.
- Confirmed "Hello, kernel world!" appears in dmesg.
- Tried unloading/reloading multiple times, no errors.
- Next step: experiment with passing parameters to the module.
```

Esse registro não precisa ser polido; ele é apenas para você. Mais tarde, durante a depuração, essas anotações podem ser inestimáveis.

### Ferramentas que Ajudam

- **Markdown**: Tanto o README quanto os diários de laboratório podem ser escritos em Markdown (`.md`), que é fácil de ler em texto simples e fica bem formatado no GitHub/GitLab.
- **man pages**: Sempre anote quais man pages você usou (por exemplo, `man 9 module`). Isso vai lembrar você das suas fontes.
- **Capturas de tela/Logs**: Se você estiver usando uma VM, tire capturas de tela das etapas importantes ou salve saídas de comandos em arquivos com redirecionamento (`dmesg > dmesg.log`).

### Laboratório Prático: Documentando o Seu Primeiro Módulo

1. No diretório do seu módulo `hello_world`, crie um `README.md` descrevendo o que ele faz, como construí-lo e como carregá-lo/descarregá-lo.

2. Adicione o seu `README.md` ao Git e faça o commit:

   ```
   % git add README.md
   % git commit -m "Add README for hello_world module"
   ```

3. Crie um arquivo `LABLOG.md` e registre as atividades de hoje.

4. Revise o seu histórico Git com:

   ```
   % git log --oneline
   ```

   para ver como os seus commits contam a história do seu projeto.

### Encerrando

Você aprendeu agora como documentar os seus experimentos com drivers FreeBSD para nunca perder o rastro do que fez ou do porquê. Com um `README`, mensagens de commit significativas e um diário de laboratório, você está construindo hábitos que farão de você um desenvolvedor mais profissional e eficiente.

Na próxima seção, vamos concluir este capítulo revisando tudo o que você construiu: um laboratório FreeBSD seguro com as ferramentas certas, backups, controle de versão e documentação, tudo pronto para a exploração mais aprofundada do FreeBSD em si no Capítulo 3.

## Encerrando

Parabéns! Você construiu o seu laboratório FreeBSD!

Neste capítulo, você:

- Entendeu por que um **ambiente de laboratório seguro** é fundamental para o desenvolvimento de drivers.
- Escolheu a configuração certa para a sua situação: uma **máquina virtual** ou **bare metal**.
- Instalou o **FreeBSD 14.3** passo a passo.
- Realizou a **configuração inicial**, incluindo rede, usuários e medidas básicas de segurança.
- Instalou as **ferramentas de desenvolvimento** essenciais: compilador, depurador, Git e editores.
- Configurou o **realce de sintaxe** no seu editor, tornando o código C mais fácil de ler.
- Clonou a **árvore de código-fonte do FreeBSD 14.3** para `/usr/src`.
- Compilou e testou o seu primeiro **módulo do kernel**.
- Aprendeu a usar **snapshots e backups** para se recuperar rapidamente de erros.
- Começou a usar o **Git** para controle de versão e adicionou um **README** e um **diário de laboratório** para documentar o seu trabalho.

Isso representa uma quantidade impressionante de progresso para um único capítulo. Você agora tem uma oficina completa: um sistema FreeBSD onde pode escrever, construir, testar, quebrar e recuperar o quanto precisar.

O aprendizado mais importante não são apenas as ferramentas que você instalou, mas a **mentalidade**:

- Espere cometer erros.
- Registre o seu processo.
- Use snapshots, backups e Git para se recuperar e aprender.

### Exercícios

1. **Snapshots**
   - Tire um snapshot da sua VM ou um snapshot ZFS no bare metal.
   - Faça deliberadamente uma alteração (por exemplo, remova `/tmp/testfile`).
   - Reverta e verifique se o sistema foi restaurado.
2. **Controle de Versão**
   - Faça uma pequena edição no seu módulo do kernel `hello_world.c`.
   - Faça o commit da alteração com o Git.
   - Use `git log` e `git diff` para revisar o seu histórico.
3. **Documentação**
   - Adicione uma nova entrada ao seu `LABLOG.md` descrevendo o trabalho de hoje.
   - Atualize o seu `README.md` com uma nova observação (por exemplo, mencione a saída de `uname -a`).
4. **Reflexão**
   - No seu diário de laboratório, responda: *Quais são as três redes de segurança mais importantes que configurei no Capítulo 2?*

### O Que Vem a Seguir

No próximo capítulo, vamos entrar no seu novo laboratório FreeBSD e explorar como **usar o sistema em si**. Você aprenderá os fundamentos de comandos UNIX, navegação e gerenciamento de arquivos. Essas habilidades farão com que você se sinta confortável dentro do FreeBSD, preparando você para os tópicos mais avançados que virão.

O seu laboratório está pronto. Agora é hora de aprender a trabalhar nele.
