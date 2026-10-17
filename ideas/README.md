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

-   [x] Sistema de propriedades tipadas: `ogui/properties.hpp` traz um
    `PropertyDef` (nome, tipo, descrição, valor padrão canônico, choices para
    enum, par read/write com `canRead()`/`canWrite()`) e um `PropertyRegistry`
    indexado por `std::type_info` com cadeia de base *declarada* — `window`
    herda de `panel` que herda de `widget`, `textarea` herda de `textinput`, e a
    declaração mais derivada de um nome vence. Tipos: `string`, `bool`,
    `number`, `color`, `enum`; a conversão é sempre por string (a forma que XML
    e Lua falam) e um valor inválido devolve mensagem do que era esperado
    (`property 'width' expects number, got 'wide'`, nome desconhecido lista os
    que existem, read-only recusa em vez de fingir). A fachada é
    `Widget::property`/`setProperty`/`propertyNames` mais as funções livres
    `widgetProperty`/`setWidgetProperty`/`widgetPropertyNames`; `ogui.get` e
    `ogui.set` caem no registro para qualquer nome que não tenham tratado, e
    `ogui.props(id)` devolve `{type, value, default, description, writable}` por
    propriedade. Widgets do host declaram a própria tabela com `declare()` e
    `numberProp`/`boolProp`/`stringProp`/`colorProp`. As 61 classes que o
    `widgetRegistry()` sabe criar estão tabeladas — 2755 propriedades entre
    elas, 2723 que lêem e escrevem, cada uma testada no round-trip de escrever a
    própria leitura de volta, e o teste ainda varre o fábrica e falha se uma tag
    chegar ao runtime sem tabela. Falha também se o parser perguntar por um nome
    que nenhuma propriedade da classe construída declara: `setAttributeTrace()`
    lembra cada `get()` feito por tag e `properties` monta um elemento por tag e
    confere o rastro contra o registro, então não existe mais atributo autorável
    silencioso (`thumbcolor`, `active`, `rowheight`) — ou a propriedade existe e
    vale para XML, `ogui.set` e a lint de valor, ou o teste quebra. Espec que o widget não guarda é write-only e a forma
    resolvida lê-se por outro nome (`columns` → `colcount`); classe que desenha
    com campos próprios sombreia a cor herdada para o nome significar o que está
    na tela (`background` em `sparkline`/`propertygrid`). O parser
    continua aplicando os atributos autorados pela sua própria cadeia tipada —
    ele também resolve geometria (`%`, `fill`, âncoras), estilos nomeados e a
    sintaxe legada `font sans 20` — e escreve nos mesmos setters, então XML e
    registro chegam ao mesmo lugar. Ficam de fora deste item: a marcação de
    quais propriedades afetam layout/desenho/estado e a definição do conjunto
    animável (`ogui.tween` ainda lista `value`/`x`/`y`/`width`/`height` à mão).
    Testes em `properties`, documentação em `docs/xml-reference.md#typed-properties`.
-   [x] Eventos tipados e regras de propagação: `EventKind`
    (`click`/`change`/`submit`/`focus`/`blur`) com um `Event` carregando `target`,
    `currentTarget`, `point`, `source`, `handled`; `Kernel::dispatchEvent` visita
    o alvo e depois cada ancestral até a raiz, chamando `Widget::onEvent` e os
    listeners Lua de cada um; `stopPropagation()` (C++) e `return false` (Lua)
    encerram a caminhada, que continua segura mesmo se um handler remover o
    widget em trânsito. Raw mouse/wheel/teclado não propaga (um único receptor,
    escolhido por hit-test/foco) e `onclick` resolve ação só no widget pressionado.
    Sem fase de captura, de propósito: o alvo já vem do hit-test, e máscaras de
    absorção usam o caminho existente de popups/`claimPoint`. Regras em
    `docs/events.md`, testes em `kernel` e `script`. Do catálogo listado para
    este item ainda não são eventos nomedados: `doubleClick`/`rightClick`
    (chegam como `Kernel::clickCount()` e o botão no `InputState`),
    `selectionChanged`, `open`/`close`, `pointerEnter`/`pointerLeave` e
    `scroll` — cada um hoje é um callback no widget que recebeu (um único
    receptor) ou um estado visual (`hover:`/`pressed:`).
-   [x] Referências seguras e índice de IDs: `shared_ptr` nas árvores,
    `Kernel::forgetWidget` e handlers que removem o próprio widget são seguros;
    `Kernel::findWidget` (e o `getText`/`ogui.get` por trás dele) agora resolve
    por um índice `id -> widget weak_ptr` da tela atual, reconstruído
    preguiçosamente uma vez a cada mudança de árvore — `addChild`,
    `removeChild`, `takeChildren`, `Widget::setId`, push/pop/switch de tela ou
    `invalidate()` explícito, que é por onde toda mutação da biblioteca passa.
    Ids repetidos ficam com o primeiro em ordem de documento (o que a varredura
    sequencial dava) e não são mais silenciosos: a mesma varredura que monta o
    índice vê a colisão, guarda `{id, first, shadowed}` e despeja uma linha por
    par no `addLog` (`duplicate id "d": Button d answers to it, Label d is
    unreachable by that name`), que um `<console>` na tela mostra de graça;
    `Kernel::duplicateIds()` devolve os ponteiros para uma ferramenta que não
    quer ler texto, `clearLogs()` esquece o que já foi dito (o host pede de
    novo), e o motivo de ser o kernel e não o parser é que um `<template>` com
    o mesmo corpo em duas instâncias só fica sem colisão depois que o escopo da
    instância é aplicado, ou seja, depois que a árvore está pronta. Um widget
    removido expira do índice e devolve `nullptr`
    em vez de um ponteiro pendurado. Os widgets passam a conhecer o kernel já
    na troca de tela, então uma árvore mutada antes do primeiro frame também
    consegue invalidar o índice. Testes em `kernel::testIdIndex`, regras em
    `docs/xml-reference.md` ("Ids and lookup").
-   [x] Validação de XML e estilos: `ogui_validate` mais os testes `validate` e
    `validate-bad` sobre todos os layouts enviados.
-   [x] Testes de criação, remoção, foco, eventos e Lua: entradas `kernel`,
    `screens` e `script` do ctest.
-   [x] Métricas básicas de desempenho: `Kernel::FrameStats`
    (`updateMs`/`drawMs`, widgets, vértices, índices, cmds, paradas de foco)
    exposto a `ogui.stats`.

**Resultado esperado:** uma base consistente para implementar novos
recursos sem multiplicar exceções.

### Fase 2 --- Interface dinâmica com Lua

-   [x] Criar, localizar, anexar, remover e destruir widgets em Lua:
    `ogui.add`/`ogui.remove`/`ogui.clear` com `Kernel::forgetWidget` para o
    handler que destrói o próprio widget pressionado.
-   [x] API consistente de eventos: `ogui.on`/`ogui.off` para click, focus e
    blur, hooks `ogui_on_*` e eventos de change/submit vindos da interação do
    usuário.
-   [x] Timers `after`, `every` e `cancel` avançados pelo relógio de frames do
    `Kernel`.
-   [x] Hooks de ciclo de vida das telas: `ogui_on_show`/`_hide`/`_unload` em
    torno das trocas de tela.
-   [x] Definição do isolamento de ambientes Lua: um `lua_State` por `Kernel`,
    reconstruído quando a tela em primeiro plano muda, com o estado do script
    anterior descartado junto (`ScriptHost::clear`).
-   [x] Limpeza automática de listeners, timers e bindings: ao remover o widget
    e ao trocar de tela.

**Resultado esperado:** scripts conseguem controlar a interface durante
a execução de forma segura e previsível.

### Fase 3 --- Componentes e layout declarativo

-   [x] Templates/componentes reutilizáveis. `<template name="Card">` declara o
    componente e registra o nome como elemento (`src/xmlparser.cpp`): parâmetros
    com valor padrão, `${param}` no corpo, `<slot/>` (com conteúdo de reserva),
    composição de componentes até 16 níveis e um catálogo global, compartilhado
    por um `<theme>` carregado via `<stylesheet path>`. Os erros nomeiam o
    componente que o autor escreveu (`while expanding <Card>: ...`), um
    `<script>` do corpo vale por instância e `docs/xml-reference.md` tem a
    secção *Templates (components)*.
-   [x] Parâmetros, slots e escopo local de IDs. Cada instância é o seu próprio
    escopo: os IDs do corpo ganham o prefixo da instância (`cpu.bar`) e os
    escopos aninham-se na componente que os contém (`stats.Gauge#2.bar`), pelo
    que duas host da mesma componente não colidem; uma instância sem `id`
    recebe `<Nome>#<n>`; atributos e estilos não consumidos como parâmetros
    caem sobre a raiz gerada, e a raiz é a fronteira dos eventos do componente.
-   [x] Layout flexível com limites e espaçamento consistente.
-   [x] Estados visuais em estilos.
-   [x] Bindings unidirecionais.
-   [x] Transições declarativas para propriedades selecionadas.

**Resultado esperado:** interfaces mais reutilizáveis e adaptáveis, com
menos lógica repetida.

### Fase 4 --- Widgets de maior retorno

-   [x] VirtualList.
-   [x] PropertyGrid.
-   [x] Console/LogView.
-   [x] TreeTable.
-   [x] Painéis divisíveis (`splitter`, aninhável); docking completo fica para
    depois, sem demanda concreta ainda.
-   [x] NumberInput (`spinbox` já cobre), Keybind, Breadcrumb e StatusBar.
-   [x] CommandPalette (busca incremental, histórico e `ogui.cmd`); um
    terminal completo ainda não tem demanda.

**Resultado esperado:** a biblioteca atende melhor interfaces de
configuração, ferramentas e aplicações com dados extensos.

### Fase 5 --- Ferramentas e refinamento

-   [x] Inspeção visual da árvore e das propriedades: `Kernel::dumpTree()`,
    `tabOrder()`, `typeName()`/`describe()` (RTTI + demangler, sem overrides por
    classe), `ogui.dump()`/`ogui.inspect(id)`; sobreposição desenhada no próprio
    `DrawList` (`DebugBounds`, `DebugLabels`, `DebugFocus`, `DebugHover`,
    `kernel.setDebugOverlay`, `ogui.debug`); `Kernel::FrameStats` com tempo de
    update/desenho, widgets, paradas de Tab e tamanho do draw list via
    `ogui.stats()`; `setDebugEvents()` registra clique, foco e binding no log
    que o `console` já espelha; `ogui_screenshot --debug` renderiza tudo
    headless, e `ogui_validate` (de `examples/validate.cpp`) valida arquivos ou
    um `manifest.xml` na linha de comando, com `line:col` e código de saída.
    Falta o diagnóstico de layout/clipping (por que tais bounds, qual clip
    cortou).
-   [x] Tab/Shift+Tab com ordem de foco previsível (document order, com
    wrap), `focusable`, `accessible`/`aria-label`, anel de foco via estados
    visuais `focus:`, Enter/Espaço ativando botão pelo mesmo caminho do
    clique, `ogui.focus("next"/"prev"/id)`. Quem é parada sem ninguém pedir é a
    classe que responde (`Widget::focusableByDefault`, overridden pelas 27
    classes interativas), não a fábrica de tags — um `Button` criado em C++ entra
    no anel igual ao `<button>`, e o atributo só sobrescreve nos dois sentidos,
    com o vocabulário bool do registro (`yes`/`on`/`off` e não só `true`/`1`),
    que é o que `debug/tests/kernel_test.cpp::testFocusableDefault` fixa.
    Limitação de plataforma
    (sem UIA/ATK) documentada no README.
-   [x] DPI e unidades lógicas: tudo o que o widget vê (XML, `bounds()`,
    ponteiro, fontes) é unidade lógica; `Screen::width/height` ficam em pixels
    de dispositivo e `Screen::scale` converte. A multiplicação acontece uma
    única vez na emissão do `Rasterizer`, e `FontManager::setScale()` grava o
    atlas em `size * scale` mantendo métricas lógicas. `Kernel::scale()`,
    `setScale()`, `logicalWidth()`, `logicalHeight()`, `Host` lendo o dpi da
    janela, testes em 1×/1.5×/2× e `ogui_screenshot --scale`.
-   [x] Unicode e fallback de fontes: UTF-8 do parser ao rasterizador, com
    glifos gravados sob demanda em células do atlas reservadas na carga e que
    nunca se movem, `atlasRevision()` avisando o backend para reenviar o atlas, e
    uma cadeia de famílias (`sans, serif, mono`, extensível com
    `FontManager::addFallback()`) cobrindo o que a família pedida não tem.
    Código ponto sem cobertura em nenhuma face desenha uma caixa `.notdef`
    compartilhada na largura `em`; marcas combinantes avançam zero e se
    centram sobre a base; caret/backspace e `Font::indexFromX` operam por
    cluster; `layoutText` quebra em CJK/Hangul/kana e puxa pontuação de
    fechamento para a linha. Testes em `debug/tests/unicode_test.cpp`
    (gravura sob demanda, fallback serif→sans, `.notdef`, clusters, quebra
    CJK, tinta cirílica no `SoftwareRenderer`) e a avaliação de dependências —
    por que HarfBuzz ainda não entrou — em `docs/unicode.md`. Faltam shaping
    de verdade (GSUB/GPOS, ligaturas, âncoras de acento), bidi e cobertura
    não-BMP, todos documentados como decisão consciente.
-   [x] Markdown/RichText/Canvas e GraphEditor --- adiados sem data, decisão
    final do roadmap. O terreno que cobririam já tem resposta na biblioteca:
    uma superfície de desenho é um `Widget` registrado em `widgetRegistry()`
    recebendo o `Rasterizer&` em `onDraw` (`examples/custom_widget.cpp` é o
    caminho completo e testado, e é exatamente assim que um editor de grafo
    entraria), séries pequenas cabem em `sparkline`, um log em `console`, e a
    repetição de peças de layout deixou de precisar de widget novo quando
    `<template>` trouxe parâmetros, slots e ids locais. RichText/Markdown fica
    atrás do shaping que ainda não existe (GSUB/GPOS, ligaturas, bidi —
    `docs/unicode.md`): estilo por trecho seria escrito sobre a medição e a
    quebra de linha que ainda vão mudar, que é a dependência que o próprio
    plano mandava estabilizar antes.
-   [x] Documentação completa e exemplos: `docs/xml-reference.md` cataloga cada
    elemento, atributo, tipo, valor padrão e regra de validação;
    `docs/examples.md` traz o layout mínimo, um contador com eventos, timers,
    bindings e ciclo de vida, a ordem de carregamento e os escopos de script, o
    registro de um widget C++ personalizado (`examples/custom_widget.cpp`) e o
    erro típico. `examples/hello.xml` e `examples/counter.xml` rodam num
    `Kernel` real e o teste `examples` extrai e executa cada bloco XML/Lua da
    documentação, então a doc não apodrece em silêncio. `ogui_validate` valida
    os layouts enviados. Faltam benchmarks.

**Resultado esperado:** melhor experiência de desenvolvimento,
compatibilidade e capacidade de manutenção.

------------------------------------------------------------------------

### Fase 6 --- Portabilidade (GLES/EGL e Android)

-   [x] `GlBackend` fala OpenGL ES 3.0 além de 3.3 core. `init(getProc,
    profile)` aceita `Profile::Auto` (padrão), `DesktopGL` ou `OpenGLES`, e
    `Auto` lê `GL_VERSION`/`GL_SHADING_LANGUAGE_VERSION` do contexto já
    corrente, então o mesmo binário serve os dois. O custo real do port são
    três pontos, todos dentro do backend: um corpo de shader com dois
    cabeçalhos (`#version 300 es` + `precision highp float;` no fragmento,
    `#version 330 core` no desktop), `GL_RGBA8` como internalformat tipado que o
    ES exige no lugar de `GL_RGBA`, e o swizzle do atlas de glifos feito por
    componente (`GL_TEXTURE_SWIZZLE_R/G/B/A`) porque o `GL_TEXTURE_SWIZZLE_RGBA`
    agregado só existe no desktop. O resto do que o backend usa já era core do
    ES 3.0. A biblioteca continua sem linkar GL: a tabela `Api` é resolvida
    inteira pelo `getProc` do hospedeiro, o ramo `__ANDROID__` do
    `defaultGetProc` procura `libGLESv2.so` e depois `eglGetProcAddress` e nunca
    toca `libGL`, e `glBlendFunc` saiu da lista obrigatória por não ser mais
    usado.
-   [x] Alpha composto como source-over de verdade: `glBlendFunc` aplica
    `SRC_ALPHA` ao canal alpha também, o que dá `αs² + αd(1−αs)` em vez de
    `αs + αd(1−αs)`. Passou a `glBlendFuncSeparate(..., GL_ONE,
    GL_ONE_MINUS_SRC_ALPHA)`. O `SoftwareRenderer` já seguia a definição, e foi
    a primeira comparação real entre os dois que mostrou a diferença.
-   [x] Teste de pixels em GL de verdade (`debug/tests/gl_test.cpp`): abre dois
    contextos EGL headless na mesma máquina, um ES com
    `EGL_CONTEXT_CLIENT_VERSION=3` e um desktop, desenha a cena do `golden` em
    ambos e compara com o `SoftwareRenderer`; verifica a detecção
    de perfil, que `render()` devolve os 19 pedaços de estado que tocou e que um
    segundo frame é bit-idêntico. O layout da cena saiu de `golden_test.cpp`
    para `debug/tests/golden_scene.hpp`, compartilhado pelos dois. A asserção de
    pixels tolera raio de 1 pixel, porque o rasterizador CPU trata toda aresta
    de triângulo como coberta e o GL aplica a regra do canto superior
    esquerdo --- é a diferença de cobertura de borda, não de cor nem de lugar.
    O executável do teste só é construído quando EGL e `libGLESv2` existem, e
    **pula** (não falha) onde não há contexto possível. A correção de
    arredondamento do `SoftwareRenderer`, cujo blend truncava em vez de
    arredondar para o mais próximo, saiu do mesmo teste.
-   [x] Hospedeiro Android: `NativeActivity` com superfície EGL real
    (`eglCreateWindowSurface` sobre o `ANativeWindow`, no lugar do pbuffer que o
    teste usa). O `AndroidHost` cria a superfície, o contexto ES 3 e o
    `GlBackend` no primeiro `APP_CMD_INIT_WINDOW` --- um config só pode
    concordar com o formato de pixels da janela, e uma `NativeActivity` não tem
    janela nenhuma quando nasce, então config e contexto são escolhidos com a
    primeira janela e sobrevivem ao `releaseWindow()`: os atlas de fonte na GPU
    atravessam pausa, retomada e rotação, e o `configChanges` do manifest é o
    que deixa a rotação seguir esse caminho em vez de demolir o processo. A
    outra metade da separação é a entrada: as *regras* --- qual dedo a árvore
    vê, que um levantar remove por id e não por índice, que `ACTION_CANCEL` não
    é um release, o que cada `AKeyEvent` significa e que caractere ele digita
    --- moram em `AndroidInputBridge`, sobre dados planos, e são testadas no
    host (`debug/tests/android_test.cpp`); o `onInputEvent()` do hospedeiro é só
    quem chama os getters do `AMotionEvent`, que só existem num telefone. Os
    layouts entram por `AAsset` pela costura de assets, a densidade vira
    `dpiScale()`, `/system/fonts` entra na busca de fontes, e
    `./scripts/build.sh android` produz um APK assinado com a chave de debug e
    verificado por `apksigner`: sem Gradle --- mas com Java, quatro classes sob
    `android/src/dev/ogui/android`, um `javac` contra o `android.jar` da API 36
    e um `d8` que delas faz `classes.dex`, com `android:hasCode="true"` e a
    activity apontando para `dev.ogui.android.OguiActivity` ---, a
    `liboggui_app.so` armazenada sem comprimir porque o carregador a faz
    `mmap` direto do arquivo, e uma etapa de `strip` antes de empacotar porque a
    biblioteca sem desbastar é dez vezes o que o vinculador precisa. **Foi
    instalado num aparelho** — um SM-A155M, API 36, 1080x2340 a 450dpi — e o
    `screencap` mostrou as três coisas que nenhum teste de host alcança: a
    rotação, porque o `configChanges` entrega `WINDOW_RESIZED` na *mesma*
    `ANativeWindow` e na mesma `EGLSurface`, sem `TERM_WINDOW` nem
    `INIT_WINDOW`, de modo que `setWindow()` nunca roda de novo e a geometria
    velha fica em cache — um layout retrato girado para pais desenha-se inteiro
    acima do topo do quadro: preto, a sessenta quadros por segundo;
    `refreshWindowSize()` relê `ANativeWindow_getWidth/Height`, que nesse
    instante já são os novos enquanto `eglQuerySurface` ainda responde os
    velhos. A densidade, porque os `ACONFIGURATION_DENSITY_*` do NDK *são*
    números de dpi (120/240/320/480/640) e o aparelho reporta 450, um deles sem
    nome no header: um `switch` sobre os sete baldes respondia `1.0` e desenhava
    a árvore inteira em mdpi num écran três vezes mais denso. E o empacotamento
    dos assets, porque `aapt2 -A dir` nomeia as entradas sem o prefixo `assets/`
    mas os nomes que os layouts autoram têm-no — `source="assets/checker.png"`
    pede `assets/assets/checker.png` no arquivo, e sem isso eram duas caixas em
    branco. A conexão de entrada veio depois, e é o item abaixo: o teclado
    aparece e escreve no campo.
-   [x] Toque no `InputState`: a lista de dedos entra pelo estado
    (`touches[]` com o id do `AMotionEvent` e a posição em pixels de
    dispositivo, `touchCount`, `touchCancelled`) e o `Kernel` a dobra no único
    ponteiro que a árvore já fala (`applyTouchStream`), então nenhum widget
    precisa saber o que é um dedo. O dedo que começa um arrasto fica com ele
    --- um segundo dedo não rouba a captura, e se o dono levantar com outro
    ainda embaixo o mesmo arrasto continua --- o levantar do último dedo libera
    o widget no lugar certo (o toque vira clique) e `touchCancelled` devolve a
    captura sem clicar. Sem dedo na tela o ponteiro sai da árvore, porque um
    estado `hover:` pintado no último item tocado é a pior marca que um toque
    pode deixar. Numa máquina com os dois, o mouse recupera o ponteiro no
    primeiro movimento dele. Testes em `debug/tests/touch_test.cpp`.
-   [x] Pan e fling: um dedo que passa do *slop* --- dez unidades lógicas ---
    deixa de ser um toque e vira um arrasto do conteúdo. O `Kernel` pergunta ao
    widget pressionado e depois a cada ancestral (`wantsPan`), e o primeiro que
    tem um `scrollUnit()` não-zero fica com o arrasto: quem estava com a
    captura é liberado sem clicar, e cada quadro entrega o deslocamento em
    *pixels do dedo* ao `onPointerScroll` daquele widget. Uma rolagem só, duas
    origens de entrada, nenhum código novo por widget. Levantar o dedo no meio
    do gesto vira a velocidade medida nos últimos 0,1 s, `advanceFling` a decai
    até parar, e um dedo que volta para a rolagem em andamento no próprio
    quadro em que pousa. Pergunta e não sinal é o ponto: o polegar de uma barra
    de rolagem, ou uma seleção de texto sendo arrastada, são arrastos que não
    podem virar rolagem --- por isso um campo de texto continua selecionando
    sob o dedo, e quem fica com o arrasto é o ancestral rolável. Mouse não é
    dedo: um arrasto dele continua sendo seleção, slider, thumb. Testes em
    `debug/tests/touch_test.cpp`.
-   [x] Pressionar-e-segurar: meio segundo dentro do mesmo *slop* e o dedo deixa
    de ser um toque esperando uma soltura. `updateLongPress` arma a espera só
    quando o ponteiro veio de `touches` --- um botão do mouse segurado é o
    primeiro quadro de uma seleção, e o botão direito já é o menu de contexto do
    desktop --- e ao disparar entrega ao widget pressionado a liberação sem
    clique, abre o `contextmenu` do ancestral mais próximo no ponto exato do dedo
    e só então roteia o evento `longPress`, que sobe a árvore como qualquer outro.
    Um dedo que anda além do *slop* é um arrasto, e um gesto não repete enquanto
    o dedo fica: a armação é por toque, não por quadro. Testes em
    `debug/tests/touch_test.cpp`.
-   [x] Pinça: dois dedos são um gesto, não dois ponteiros. A distância entre eles
    é medida a cada quadro e a razão entre dois quadros vai ao widget que reivindicar
    o par --- a mesma ordem do *pan*: o pressionado e depois cada ancestral,
    `wantsPinch` --- que toma a captura para si (liberada, sem clique) e segura os
    dedos longe do arrasto enquanto a pinça durar. `factor` é a distância deste
    quadro dividida pela do anterior, então quem recebe multiplica o que já tem e
    não guarda memória de gesto nenhum; `pivot` é o ponto entre os dedos em
    unidades lógicas. Quem consome hoje é `<image pinchzoom="true">`: o *zoom*
    (1..8) é uma janela de UV sobre a mesma textura e o destino continua sendo o
    *bounds*, então ampliar não recorta nem transforma nada --- e o ponto sob os
    dedos fica onde estava, que é o que faz a pinça parecer segurar a figura e não
    redimensionar a moldura. Rotação continua aberta de propósito: nada na
    biblioteca gira uma subárvore, então não haveria a quem entregar um ângulo.
    Testes em `debug/tests/touch_test.cpp`.
-   [x] Leitura de assets por uma costura só: nada dentro da biblioteca chama
    `fopen` --- layout (`parseFile`, `parseThemeFile`), script
    (`ScriptHost::loadFile`), fonte e imagem passam por `ogui::readFile()`,
    que pergunta primeiro ao `AssetReader` instalado pelo host e depois ao
    sistema de arquivos. Um host Android aponta o leitor para o `AAssetManager`
    e embarca o layout no APK; um host que não instala nada lê os mesmos
    arquivos de sempre. `nullopt` significa "esse nome não é meu" (cai no
    disco) e é diferente de um vetor vazio, que é "existe e está vazio" --- um
    layout ausente e um corrompido são erros distintos a reportar. O caminho
    explícito do `FontManager` e o *probe* dos diretórios de sistema também
    aceitam um nome do leitor, então uma fonte registrada por nome de asset
    funciona. Ficam no sistema de arquivos, de propósito e por enquanto: a
    biblioteca `io` do Lua (lê o que o script quiser, não é assunto do
    toolkit) e os navegadores de diretório (`<pathpicker>`, o preenchimento
    lazy do `treeview`), que listam pastas em vez de ler um nome. Testes em
    `debug/tests/asset_test.cpp`.
-   [x] Entrada de texto pelo IME: `InputState::lastChar` chega de eventos de
    teclado, e o NDK r30 não expõe cabeçalho de `AInputConnection` nenhum --- na
    fonte (lida aqui, `NativeActivity.java` do AOSP) a classe não declara
    `onCreateInputConnection` nem `onStartInput`/`onFinishInput`: ela só pega a
    `InputMethodManager` e chama `showSoftInput(mNativeContentView, ...)`, sendo
    `mNativeContentView` um `View` comum que `requestFocus()` no `onCreate`. A
    conexão que o IME recebe era portanto a vazia de um `View` qualquer, e o
    aparelho mostrava isso: `inputType=0, isTextEditor=false`. O que fecha isso
    é meio `View` nosso e uma `InputConnection` nossa, ambos em Java, porque só
    o quadro de View é quem fala com o teclado: `OguiActivity` estende a
    `NativeActivity`, adiciona um `OguiEditorView` por `addContentView()` (nunca
    `setContentView()`, que o `onCreate` da `NativeActivity` já usou para a
    surface dela), `requestFocus()` nele, e `OguiEditorView` devolve um
    `OguiInputConnection` com `inputType`/`imeOptions` do `fieldName` que a
    ponte publica. Cada método da conexão vira um `ImeEdit` --- inserir, apagar
    antes/depois do caret, substituir um *span* de composição, um trecho que o
    IME reivindica, um `performEditorAction`, uma tecla --- e `applyEdits()` os
    drena na ordem, no thread do quadro, contra o texto que o edit anterior
    deixou. As duas metades que não dependiam de aparelho ficaram: os ints da
    `InputMethodManager` conferidos contra `javap -constants` do `android.jar`
    de API 36, e a contagem UTF-16 do IME contra os bytes UTF-8 do caret, em
    `imeAdvanceUnits()`/`imeRewindUnits()`/`imeCountUnits()`, que param antes de
    um par quando pedem uma unidade --- exceção da deleção, que tem de cair em
    algum lugar: um apagar que não andou pede as duas unidades e toma o código
    inteiro, porque duas é o máximo que um caractere ocupa. Três coisas só o
    aparelho ensinou. A primeira é que `ExtractedText` da API 36 não tem campo
    `token`, e referenciá-lo é o `javac` recusando. A segunda: o
    backspace do Gboard não é um delete nem um `sendKeyEvent` que a ponte
    conhecesse --- é um par `down`+`up` do mesmo instante dentro de uma única
    drenagem, que deixava o quadro com `keyDown` armado e o soltava só no
    release seguinte; então uma
    pressão pertence ao quadro em que chegou, e um release que chega nesse
    quadro espera `endFrame()` para abaixar o nível, quem quer que tenha
    pressionado. A terceira é que a carga nativa estava errada desde o começo: os
    registros dependiam de `JNI_OnLoad`, porque
    `NativeActivity` dá `dlopen()` na biblioteca para achar `android_main` e um
    `dlopen()` de código nativo nunca chama `JNI_OnLoad` --- é o bloco
    `static { System.loadLibrary("oggui_app"); }` do `OguiJni` que faz a VM
    registrá-lo, sem o qual a primeira chamada nativa é um `UnsatisfiedLinkError`
    fatal. O `dumpsys input_method` do SM-A155M hoje responde
    `mServedView=dev.ogui.android.OguiEditorView`, `inputType=0x1`,
    `imeOptions=0x6`, `fieldName=ogui`, e um "o" digitado no campo chega como
    `text=o bytes=1` pela consulta do próprio IME; `syncIme()` diffa o snapshot
    a cada quadro. O que a conexão não atende é recusa registrada: clipboard e
    `performContextMenuAction`, que não têm para o que voltar. Testes em
    `debug/tests/android_test.cpp`.
-   [x] Área segura (insets): uma janela de telefone não é a janela inteira ---
    barra de status, recorte e teclado cada um come uma faixa, e uma layout que
    não sabe disso desenha botão debaixo da barra e campo debaixo do teclado. O
    `Kernel` guarda isso como `Insets` e `safeRect()`, no modelo em que o widget
    mantém o *bounds autoral relativo* à área segura em vez de ter o bounds
    reescrito: `update()` converte o ponteiro de pixels do aparelho para lógico e
    subtrai a origem segura uma vez só, `buildDrawList()` entrega a origem ao
    `drawTree` para somar de volta, e como os dois passes deitam a árvore a cada
    quadro, um deslocamento persistente somaria duas vezes e uma conversão no
    ponteiro não. `OguiActivity` pede a janela inteira
    (`setDecorFitsSystemWindows(false)` na API 30+, os flags legados
    `LAYOUT_STABLE | LAYOUT_FULLSCREEN | LAYOUT_HIDE_NAVIGATION` abaixo dela) para
    o quadro parar de consumir as barras e entregar os números; o
    `onApplyWindowInsets` lê `systemBars() | displayCutout()` e `ime()`, e o
    teclado *substitui* a barra de navegação, então o fundo é `max(bars, ime)` e
    não a soma. O que se reporta é o `getRootWindowInsets()` e não o argumento que
    o view recebeu, porque um filho do content recebe o que os pais resolveram
    passar: no M2101K7AI (MIUI, API 31) o decor é despachado com `top=93` e todo
    filho abaixo dele com `top=0` --- o decor consumiu a barra por ter encaixado o
    *próprio* content view debaixo dela, enquanto a superfície de um
    `NativeActivity` continua sendo a janela inteira (1080x2400 para o native,
    1080x2246 em y=154 para a árvore de views), de modo que o app desenha debaixo
    do relógio e é informado de que não há nada a evitar. A raiz guarda o número
    antes de alguém consumi-lo, certo naquele aparelho e no Samsung, onde os dois
    concordam; o argumento só vale enquanto a raiz é nula, isto é, com o view
    destacado, que nunca é despachado. Os valores chegam em pixels do aparelho, no
    thread da UI, e ficam como último valor, não fila: `takeWindowInsets` só
    reporta uma barra uma vez e
    só quando ela mudou, `AndroidHost::applyWindowInsets` divide pela escala e
    `setInsets` invalida quando algo se moveu, não sessenta vezes por segundo
    enquanto nada se move. A conversão e a regra de drenagem vivem fora do
    `#ifdef __ANDROID__` em `androidhost.cpp`, portanto testadas no host e não só
    no aparelho. Testes em `debug/tests/kernel_test.cpp` (o retângulo, a lista de
    desenho, o hover no ponto convertido, o clamp do toast, o overrun além da
    janela) e `debug/tests/android_test.cpp` (o último-valor, o só-uma-vez, o
    negativo lido como zero); controles negativos mostram cada regra derrubando
    exatamente a asserção escrita para ela. A regra da raiz é Java e não tem teste
    de host: o que ela provou ser foi no aparelho, com `ins=[0,69,0,96]` chegando
    ao `Kernel` e a primeira linha da galeria parando debaixo do relógio em vez de
    embaixo da barra.
-   [x] A galeria cabe no telefone: as telas são autoradas para um canvas fixo de
    800 unidades lógicas, e na densidade do aparelho 1080 px são só 384 lógicos,
    então a borda direita de cada linha caía fora da tela. Em vez de remanejar um
    design que não foi feito para telefone, `fit()` dá zoom:
    `fitToWidthScale(deviceWidthPx, designWidth, dpiScale)` devolve a escala que
    mapeia a largura do design sobre a da tela --- um zoom-out num telefone
    estreito --- mas nunca acima de `dpiScale`, para que uma tela já larga o
    bastante na própria densidade mantenha o tamanho físico e não seja ampliada. A
    largura lógica termina no width do design, então nada corta na direita; o canvas
    alto que sobra abaixo do design (ancorado no topo) é de onde a área segura
    desconta as barras. O custo é uma caixa de altura `%` esticada no canvas mais
    alto --- um layout de desktop num telefone 19.5:9 tem mais sala do que foi
    desenhado --- troca menor contra conteúdo que simplesmente não aparecia. A
    regra é aritmética e está em `debug/tests/android_test.cpp` (zoom-out no
    estreito, densidade no largo, guarda de width zero, e a largura lógica saindo
    no design com a barra de status ainda virando unidades do design); no aparelho
    as telas Basico, Containers e Game agora ocupam a largura inteira sem corte.
-   [x] Contêineres de rolagem por eixo: `scrollableVerticalContainer` e
    `scrollableHorizontalContainer`. Um `ScrollView` é uma *página*: cada filho
    fica onde a layout autorou e o que passa do viewport rola no eixo que
    transbordar --- certo para uma página grande, errado para uma lista de coisas
    que se empilham sozinhas, onde o autor teria de calcular `y` de cada filho e
    mesmo assim a roda moveria os dois eixos. As duas classes novas são um
    `ScrollView` que deita os próprios filhos (empilhados para baixo, como
    `column`, ou ladrilhados para a direita, como `row`, com o `gap` do estilo) e
    só rola essa linha. Elas não copiam a lógica: `ScrollView` ganhou dois modos
    --- `Flow` e `Bar` --- cujo default é exatamente o comportamento de antes, e
    as duas classes só respondem `setFlow()`/`setBar()` no construtor, três linhas
    cada. O eixo travado mora em `clampScroll()`, para onde todo caminho que move
    conteúdo já ia --- roda, arrasto do dedo, inércia (os três por
    `onPointerScroll()`, que agora não sabe nada de eixo), `setScroll()` de um
    script, um `scrollY=` autorado e o arrasto da barra: uma trava, um lugar, e
    nenhum jeito de passar por fora. A barra acompanha o eixo: borda direita na
    vertical, borda inferior na horizontal, sempre testada em espaço local (o
    bloco de origem ≠ 0) e desenhada em absoluto --- e uma barra cujo trilho é
    mais curto que o polegar mínimo (uma caixa de 12px) enche o trilho em vez de
    estourar o `std::clamp` com `lo > hi`, e um arrasto nela não faz nada em vez
    de teletransportar o conteúdo para o começo. Um `<scrollview>` não mudou nada
    --- o golden e o suite de scrollbar passaram sem tocar em um único pixel
    esperado. Testes em `debug/tests/scrollbar_test.cpp` (empilhamento, travamento
    por cada caminho, arrasto da barra em cada borda, uma pressão fora da barra
    que não rola, a caixa pequena que não quebra, e as duas tags parseando e
    herdando `scrollStep`/`gap`) e a varredura de round-trip de `properties_test`,
    que agora declara as duas classes e lê as 40 propriedades do `ScrollView`
    nelas. No aparelho o arrasto de dedo fechou o ciclo: um swipe diagonal sobre
    `shc_demo` (400 px na horizontal, 29 px para cima) moveu a linha de cartões e
    o polegar da barra inferior, e a borda de cima da linha continuou em
    y=2118 --- a mesma coordenada antes, depois e no swipe de volta ---, que é a
    trava do eixo cruzado vista em pixels reais em vez de num `InputState`
    montado. A vertical não tinha o que testar no aparelho: com a altura em `%`
    esticada pelo canvas alto do telefone, as oito linhas cabem no viewport e não
    transbordam, então o empilhamento para baixo foi provado num render do host
    --- doze cartões de 60 num viewport de 200, arrastados 160 unidades para a
    esquerda e 100 para cima --- onde as bordas dos cartões continuam em x=10 e
    x=190 exatamente como antes do gesto, com banda deslocada 49,6 para cima
    (o resto do scroll virou a barra que apareceu na borda direita).

**Resultado esperado:** o mesmo `DrawList` renderiza em desktop GL, GLES 3.0 e
CPU, com a portabilidade coberta por teste em vez de suposição.

------------------------------------------------------------------------

## Critérios de conclusão

Uma funcionalidade não deve ser considerada concluída apenas porque o
exemplo básico funciona. Sempre que aplicável, verificar:

-   [x] A API está documentada e segue as convenções da biblioteca. (Cada tag e
    cada propriedade tem linha em `docs/xml-reference.md`, a tabela de widgets do
    `README.md` espelha o `PropertyRegistry` --- é o mesmo dado que a varredura de
    round-trip lê ---, e as regras que não cabem numa tabela de atributos estão
    comentadas no header onde moram: o contrato de remoção em `widget.hpp`, a
    escala lógica/aparelho em `kernel.hpp`, o fluxo de eventos em
    `docs/events.md`.)
-   [x] Os valores e argumentos inválidos produzem erros úteis. (O
    `XmlParser` agora passa cada atributo autoral pelo `PropertyRegistry` —
    `checkWidgetProperty()` olha o tipo declarado e as choices sem aplicar nada
    — e um valor que não cabe no nome vira erro com linha, coluna e o que a
    propriedade esperava: `height="tall"`, `visible="ture"`, `align="middle"`,
    `@token` inexistente, um `estado:` que não é dos sete. Antes cada setter
    engolia o que não parseava (`height` virava 0, `@nope` desenhava `window`).
    O CLI do `ogui_validate` também passou a reclamar de `--chdir` sem
    argumento e de opção inexistente, em vez de validar isso como nome de
    arquivo. Coberto por `parser_test`/`properties_test` e pelo bloco
    expect-error de `docs/examples.md`. E nenhum nome autorado fica silencioso:
    `XmlParser::setAttributeTrace()` grava cada `get()` que `applyAttributes()`
    faz por tag, e `properties_test::testNameDrift()` monta um elemento por tag
    do `widgetRegistry()` e exige que o nome perguntado seja propriedade
    declarada da classe que o próprio parser construiu — um `get()` novo sem
    propriedade correspondente quebra o teste em vez de alargar o buraco entre
    o que uma layout escreve e o que um script lê.)
-   [x] O ciclo de vida é seguro, inclusive após remoção de widgets ou
    troca de tela. (`debug/tests/lifetime_test.cpp`, coberto pelo ASAN:
    `forgetWidget` desce a árvore e limpa o tooltip, a troca de tela solta o
    popup e o foco *antes* de liberar a tela que sai, o callback de popup
    captura `weak_from_this()` e o snapshot de animação do `treeview` segura as
    linhas fracamente. O buraco que faltava está fechado: `Widget::removeChild()`
    e `Widget::takeChildren()` chamam `forgetWidget()` *antes* do `erase`, porque
    apagar o último `shared_ptr` pode liberar o widget ali mesmo e não existe
    depois do quê um host pudesse avisar --- o identificador que ele nomearia é a
    memória que acabou de ir. É a mesma promessa que `ogui.remove()` já fazia em
    Lua, agora no caminho C++ onde `widgets_list.cpp` reconstrói linhas e
    `ScrollView::setContent()` troca o conteúdo. Dois testes novos
    (`testRemoveChildNeedsNoHelp`, `testTakeChildrenNeedsNoHelp`) removem um
    painel que segura foco e tooltip sem chamar nada além do `removeChild`, e um
    controle negativo que tira as duas chamadas derruba exatamente as três
    asserções novas --- e só elas, em toda a suíte.)
-   [x] O comportamento por mouse, teclado e foco está definido. (Definido e
    testado quadro a quadro: `kernel_test` cobre despacho, foco/captura, edição de
    texto, abas, tooltip e a propagação que `stopPropagation` corta; `touch_test`
    cobre o dedo --- o tap que é clique, o arrasto que fica com o dedo que
    começou, o slop antes do pan, quem reclama o gesto, o flick que continua e
    para, o segundo dedo que não rouba a pressão; `scrollbar_test` cobre a barra
    que rola em vez de selecionar. O que cada caminho significa está escrito em
    `docs/events.md`.)
-   [x] A integração com XML, Lua e estilos é consistente. (É a mesma tabela dos
    dois lados: `properties_test` escreve e relê as 2531 propriedades que a
    varredura percorre, `testNameDrift()` exige que todo nome que `applyAttributes()`
    pergunta seja propriedade declarada da classe que o parser construiu, e
    `script_test`/`templates_test`/`examples_test` rodam layout, `<script>`,
    `@token` de tema e `<template>` juntos --- inclusive o `ogui.get`/`ogui.set`
    caindo no registry quando o widget não tem hook próprio.)
-   [x] Há testes automatizados para o comportamento principal e
    casos-limite. (17 binários `ogui_*_test`, cada um no `ctest` e rodando duas
    vezes --- uma normal, uma sob AddressSanitizer, porque metade dos casos-limite
    é ponteiro que sobrevive ao dono. Toda regra tem controle negativo: quebrá-la
    derruba exatamente a asserção escrita para ela, e o `bench` só aceita
    proporções, para o verde não depender da máquina.)
-   [x] O desempenho foi medido quando a funcionalidade pode afetar a
    árvore ou o desenho. (`debug/tests/bench.cpp`, o `ogui_bench_test`: uma caixa
    de N painéis no canvas do telefone --- 800×1600 lógicos --- arrastada por um
    dedo sintético, e a tela real da galeria. Os números absolutos são da máquina
    onde rodou e ficam impressos; o que a suíte exige são proporções, para valer
    também numa dez vezes mais lenta. Com 800 cartões: página `scrollview`
    414 µs/quadro, container vertical 423, horizontal 423 --- deitar os próprios
    filhos custa ~2% a mais que a página que a classe substitui, não uma classe
    nova de custo. A linha crescendo: 200 cartões 138 µs, 800 423, 3200 1860, ou
    seja 16× os filhos por 13,5× o tempo (linear; quadrático seriam 256×). O
    desenho não acompanha a linha: 154 vértices com 200 cartões e 154 com 3200,
    porque o que não entra no clip é descartado no rasterizador, e um pan de 120
    quadros muda isso para 158 --- a geometria do viewport, não a da fila
    inteira. Um quadro que não muda nada reaproveita a lista: 0,0003 ms contra
    0,29 ms depois de um `invalidate()`. A tela `Containers` inteira, com janelas,
    abas, splitter e os dois contêineres, custa 13 µs/quadro ociosa, 54 widgets,
    7470 vértices. Três controles negativos, cada um derrubando exatamente a
    asserção da regra que ele quebra: tirar o cache de redraw (`if (!m_dirty)`
    nunca mais) derruba a do quadro ocioso; descartar por clip desligado derruba a
    do tamanho do draw list; e um `flowHorizontal()` que recalcula o offset de
    cada filho percorrendo os anteriores --- mesma saída, O(n²) --- derruba as
    duas de custo e só elas.)
-   [x] Existe ao menos um exemplo pequeno e reproduzível. (`examples/hello.xml`
    e `examples/counter.xml` são executados por `examples_test` dentro de um
    `Kernel` real --- load/show, um botão clicado, um binding vivo ---, cada bloco
    `xml`/`lua` de `docs/examples.md` é parseado no mesmo teste, e a galeria dos
    `layouts/*.xml` é o exemplo grande, validado por `ogui_validate --manifest`
    com zero erros e zero avisos.)

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
