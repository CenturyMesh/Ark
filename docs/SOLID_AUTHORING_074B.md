# Autoria de sólidos 074B

Milestones **B.01, B.02, B.03 e A.03**: verificado localmente no escopo nativo
abaixo. A qualificação da interface e do pacote é registada separadamente na
iteração. Os comandos continuam sujeitos a `expected_revision`, candidato
verificado e publicação atómica.

## Regiões e vários corpos

`native.region_mode:"arrangement"` divide as curvas do sketch nas suas células
planas fechadas, usando interseções e faces OCCT. A seleção usa pontos interiores:

```json
{"op":"extrude","region_mode":"arrangement","regions":[{"point":["SeedX","SeedY"]}],"result_mode":"bodies"}
```

`sketch_inspect` aceita `native_regions:true, region_mode:"arrangement"` e devolve
as regiões com área, furos, seed e linhagem das arestas de origem. Um ponto na
fronteira ou ambíguo é rejeitado. Neste modo, uma entidade pode limitar várias
células e não substitui o seletor por ponto. O modo anterior de loops mantém-se
por omissão.

`result_mode:"bodies"` conserva todos os sólidos positivos selecionados numa
feature. `report.bodies` e `topology.solids` identificam cada sólido, volume,
limites e índices das suas faces. Os IDs `@ark.solid.<digest>.<index>` pertencem
apenas ao B-rep atual; não são nomes persistentes entre alterações da geometria.
Não há descarte implícito de ilhas, paredes ou fragmentos. A operação aceita até
64 corpos, enquanto a descoberta de regiões apresenta até 256 células.

`native.boolean_mode:"all"` faz uma construção com todos os inputs. Uma união
pode assim ligar dois alvos separados através de uma extrusão posterior na lista.
`cut` subtrai a união dos cortantes; `common` calcula a interseção de todos os
inputs, e não apenas com a união dos restantes. São aceites 2–16 inputs. O modo
anterior sequencial permanece por omissão. O trabalho paralelo usa o orçamento
de threads nativas do pedido; a capacidade de paralelizar não é uma promessa de
menor duração para qualquer desenho.

## Extrusão fina e fillet variável

No diálogo **Extrude sketch**, `Sketch regions` escolhe loops ou células
intersectantes, `Thin method` escolhe o método de parede e `Corner joins` define
o tipo de canto. `Region output` permite features separadas ou uma feature com
todos os corpos selecionados. Ao editar apenas dimensões, `Keep region selection`
mantém os seletores e as suas expressões; mudar o sketch ou modo desativa essa
opção. A revisão apresenta a quantidade de sólidos e o volume, ou a área quando
o resultado é uma superfície.

Em **Fillet or chamfer**, `Radius mode: Variable contour` abre uma tabela de
2–16 estações, cada uma com fração e raio. Os campos aceitam expressões, e
`Origin edge` identifica a origem entre as arestas selecionadas. A tabela segue
para uma pré-visualização nativa antes de Apply. Constant mantém o fluxo anterior;
um documento alterado enquanto o diálogo está aberto exige nova inspeção.

`native.thin_method:"offset"` e `native.thin_join:"arc"|"intersection"` ativam
a construção nativa por offset, com `p.thickness` e os lados existentes:
`inside/outside/symmetric` para contornos fechados, `left/right/symmetric` para
abertos. `SetApprox(false)` impede a conversão solicitada para linhas/arcos.
O método analítico anterior continua por omissão.

Os contornos fechados são tratados individualmente no plano e normal autorados,
com sinal contrário para os furos. A contenção e a cobertura da linhagem
`Generated` são verificadas antes de formar a parede. Contornos que colapsam,
se cruzam ou perdem arestas são rejeitados. Paredes separadas de furos exigem
`result_mode:"bodies"`. Perfis abertos usam o plano encontrado pelo produtor,
comparado com o plano autorado antes de construir o offset; os extremos também
têm de cumprir o lado solicitado. Linhas isoladas conservam a construção
analítica exata, pois não definem por si só um plano único.

`native.radius_domain:"contour"` alarga `radius_law` a curvas e cadeias tangentes.
Todas as arestas da cadeia têm de ser explicitamente nomeadas. As frações seguem
o comprimento da cadeia, com origem e sentido verificados. A interpolação nativa
por aresta recebe as estações autoradas e valores lineares nas fronteiras entre
arestas. Uma cadeia fechada exige raios iguais nas frações 0 e 1. Arestas implícitas,
origens ambíguas e falhas do fillet são rejeitadas sem reduzir raios ou aumentar
tolerâncias. A semântica anterior da lei numa aresta reta permanece disponível.
As falhas incluem os contours/vertices assinalados pelo kernel; uma seleção
incompleta identifica as arestas atuais que faltam. Um resultado parcial não é
aceite como sólido válido.

## Referências e seleção explícita de corpos

`NativeSolid` com `op:"reference_anchor"`, um input sólido, `alias`, `point` e
`source_reference` opcional cria uma referência nomeada sem mudar a geometria.
Com `source_reference`, só considera os descendentes reais dessa face publicados
por `Modified/Generated`; sem ele, considera as faces atuais no ponto explícito.
O ponto tem de estar no interior de exatamente uma face, à tolerância da própria
face. Não existe seleção pela face mais próxima. Faces curvas de sólidos são
suportadas; um frame só é publicado quando o produtor o disponibiliza.

`op:"body_select"` aceita um input e **ou** `body_ids:[...]` **ou**
`points:[[x,y,z],...]`. Os pontos são estritamente interiores; fronteiras,
sobreposições ambíguas e seleções repetidas rejeitam. IDs de um digest anterior
também rejeitam. Pontos e coordenadas do anchor podem ser expressões. O recibo
enumera os corpos conservados e omitidos, incluindo os respetivos volumes.
Grupos históricos não vinculados mantêm o seu estado, membros e explicação;
os membros nomeados propagam-se individualmente. Não se confundem com uma face
que a operação efetivamente dividiu.

Exemplo de anchor após uma face se dividir:

```json
{"op":"reference_anchor","alias":"Mount","source_reference":"z_max","point":["2","10","Height"]}
```

## Persistência e prova local

Os novos contratos requerem a versão semântica **59**, condicionalmente. As
receitas anteriores conservam a sua versão e o comportamento por omissão. Um
documento que declare estas semânticas como versão 58 é rejeitado, preservando o
destino.

O teste `solid-authoring-074b` passou **123 checks**. O recibo local
`dev-build-20261108T172201924Z.json` confirma novamente essa suite e
`modeling-workflow`, após a correção de preservação dos grupos históricos.
Inclui grelha de quatro células, círculos
intersectantes e aninhados, bridge union, cut/common com vários inputs, volumes
analíticos de paredes interiores/exteriores/centradas, plano invertido, perfis
mistos C0 e tangentes, fillet circular e cadeia estádio, aliases, IDs obsoletos,
persistência e falha atómica. O caso C0 original permanece com o oráculo da
integral exata das linhas/arcos da sua fronteira, distinto do caso tangente.

Também passaram 14 suites de regressão nativa, incluindo os contratos de sólidos,
features, regiões 046B, extrusão 073B, split lines e caches. Esse checkpoint teve
alterações concorrentes na fonte da interface, pelo que não qualifica sozinho o
par global fonte/binários. Os testes locais não equivalem à distribuição do
pacote nem à qualificação de fabrico.

Os testes locais dos diálogos adicionais passaram 61 checks de extrusão e
44 de fillet por execução, nas configurações normal e compacta do Studio.
As capturas dos controlos, estações, expressões editadas e candidato do fillet
foram inspecionadas: textos e ações permaneceram legíveis, e a revisão mostrava
o sólido nativo e o respetivo volume. Estes resultados complementam, sem
substituir, a qualificação final do pacote.
