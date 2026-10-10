# 005B — auditoria de modelação complexa e eficiência dos agentes

Data: 11-10-2026. Baseline: 004B / `a93e275`. Milestones B.01, B.02,
B.03, C.02, D.02 e D.04. Auditoria de código, contratos, testes e de um
desenho local autorizado, conservado fora do repositório e do pacote.

## O que foi encontrado e corrigido

| Área | Evidência | Alteração |
|---|---|---|
| Autoria de arcos | O kernel já aceitava arcos por centro/raio/ângulos; faltavam receitas acessíveis por três pontos e tangente | Construções exatas 2D e espaciais, expressões persistentes, edição e controlos no ribbon |
| Continuidade espacial | A interface anterior exigia JSON para muitas curvas; arcos de raio eram apenas menores | Campos XYZ e modos three_point/tangent; continuidade tangente a um segmento anterior e opção major no kernel |
| Indicadores de sketch | O desenho de anchors tratava pares de DOFs de arco como XY | Usar endpoints avaliados, sem desenhar raio/ângulo como pontos falsos |
| Feature tree | Muitos elementos podiam ficar fora do painel sem indicação de posição | Scrollbar com thumb, arrasto, paginação, roda, clipping e ajuste após resize/collapse |
| Encontros tubulares | O desenho auditado tinha 24 Tubes e zero ConstructionFrames; ferramentas de frame não podiam editar essa autoria | Operação ligada `tube_end_fit` para Tube e sua cadeia de transformações, preservando as fontes |
| Propriedades após trim | Um novo native_solid não herdava automaticamente o material da peça cortada | Herança de material/aparência declarados no novo end fit, com overrides explícitos preservados |
| Consultas repetidas | `doc_info design` devolvia todo o desenho de 154 features | Seleção por IDs/nomes e secções, com revisão e âmbito parcial explícitos |
| Conclusão | Idle/Turn completed apenas indicavam fim de uma resposta | Relatórios explícitos de tarefa e objetivo, com guards para trabalho/erros pendentes |

## Medição de payload

Num documento local de 154 features, as mesmas duas linhas de feature foram
pedidas pelo caminho completo e pelo seletivo. Os bytes abaixo são o JSON efetivo
de `content.text` emitido pelo MCP, não uma estimativa de tokens.

| Consulta | Bytes |
|---|---:|
| `doc_info{detail:"design"}` completo | 117 916 |
| Mesmas duas features, `ids:[3,8]` | 1 650 |
| Mesma seleção, todas as secções explicitamente pedidas | 3 475 |

A primeira seleção reduziu esse payload em **98,6%**, mantendo os objetos das
features exatamente iguais aos do resultado completo e sem mudar a revisão.
Não se mediu uma redução equivalente de custo, tempo do fornecedor ou tokens
faturados. A escolha de ferramentas pelo modelo continua a influenciar o resultado.

Depois do inventário inicial, pedir IDs conhecidos e apenas as secções necessárias.
Quando parâmetros globais forem pedidos, o resultado identifica esse âmbito;
as outras secções selecionadas representam associações diretas. Não interpretar
um resultado parcial como inventário completo ou ausência de outros problemas.
`doc_get` continua disponível quando é necessária toda a autoria, sem truncagem
silenciosa de histórico ou remoção dos recibos de alterações.

O teste de memória passou a derivar o seu limite a partir do pedido real com
schemas/instruções: base 8 361 bytes, limite de fixture 9 385, maior pedido 8 721.
Isto corrige uma suposição do teste; os limites de produção não foram aumentados
e não foram introduzidos budgets de turns/tokens.

## Auditoria geométrica do exemplo

O exemplo local tem 154 features, 24 Tubes, 103 ocorrências e dois mates. Os doze
frames das ocorrências envolvidos nos encontros auditados são identidade: as
transformações já estão na autoria das features. As medições de B-rep correspondem
por isso às posições realmente colocadas, sem aplicar uma transformação duas vezes.

Foram medidos dez encontros com sobreposição de material. Uma cópia independente
recebeu dez operações ligadas e as ocorrências foram retargeted para os resultados;
as 24 fontes Tube e as 103 ocorrências foram preservadas. O original foi verificado
por hash e não foi alterado. A primeira passagem de coping eliminou a sobreposição
nos dez encontros e conservou contacto após guardar/reabrir, mas sete ainda
retinham área da tampa original. Esse resultado não foi confundido com dez juntas
totalmente flush, fechadas ou qualificadas para soldadura.

A revisão seguinte distinguiu o ajuste com apoio integral (`plane`) do corte
pela superfície plana de referência (`plane_trim`). O segundo conserva validade,
contacto e rejeição de sobreposição, mas mede e relata o apoio parcial em vez de
impor apoio integral como condição para uma operação CAD válida.

A cópia final local `out/bike3-flush-005B.ark` usa seis cortes planares e quatro
copes. Uma faixa de 1,5 mm na extremidade da quilha ficava fora da projeção do
cilindro OD76: o centro da secção 75×75 estava em X422 e o eixo do cilindro em
X420. Prolongar o tubo não podia remover essa faixa. Na cópia, a translação literal
da quilha foi corrigida explicitamente de X422 para X420, sem alterar secção,
diâmetro ou mates. Os dez encontros foram então revalidados: zero sobreposição,
contacto e remoção completa das tampas originais, incluindo readback após save.
As seis interfaces planares conservam área de apoio parcial medida; o corte flush
não é confundido com fechamento ou qualificação de soldadura.

Uma separação de cerca de 9,7 mm entre componentes da direção foi excluída da
correção automática: preencher essa separação poderia alterar a intenção mecânica.
Outros membros distantes também não foram tratados como juntas só pela proximidade
dos nomes. Os detalhes geométricos e imagens locais ficam em `out/bike3-audit-005B`.

## Limites que permanecem

- O 3D curve é autoria espacial exata, com relações específicas de construção;
  não é ainda um solver geral de constraints de sketch 3D.
- Tangência de construção e as constraints existentes não implicam constraints
  gerais entre quaisquer duas splines/superfícies ou continuidade Class A.
- Trim de extremidade, apoio integral numa face, coping e soldadura são operações
  distintas; os relatórios identificam contacto, sobreposição e cobertura.
- O movimento de assemblies continua limitado aos grafos cinemáticos documentados;
  não houve alteração para dinâmica de contacto ou análise estrutural.
- Snapshot ainda fornece caminho/metadados ao conector interno; não se declara
  que o agente viu pixels que não lhe foram entregues.
- Um objetivo concluído é uma declaração explícita da equipa com guards de fluxo,
  não uma prova automática de que a intenção do utilizador ou o fabrico está perfeito.

Ver [contrato de sketch](PARAMETRIC_SKETCH.md), [superfícies](SURFACING.md),
[tubos/frames](FRAME_SHEET_OPERATIONS.md) e [recibo da entrega](verification-005B.json).
