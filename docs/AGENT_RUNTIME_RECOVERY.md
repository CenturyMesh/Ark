# 002B — recuperação do fluxo dos agentes

Milestones D.02/D.04, com B.02/B.03 e C.02. Investigação iniciada após o relato
de agentes lentos, designs demasiado retilíneos e ferramentas de Frame aparentemente inativas.

## Causas demonstradas no código

| Alteração histórica | Comportamento observado na auditoria | Correção em 002B |
|---|---|---|
| `d73ad18` | Guia de fabrico e escolha de rota exigidos antes da autoria mecânica | Conceito primeiro em pedidos abertos; requisitos explícitos continuam a aplicar-se desde o início |
| `85612c1` | Pesquisa de catálogo imposta antes de recriar componentes | Pesquisa obrigatória quando o pedido precisa de componentes reais/especificados; envelopes de conceito claramente identificados são permitidos |
| `f77168e` | Um máximo de uma feature/placement por construção e instruções de sketch entidade a entidade | Até oito comandos coerentes numa transação, respeitando o papel, com uma avaliação e rollback atómico |
| `ed0a51d` | Vários tópicos de onboarding pré-carregados, sem estado vivo do documento | Arranque compacto; ler o estado/revisão reais e obter tópicos especializados quando necessários |
| `5e7153f` | Guia do engenheiro ampliado para onze tópicos | Engenheiro/designer começam com overview; reviewer recebe overview/physics |
| Camada de equipa em 001A | Operações públicas como new/undo/redo/import recusadas ao engenheiro; canvas fora do âmbito de escrita | Capacidades públicas restauradas ao engenheiro e canvas aos autores, com autorização da tarefa, revisão e isolamento de papéis |
| Previews de equipa em 001A | Candidate IDs anunciados mas recusados por falta de comandos explícitos | Receita de preview ligada ao autor, comandos/checks e revisão; commit do candidato validado conserva o caminho sem recompute |
| Transporte do resultado das ferramentas | O mesmo JSON seguia em `content.text` e `structuredContent` no histórico | Remover apenas a cópia textual byte-idêntica; preservar conteúdo diferente, imagens, annotations, erros e recibos |
| Interface Frame | `selectedEditableBody` recusava instâncias de assembly antes do handler de Frame | Editar a definição ConstructionFrame pela instância, identificando o efeito nas restantes instâncias |
| Projeção anterior | Curvas de arestas convertidas em cinco pontos; contornos de faces reconstruídos por endpoints | Projeção de curvas exatas; amostras servem apenas para apresentação |

Estas são regressões/restrições reproduzíveis. Não demonstram, isoladamente, qual
delas dominou cada run do utilizador. Os runs anteriores não guardavam a medição
separada de provider e ferramentas necessária para essa atribuição.

## Política de design

- Criar primeiro uma forma global coerente, proporções e interfaces editáveis.
- Usar círculos, arcos, splines, revolve, loft, sweep ou superfícies quando exprimem
  a intenção. Primitivas simples continuam disponíveis, sem impor designs de caixas.
- Não interromper um conceito para inventariar fabrico ou fazer cálculos não pedidos.
- Respeitar imediatamente dimensões, materiais, processos e limites indicados pelo utilizador.
- Rotas desconhecidas podem ficar unspecified durante o conceito. Isso não autoriza
  inventar materiais, propriedades, compatibilidade ou certificação.
- Pedidos de produção e afirmações de fabricabilidade continuam a exigir o contrato
  e a evidência correspondentes. Validade geométrica e revisão nunca são opcionais.
- Designer cria peças; engineer integra assemblies e decide alterações globais;
  reviewer inspeciona e propõe. Os schemas/instruções refletem essas permissões.

## Evidência de desempenho e seus limites

Os testes offline comparam a configuração anterior e a nova com o mesmo catálogo
e receitas CAD controladas. Medem bytes serializados, chamadas, transações e
equivalência da geometria/placements. O replay inclui formas curvas.

Há três medições distintas:

1. Guia completo de onze tópicos versus arranque overview, conservando todas as
   ferramentas do catálogo usado no teste.
2. Resultado MCP duplicado versus resultado sem a cópia textual idêntica.
3. A mesma construção dividida pela antiga restrição de uma feature versus um
   lote atómico coerente; os erros continuam a reverter o lote inteiro.

Os resultados numéricos do build final ficam no [recibo 002B](verification-002B.json).
São medidas de overhead e de um replay com transporte simulado. Não são um
benchmark pago de qualidade estética, latência do fornecedor ou poupança em euros.

| Medição offline, mesmo catálogo/receita | Política anterior reconstruída | 002B |
|---|---:|---:|
| Guia de arranque | 47 491 bytes | 5 232 bytes |
| Primeiro request | 97 206 bytes | 54 051 bytes |
| Receipt MCP do cenário | 17 882 bytes | 8 785 bytes |
| Request seguinte com esse receipt | 73 150 bytes | 63 419 bytes |
| Quatro peças curvas de display + quatro placements | 5 requests / 4 commits | 2 requests / 1 commit |
| Bytes enviados nesse replay Focused | 322 826 | 126 106 |

As 65 ferramentas base disponíveis ao modelo foram conservadas no benchmark;
`usage_record` permanece do host. O replay Parallel corrigido também usa dois
requests. Um teste separado confirma `applied_prepared:true`,
`geometry_recomputed:false` e `features_evaluated:0` no commit de um preview válido.

## Medições na aplicação

Activity mostra, por papel, número de requests, tempo de provider, tempo das
ferramentas e bytes de input/schema/request. São medições locais dos pedidos,
separadas dos tokens faturados que o fornecedor efetivamente reporta.

- Provider inclui rede, fila remota e geração da resposta.
- Tools inclui fila da interface e execução da ferramenta, não só CPU do kernel.
- Os valores registam chamadas medidas; tempos de papéis paralelos podem sobrepor-se.
- Bytes não são tokens nem uma estimativa de preço. Ausência de usage não significa custo zero.

Os modelos, esforços e modo escolhidos pelo utilizador são preservados. Não foram
introduzidos budgets de turns/tokens. Focused continua o padrão para novas
configurações; a preferência Parallel existente não é sobrescrita.

## Limite visual que permanece

O `snapshot` atual grava um PNG e devolve caminho/metadados. O conector interno
não entrega automaticamente esses pixels ao modelo. Receber um caminho não é
ver a imagem: o agente deve apoiar-se nas inspeções geométricas disponíveis e
não afirmar que fez uma avaliação visual. Feedback multimodal verdadeiro continua
um incremento pendente de D.04; esta limitação não foi atribuída ao onboarding.

## Como verificar a recuperação

Abrir a nova build, carregar o desenho guardado e iniciar uma equipa com o objetivo
pretendido. A sessão antiga não é alterada ao instalar a nova versão. Comparar
tarefas com o mesmo objetivo, modelo e esforço; observar a separação Provider/Tools
antes de atribuir uma pausa ao kernel ou ao modelo.

Para Frame, selecionar uma definição ou instância **ConstructionFrame**. Trim e
weld não passam a aceitar sólidos/tubos genéricos por ficarem visíveis no ribbon.
Para projeção, consultar o contrato de projeções e os novos comandos no Sketch.
