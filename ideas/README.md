# OGUI --- Roadmap de funcionalidades e melhorias

> Documento de planejamento para a evolução da OGUI, uma biblioteca de
> interface gráfica em C++/OpenGL com layouts XML, estilos e scripting
> Lua.
>
> **Objetivo:** organizar as melhorias propostas por prioridade,
> favorecendo primeiro uma base consistente e extensível; depois,
> recursos declarativos e widgets avançados.

Este documento é uma proposta de roadmap. Os nomes de APIs, elementos
XML e funcionalidades futuras são exemplos de design, não uma afirmação
de que já estejam implementados.

------------------------------------------------------------------------

## Índice

-   [Princípios de desenvolvimento](#princípios-de-desenvolvimento)
-   [Níveis de prioridade](#níveis-de-prioridade)
-   [P0 --- Fundação e consistência da
    API](#p0--fundação-e-consistência-da-api)
-   [P1 --- Lua, eventos e ciclo de
    vida](#p1--lua-eventos-e-ciclo-de-vida)
-   [P2 --- XML, componentes e layout](#p2--xml-componentes-e-layout)
-   [P3 --- Widgets avançados](#p3--widgets-avançados)
-   [P4 --- Acessibilidade, internacionalização e
    qualidade](#p4--acessibilidade-internacionalização-e-qualidade)
-   [Melhorias internas de
    arquitetura](#melhorias-internas-de-arquitetura)
-   [Exemplos de API e XML](#exemplos-de-api-e-xml)
-   [Roadmap sugerido](#roadmap-sugerido)
-   [Critérios de conclusão](#critérios-de-conclusão)

------------------------------------------------------------------------

## Princípios de desenvolvimento

1.  **Priorizar fundamentos antes da quantidade de widgets.** Uma API
    coerente e extensível vale mais do que adicionar dezenas de
    controles isolados.
2.  **Manter XML declarativo e Lua responsável pelo comportamento.** XML
    descreve estrutura, propriedades e ligações; Lua implementa a lógica
    da aplicação.
3.  **Oferecer uma API C++ extensível.** Deve ser possível registrar
    novos tipos de widget sem modificar o núcleo da biblioteca.
4.  **Separar a interface do backend gráfico.** Preservar uma camada de
    desenho independente do backend OpenGL facilita testes e futuras
    mudanças.
5.  **Favorecer desempenho previsível.** Evitar atualizações completas
    da árvore quando apenas um widget ou propriedade mudou.
6.  **Projetar recursos avançados sobre primitivas compartilhadas.**
    Eventos, propriedades tipadas, referências seguras, layout e estilos
    devem servir a todos os widgets.
7.  **Validar e documentar os layouts.** Erros de XML, estilos e
    bindings devem ser claros e localizáveis.

## Níveis de prioridade

  -----------------------------------------------------------------------
  Prioridade              Significado             Critério
  ----------------------- ----------------------- -----------------------
  **P0 --- Crítica**      Fundação da biblioteca  Desbloqueia ou
                                                  simplifica várias
                                                  outras funcionalidades;
                                                  reduz inconsistências e
                                                  riscos arquiteturais.

  **P1 --- Alta**         Comportamento e         Torna a biblioteca
                          scripting               realmente dinâmica e
                                                  prática para aplicações
                                                  Lua/XML.

  **P2 --- Média-alta**   Produtividade           Facilita reutilização,
                          declarativa             responsividade e
                                                  manutenção de
                                                  interfaces.

  **P3 --- Média**        Widgets e ferramentas   Expande os casos de uso
                          especializadas          depois que a base
                                                  estiver estável.

  **P4 --- Contínua**     Qualidade e refinamento Melhora robustez,
                                                  acessibilidade,
                                                  compatibilidade e
                                                  experiência de
                                                  desenvolvimento.
  -----------------------------------------------------------------------

As prioridades indicam ordem sugerida de implementação, não tamanho do
recurso. Itens podem ser divididos em entregas menores.

------------------------------------------------------------------------

## P0 --- Fundação e consistência da API

### 1. Sistema de propriedades tipadas

Criar uma camada uniforme de propriedades para os widgets, em vez de
depender exclusivamente de atributos genéricos e conversões específicas
por tipo.

**Requisitos:** - Declarar propriedades com tipo, valor padrão e regras
de validação. - Suportar tipos básicos como `bool`, `int`, `float`,
`string`, cores, dimensões e enums. - Converter valores de XML e Lua de
maneira consistente. - Informar erros quando um valor não for válido ou
quando a propriedade não existir. - Marcar as propriedades que afetam
layout, desenho ou estado. - Permitir que widgets personalizados
declarem suas próprias propriedades. - Definir quais propriedades podem
ser animadas.

**Por que é prioritário:** bindings, estilos, animações, validação XML e
widgets personalizados ficam mais simples quando compartilham o mesmo
sistema.

### 2. Eventos tipados e propagação

Padronizar os eventos disponíveis para todos os widgets.

**Eventos a considerar:** - `click`, `doubleClick`, `rightClick` -
`change`, `submit`, `selectionChanged` - `focus`, `blur` - `keydown`,
`keyup` - `open`, `close` - `scroll` - eventos de entrada e saída do
ponteiro, como `pointerEnter` e `pointerLeave`

**Requisitos:** - Definir uma estrutura consistente para os dados de
cada evento. - Implementar propagação pela árvore de widgets, com fases
de captura e de bubbling. - Permitir interromper a propagação quando
necessário. - Definir claramente a ordem de execução dos callbacks. -
Evitar que a destruição de widgets durante um evento invalide
referências ainda em uso. - Documentar como eventos de mouse, teclado e
toque se relacionam.

### 3. Referências seguras e índice de IDs

**Requisitos:** - Disponibilizar referências/handles gerenciados para
uso em Lua, sem expor ponteiros C++ crus. - Definir o comportamento de
uma referência quando o widget é destruído. - Manter um índice de IDs
para evitar percorrer a árvore inteira em cada busca. - Validar IDs
duplicados e definir o escopo dos IDs. - Atualizar o índice corretamente
em operações de criação, remoção e mudança de árvore.

### 4. Validação de XML e estilos

**Requisitos:** - Validar nomes de widgets, propriedades, tipos e
valores. - Apontar arquivo, elemento e atributo responsáveis por cada
erro. - Diferenciar erros fatais, avisos e atributos desconhecidos. -
Validar referências a estilos, templates, scripts e widgets por ID. -
Disponibilizar um modo de validação que não precise abrir a interface
completa. - Manter mensagens de erro úteis para quem está criando
layouts.

### 5. Testes automatizados e métricas básicas

Criar testes para os contratos fundamentais: - Carregamento e validação
de XML. - Criação, busca, reparenting e destruição de widgets. - Eventos
e foco por teclado. - Conversão e validação de propriedades. - Execução
de callbacks Lua e tratamento de erros. - Animações e atualizações de
layout. - Testes de regressão para widgets existentes.

Adicionar métricas simples para medir tempo de layout, desenho, busca
por ID e atualização da árvore.

------------------------------------------------------------------------

## P1 --- Lua, eventos e ciclo de vida

### 6. Criação e manipulação dinâmica de widgets em Lua

Permitir que scripts criem, removam e reorganizem a interface em tempo
de execução.

**API ilustrativa:**

``` lua
local popup = ogui.widget.create("Container", {
    id = "popupTest"
})

ogui.widget.append(screen, popup)
ogui.widget.remove(popup)
ogui.widget.destroy(popup)
ogui.widget.clear(container)
ogui.widget.exists("popupTest")
ogui.widget.find("popupTest")
ogui.widget.children(container)
```

**Requisitos:** - Manter a mesma validação de propriedades usada pelo
XML. - Definir explicitamente quem é responsável pelo ciclo de vida de
cada widget. - Evitar IDs duplicados. - Garantir que callbacks,
animações, timers e bindings sejam limpos quando o widget for
destruído. - Permitir operações como anexar, remover, limpar e reordenar
filhos. - Considerar enfileirar mutações estruturais quando ocorrerem
durante a propagação de eventos ou a travessia da árvore.

Os nomes e assinaturas acima são exemplos; a API definitiva deve seguir
a convenção já adotada pela biblioteca.

### 7. API de eventos para Lua

Permitir conectar callbacks a eventos sem depender de uma função global
diferente para cada controle.

**Exemplo ilustrativo:**

``` lua
ogui.event.on("startButton", "click", function(event)
    print("Iniciar")
end)

ogui.event.off("startButton", "click", callback)
```

**Requisitos:** - Registrar e remover listeners. - Definir se vários
listeners podem ouvir o mesmo evento. - Oferecer acesso seguro ao widget
e aos dados do evento. - Permitir interromper a propagação quando
aplicável. - Limpar listeners associados a widgets destruídos. - Manter
compatibilidade com bindings declarados em XML, se já fizerem parte do
fluxo da biblioteca.

### 8. Timers Lua integrados ao ciclo de atualização

Disponibilizar timers sem exigir que cada aplicação implemente seu
próprio relógio.

**API ilustrativa:**

``` lua
local timeout = ogui.timer.after(1.0, function()
    print("Executado uma vez")
end)

local interval = ogui.timer.every(0.5, function()
    updateStatus()
end)

ogui.timer.cancel(interval)
```

**Requisitos:** - Basear os timers no relógio/loop da biblioteca. -
Oferecer cancelamento explícito. - Definir se os tempos são em segundos
e como lidam com pausas. - Evitar callbacks após o encerramento do
contexto Lua ou da tela associada. - Considerar timers associados ao
ciclo de vida de uma tela ou widget. - Definir como erros de callback
são registrados.

### 9. Ciclo de vida das telas

Padronizar o comportamento ao carregar, exibir, ocultar, trocar e
descarregar telas.

**Hooks Lua sugeridos:** - `ogui_on_load` - `ogui_on_show` -
`ogui_on_hide` - `ogui_on_unload` - `ogui_on_update`, se necessário para
lógica específica da aplicação

**API ilustrativa:**

``` lua
ogui.screen.push("settings.xml")
ogui.screen.pop()
ogui.screen.replace("main.xml")
local current = ogui.screen.current()
```

**Requisitos:** - Documentar exatamente quando cada hook é chamado. -
Definir se uma tela oculta continua atualizando. - Permitir retornar à
tela anterior sem reconstruí-la, quando apropriado. - Limpar timers,
listeners e bindings ligados à tela. - Isolar estado de telas quando
necessário, sem impedir o compartilhamento intencional de serviços.

### 10. Isolamento de ambientes Lua

Evitar colisões acidentais de funções e variáveis globais entre scripts
e telas.

**Requisitos:** - Definir se os scripts compartilham um único ambiente
ou se cada tela possui um ambiente próprio. - Preferir módulos e tabelas
de contexto explícitas a globais implícitas. - Permitir compartilhar
serviços e estado deliberadamente. - Documentar a ordem de carregamento
dos scripts. - Manter diagnósticos claros para erros de compilação e
execução. - Considerar recarga de scripts para desenvolvimento, se isso
se encaixar no escopo da biblioteca.

### 11. Sistema de bindings de dados

Adicionar bindings declarativos de uma via e, posteriormente, de duas
vias.

**Objetivos:** - Atualizar a interface quando um valor de estado
mudar. - Sincronizar controles editáveis com o modelo quando
necessário. - Evitar atualizações redundantes e ciclos de atualização. -
Definir como erros de conversão são reportados. - Desvincular
automaticamente os listeners quando um widget ou tela for destruído.

A primeira versão pode ser simples: propriedades observáveis e
atualização unidirecional. O binding bidirecional deve vir depois que o
sistema de propriedades e eventos estiver estável.

------------------------------------------------------------------------

## P2 --- XML, componentes e layout

### 12. Templates e componentes reutilizáveis

Permitir definir um componente uma vez e reutilizá-lo em diferentes
telas.

**Capacidades desejadas:** - Parâmetros e valores padrão. - IDs locais
ao componente, sem colisões com a tela hospedeira. - Slots ou áreas para
inserir conteúdo. - Eventos expostos pelo componente. - Estilos herdados
ou sobrescritos. - Composição de componentes. - Validação de parâmetros
obrigatórios e tipos. - Mensagens de erro que identifiquem o componente
de origem.

**Exemplo conceitual:**

``` xml
<template name="PrimaryButton">
    <Button class="primaryButton" text="${text}" />
</template>

<screen>
    <PrimaryButton id="startButton" text="Iniciar" />
</screen>
```

A sintaxe é ilustrativa e deve ser definida conforme o parser existente.
Evitar criar um sistema de templates complexo antes de estabilizar
propriedades e validação.

### 13. Melhorias de layout responsivo

Expandir o sistema de layout com regras consistentes e previsíveis.

**Propriedades a considerar:** - `minWidth`, `maxWidth`, `minHeight`,
`maxHeight` - `margin`, `padding` e `gap` - `flexGrow`, `flexShrink` e
`flexBasis` - quebra de linha em layouts horizontais (`wrap`) -
alinhamento horizontal e vertical - dimensões intrínsecas do conteúdo -
colunas e linhas de grid com limites mínimos e máximos - comportamento
explícito para conteúdo que excede os limites

**Objetivo:** reduzir a necessidade de posições absolutas e permitir que
a mesma interface se adapte a diferentes resoluções e proporções.

**Exemplo conceitual:**

``` xml
<Column gap="12" padding="16">
    <Label text="Configurações" />
    <Row gap="8" wrap="true">
        <Button text="Aplicar" flexGrow="1" />
        <Button text="Cancelar" />
    </Row>
</Column>
```

### 14. Estados visuais no sistema de estilos

Permitir que estilos dependam do estado real do widget.

**Estados a considerar:** - `hover` - `pressed` - `focused` -
`disabled` - `checked` - `selected` - `invalid`

**Requisitos:** - Definir uma sintaxe consistente para seletores de
estado. - Estabelecer precedência entre estilo padrão, classe, estado e
sobrescritas locais. - Garantir que o estado visual venha do widget, e
não de uma duplicação manual em Lua. - Permitir transições entre estados
quando houver suporte a animações.

### 15. Transições declarativas e propriedades animáveis

Complementar o sistema de tween existente com regras declarativas.

**Possíveis propriedades animáveis:** - opacidade; - cor; - posição e
dimensões; - propriedades numéricas compatíveis; - escala, caso seja
suportada pela transformação de desenho.

**Requisitos:** - Definir duração, easing e comportamento de
interrupção. - Evitar conflito entre animações simultâneas que alterem a
mesma propriedade. - Respeitar o ciclo de vida do widget. - Integrar
animações com invalidação de desenho/layout. - Começar com propriedades
bem definidas, sem tornar toda propriedade animável automaticamente.

------------------------------------------------------------------------

## P3 --- Widgets avançados

A ordem dentro desta seção pode variar conforme o uso pretendido pela
aplicação. Recomenda-se implementar primeiro os widgets que também
testem os recursos fundamentais.

### 16. VirtualList --- lista virtualizada

**Prioridade dentro dos widgets: muito alta.**

Uma lista virtualizada cria e mantém apenas os elementos visíveis e uma
pequena margem ao redor deles, em vez de criar um widget para cada item
de uma coleção grande.

**Requisitos:** - Suporte a conjuntos de dados grandes. - Reutilização
de widgets de item. - Seleção e navegação por teclado. - Rolagem suave e
altura fixa ou variável, se viável. - Atualizações incrementais de
inserção, remoção e alteração. - Separação entre dados e widgets
visíveis.

### 17. PropertyGrid --- grade de propriedades

Exibe propriedades editáveis de um objeto, com rótulos, valores e
controles adequados ao tipo.

**Usos:** - editores de configuração; - ferramentas de
desenvolvimento; - inspeção de widgets; - editores de manifestos e
objetos.

**Recursos:** - agrupamento por categoria; - editores específicos para
booleanos, números, texto, enums e cores; - validação e mensagens de
erro; - valores somente leitura; - alteração via Lua ou C++; -
integração natural com o sistema de propriedades tipadas.

### 18. Console / LogView

Um painel para mensagens de log e depuração.

**Recursos:** - níveis de log; - filtro por texto e categoria; - rolagem
eficiente; - opção de seguir as mensagens mais recentes; - seleção e
cópia de texto; - limite de histórico; - integração com o sistema de
logs da biblioteca; - tratamento eficiente de grande volume de
mensagens.

### 19. TreeTable

Combina hierarquia de árvore com colunas tabulares.

**Recursos:** - expandir e recolher nós; - colunas configuráveis; -
seleção de linhas; - ordenação, quando aplicável; - navegação por
teclado; - virtualização para grandes conjuntos de dados.

### 20. DockPanel / docking

Permite dividir uma área em painéis e, futuramente, mover ou encaixar
painéis.

**Etapas sugeridas:** 1. Divisores redimensionáveis e painéis aninhados.
2. Painéis recolhíveis e áreas com abas. 3. Arrastar, soltar e
reencaixar painéis. 4. Persistência de layouts, caso seja necessária.

O docking completo é complexo; começar com painéis divisíveis evita
comprometer o escopo cedo demais.

### 21. CommandPalette / Terminal

Uma interface rápida para executar comandos ou localizar ações.

**Recursos possíveis:** - busca incremental; - atalhos de teclado; -
ações registradas pela aplicação; - histórico de comandos; - sugestões e
categorias; - integração com logs e ferramentas de desenvolvimento.

A paleta de comandos pode ser implementada antes de um terminal
completo, pois exige menos recursos e já oferece valor significativo.

### 22. GraphEditor / NodeEditor

Editor visual de nós conectados por arestas.

**Recursos possíveis:** - nós e portas tipadas; - conexões; - pan e
zoom; - seleção múltipla; - movimentação de nós; - validação de
conexões; - serialização do grafo; - API de desenho e interação
extensível.

É um recurso especializado e deve ficar depois da base de eventos,
canvas, transformação e seleção.

### 23. Widgets menores complementares

Adicionar conforme a necessidade real, reutilizando as primitivas
comuns:

-   `NumberInput` --- edição numérica com limites e incremento.
-   `Keybind` --- captura e exibição de combinações de teclas.
-   `Shortcut` --- representação e integração de atalhos.
-   `Breadcrumb` --- navegação hierárquica.
-   `StatusBar` --- estado e informações resumidas.
-   `NotificationCenter` --- notificações persistentes e temporárias.
-   `MultiSelect` --- seleção de múltiplos itens.
-   `Markdown` --- exibição de conteúdo Markdown.
-   `RichText` --- texto com estilos e segmentos.
-   `Canvas` --- desenho personalizado e interação em uma área.
-   `ImageView` --- visualização de imagens, com opções de ajuste e
    escala.

Antes de adicionar cada widget, verificar se ele pode ser composto a
partir dos controles existentes ou se realmente precisa de comportamento
próprio.

------------------------------------------------------------------------

## P4 --- Acessibilidade, internacionalização e qualidade

### 24. Navegação por teclado e acessibilidade

-   Garantir ordem de foco previsível.
-   Exibir foco visual claramente.
-   Definir comportamento de Tab, Shift+Tab, Enter, Espaço e Escape.
-   Permitir navegação por teclado em listas, árvores, tabelas, menus e
    popups.
-   Oferecer nomes acessíveis, rótulos e descrições para controles.
-   Distinguir estado desabilitado, selecionado, focado e inválido.
-   Documentar limitações de acessibilidade conforme a plataforma e o
    backend.

### 25. Texto avançado e Unicode

Avaliar integração com HarfBuzz e mecanismos de fallback de fontes para
melhorar a renderização de texto.

**Objetivos:** - modelagem de texto Unicode; - shaping para scripts
complexos; - suporte a ligaturas e posicionamento de glifos; - fallback
entre fontes; - tratamento de direção do texto quando aplicável; -
medição e quebra de linha consistentes.

A implementação deve considerar dependências, tamanho do binário,
desempenho e plataformas-alvo. Não é necessário introduzir todas as
dependências de uma vez.

### 26. DPI, escala e unidades lógicas

Padronizar como dimensões e fontes se comportam em diferentes densidades
e resoluções.

**Requisitos:** - definir unidades lógicas e sua conversão para
pixels; - documentar o papel de `Screen::scale`; - evitar aplicar a
escala duas vezes; - testar layouts em escalas e proporções
diferentes; - garantir consistência entre desenho, hit-testing, fontes e
entrada.

### 27. Depuração e ferramentas de desenvolvimento

-   Visualizador da árvore de widgets.
-   Inspeção de propriedades em tempo de execução.
-   Destaque de limites e áreas de hit-testing.
-   Diagnóstico de layout e clipping.
-   Visualização de foco e ordem de navegação.
-   Estatísticas de tempo de layout e desenho.
-   Logs de eventos e bindings, ativáveis em modo de depuração.
-   Validação de XML integrada ao fluxo de desenvolvimento.

Essas ferramentas podem ser construídas em cima do `PropertyGrid`,
`Console` e APIs de inspeção, em vez de criarem sistemas paralelos.

### 28. Documentação e exemplos

-   Documentar todos os widgets e suas propriedades.
-   Documentar tipos, valores padrão e regras de validação.
-   Disponibilizar exemplos mínimos de XML e Lua.
-   Criar exemplos de eventos, timers, bindings e ciclo de vida.
-   Mostrar como registrar um widget C++ personalizado.
-   Explicar a ordem de carregamento e os escopos de scripts.
-   Manter exemplos testados para evitar documentação desatualizada.

------------------------------------------------------------------------

## Melhorias internas de arquitetura

Estas melhorias atravessam várias prioridades e devem ser implementadas
de acordo com o que os testes e as medições indicarem.

### Invalidação seletiva

-   Separar invalidação de layout, desenho e dados.
-   Evitar recalcular toda a árvore por alterações locais.
-   Agrupar atualizações quando várias propriedades mudarem em
    sequência.
-   Garantir que animações invalidem apenas o que for necessário.

### Segurança da árvore de widgets

-   Definir regras para alterações estruturais durante callbacks.
-   Evitar referências pendentes em eventos, timers e animações.
-   Determinar como widgets removidos deixam de receber atualizações.
-   Centralizar a limpeza de recursos ligados ao ciclo de vida.

### Registro de widgets C++

Implementar um `WidgetRegistry` ou mecanismo equivalente para registrar
tipos de widget e criá-los a partir de XML.

**Capacidades:** - registrar um tipo com nome e função de criação; -
declarar propriedades e seus validadores; - integrar com o parser XML; -
produzir erros úteis para tipos desconhecidos; - permitir widgets
personalizados sem modificar o núcleo.

Começar com registro estático/em processo. Plugins em bibliotecas
compartilhadas devem ser considerados somente depois, pois trazem
questões de ABI, compatibilidade de versões, carregamento e
descarregamento.

### Camada de desenho

Preservar uma fronteira clara entre a lista de comandos de desenho e o
backend OpenGL. Adicionar capacidades como clipping, transformações,
linhas e texturas de acordo com necessidades concretas dos widgets, sem
acoplar a lógica dos widgets diretamente ao backend.

### Desempenho medido

Antes de otimizações complexas: 1. criar cenários de teste
reproduzíveis; 2. medir layout, desenho, busca por ID e atualização; 3.
identificar gargalos reais; 4. otimizar o caminho mais caro; 5. manter
testes de regressão de desempenho.

------------------------------------------------------------------------

## Exemplos de API e XML

Os exemplos desta seção são **conceituais**. Eles mostram a direção
desejada, não uma API final obrigatória.

### Lua manipulando a tela

``` lua
local popup = ogui.widget.create("Container", {
    id = "popupTest"
})

ogui.widget.append(screen, popup)

ogui.event.on("startButton", "click", function()
    ogui.screen.push("settings.xml")
end)
```

### XML declarativo com eventos e estilo

``` xml
<screen>
    <Column gap="12" padding="16">
        <Label text="Menu principal" />

        <Button
            id="startButton"
            text="Iniciar"
            class="primaryButton" />
    </Column>
</screen>
```

O formato definitivo de eventos declarados em XML deve ser padronizado
com o mecanismo de bindings já existente, evitando duas formas
incompatíveis de fazer a mesma coisa.

### Binding de dados conceitual

``` xml
<screen>
    <Label text="{player.name}" />
    <ToggleSwitch checked="{settings.fullscreen}" />
</screen>
```

O binding de exemplo representa a intenção declarativa. A sintaxe, a
observabilidade dos dados e as regras de sincronização precisam ser
especificadas antes da implementação.

------------------------------------------------------------------------

## Roadmap sugerido

### Fase 1 --- Base confiável

-   [ ] Sistema de propriedades tipadas.
-   [ ] Eventos tipados e regras de propagação.
-   [ ] Referências seguras e índice de IDs.
-   [ ] Validação de XML e estilos.
-   [ ] Testes de criação, remoção, foco, eventos e Lua.
-   [ ] Métricas básicas de desempenho.

**Resultado esperado:** uma base consistente para implementar novos
recursos sem multiplicar exceções.

### Fase 2 --- Interface dinâmica com Lua

-   [ ] Criar, localizar, anexar, remover e destruir widgets em Lua.
-   [ ] API consistente de eventos.
-   [ ] Timers `after`, `every` e `cancel`.
-   [ ] Hooks de ciclo de vida das telas.
-   [ ] Definição do isolamento de ambientes Lua.
-   [ ] Limpeza automática de listeners, timers e bindings.

**Resultado esperado:** scripts conseguem controlar a interface durante
a execução de forma segura e previsível.

### Fase 3 --- Componentes e layout declarativo

-   [ ] Templates/componentes reutilizáveis. *(adiado — o próprio plano recomenda estabilizar antes; adiado conscientemente)*
-   [ ] Parâmetros, slots e escopo local de IDs.
-   [x] Layout flexível com limites e espaçamento consistente.
-   [x] Estados visuais em estilos.
-   [x] Bindings unidirecionais.
-   [x] Transições declarativas para propriedades selecionadas.

**Resultado esperado:** interfaces mais reutilizáveis e adaptáveis, com
menos lógica repetida.

### Fase 4 --- Widgets de maior retorno

-   [ ] VirtualList.
-   [ ] PropertyGrid.
-   [x] Console/LogView.
-   [ ] TreeTable.
-   [ ] Painéis divisíveis e, se necessário, docking.
-   [ ] NumberInput, Keybind, Breadcrumb e StatusBar conforme a demanda.

**Resultado esperado:** a biblioteca atende melhor interfaces de
configuração, ferramentas e aplicações com dados extensos.

### Fase 5 --- Ferramentas e refinamento

-   [ ] Inspeção visual da árvore e das propriedades.
-   [ ] Melhorias de acessibilidade e navegação por teclado.
-   [ ] DPI e unidades lógicas.
-   [ ] Unicode, shaping e fallback de fontes.
-   [ ] Markdown/RichText/Canvas e GraphEditor conforme a necessidade.
-   [ ] Documentação completa, exemplos e benchmarks.

**Resultado esperado:** melhor experiência de desenvolvimento,
compatibilidade e capacidade de manutenção.

------------------------------------------------------------------------

## Critérios de conclusão

Uma funcionalidade não deve ser considerada concluída apenas porque o
exemplo básico funciona. Sempre que aplicável, verificar:

-   [ ] A API está documentada e segue as convenções da biblioteca.
-   [ ] Os valores e argumentos inválidos produzem erros úteis.
-   [ ] O ciclo de vida é seguro, inclusive após remoção de widgets ou
    troca de tela.
-   [ ] O comportamento por mouse, teclado e foco está definido.
-   [ ] A integração com XML, Lua e estilos é consistente.
-   [ ] Há testes automatizados para o comportamento principal e
    casos-limite.
-   [ ] O desempenho foi medido quando a funcionalidade pode afetar a
    árvore ou o desenho.
-   [ ] Existe ao menos um exemplo pequeno e reproduzível.

------------------------------------------------------------------------

## Recomendação final

Se for necessário escolher apenas três iniciativas para começar,
priorize:

1.  **Propriedades tipadas, eventos e validação** --- estabelecem
    contratos claros para toda a biblioteca.
2.  **Criação dinâmica de widgets e ciclo de vida em Lua** --- tornam a
    interface programável sem depender exclusivamente de layouts
    estáticos.
3.  **Templates reutilizáveis e layout responsivo** --- reduzem
    duplicação e melhoram a manutenção das telas.

Em seguida, use **VirtualList**, **PropertyGrid** e **Console/LogView**
como primeiros widgets avançados. Eles exercitam os fundamentos
recém-criados e são úteis em aplicações reais, além de ajudarem a
construir ferramentas de depuração para a própria OGUI.

O princípio central deste roadmap é simples: **fortalecer a arquitetura
compartilhada antes de aumentar a superfície da API**.
