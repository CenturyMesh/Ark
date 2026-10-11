# Contexto de objetos existentes — 006B

Escopo: **B.01**, **B.04**, **D.02** e **D.04**. O contexto por objeto apoia
iterações num desenho já existente. Não prova melhoria do design, poupança de
custo, equivalência geométrica ou qualificação de fabrico. A entrega visual e
a verificação observada estão nas notas da iteração.

## Identidade, conteúdo e autoridade

O ID persistido de uma feature identifica a definição dentro do documento.
Uma ocorrência identifica a colocação dessa definição: `node` e `copy` distinguem
instâncias e cópias de um pattern. Os nomes servem para descobrir objetos; um
nome duplicado é uma referência ambígua e rejeita a seleção explícita.

`object_context` devolve chaves como `feature:73` e `occurrence:21:0`, uma revisão
observada e um fingerprint de conteúdo. O fingerprint **não substitui o ID**.
É um token determinístico de deteção de alteração, com JSON canónico e FNV-1a
de 64 bits (`oc1-fnv64:`). Não é uma assinatura de segurança nem uma prova de
igualdade de B-reps; colisões são possíveis. Os tokens pertencem ao documento
atual e não devem ser comparados como identidades entre desenhos diferentes.

O conteúdo considerado inclui a receita persistida, dependências, parâmetros
referenciados e respetivas cascatas, sketches, datums, materiais, colocação
resolvida e vizinhança ligada por mates. Também inclui a evidência avaliada que
está disponível. A ordenação das chaves JSON não muda o token. A revisão da
sessão, tempos de execução, usage ledger, imagens e histórico de conversação
não fazem parte desse token. A descoberta de recursos é conservadora: nomes
exatos e identificadores de expressão existentes podem invalidar contexto a
mais; nunca se deve interpretar `changed:false` como aprovação para fabricar.

B-rep e os inputs explícitos continuam a ser a autoridade geométrica. O campo
`authored` contém declarações e receitas; `measured` distingue a origem dos
bounds e do volume. Um volume nativo só é reportado quando existe no relatório
B-rep. Bounds sem esse relatório podem vir da geometria de apresentação.
`manufacturing_qualified:false` permanece explícito. Para referências exatas,
validação nativa ou física, usar as inspeções especializadas.

## Protocolo de leitura e edição

Descobrir pelo nome parcial ou tipo sem ler todo o documento:

```json
{"tool":"object_context","arguments":{"query":"susp","limit":8}}
```

Selecionar uma definição identificada:

```json
{"tool":"object_context","arguments":{"ids":[73],"expected_revision":14}}
```

Selecionar apenas uma ocorrência, com a sua colocação:

```json
{"tool":"object_context","arguments":{"node":21,"copy":0,"expected_revision":14}}
```

`ids` aceita IDs ou nomes únicos. `node` aceita um ID ou nome único de uma
instância de peça; um grupo não é uma peça selecionável nesta API. Não combinar
`ids` e `node`. `copy` começa em zero e exige `node`. `type` usa o nome estável
de feature, como `native_solid`, `transform` ou `tube`. `query` é uma substring
sem distinção de maiúsculas ASCII, até 128 caracteres. A pesquisa não deduz a
função mecânica a partir da imagem ou de uma palavra.

O resultado contém `revision`, `objects` e `fingerprints`. Uma linha alterada
inclui identidade, receita compacta, evidência disponível, dependências,
recursos e mates. Uma leitura de uma ocorrência contém só a colocação
selecionada; vizinhos são referências compactas, sem repetir as suas receitas.

Reutilizar os tokens devolvidos para pedir deltas:

```json
{"tool":"object_context","arguments":{"ids":[73],"expected_revision":15,
 "known_fingerprints":{"feature:73":"oc1-fnv64:0123456789abcdef"}}}
```

Uma linha inalterada mantém `key`, `id`, `name`, `type`, `fingerprint` e
`changed:false`, mais `node`/`copy` quando aplicável; os detalhes são omitidos.
Conservar localmente os detalhes associados ao token. Uma mudança numa
dependência ou colocação invalida o contexto mesmo que a receita da feature
selecionada permaneça igual. A revisão devolvida continua a ser necessária em
`doc_apply`; o token nunca permite rebase silencioso.

`expected_revision` rejeita uma leitura desatualizada com `REVISION_CONFLICT`.
Uma seleção inexistente ou uma cópia removida rejeita com `REFERENCE_MISSING`.
Nomes duplicados rejeitam com `AMBIGUOUS_REFERENCE`. Uma pesquisa sem resultados
devolve uma lista vazia. A API é de leitura: não altera o desenho, ficheiro,
histórico ou revisão.

## Limites e apresentação

`depth` de 0 a 2 expande dependências e dependentes; o default é 0. `limit` de
1 a 64 limita as linhas; o default é 16. Seleções explícitas devem caber nesse
limite. A descoberta sem expansão pagina por `after_id`; quando há mais
resultados, `next_after_id` é o último ID devolvido. A revisão deve ficar
constante ao percorrer as páginas.

`partial:true` significa contexto delimitado. `truncated`, `has_more` e os
campos `omitted` expõem as limitações; não esconder essas indicações do agente.
Arrays grandes, strings extensas, fontes, meshes e buffers B-rep são substituídos
por tamanho e token local. Receitas compactas têm um limite de 8 KiB por ramo;
linhas têm 24 KiB e o conjunto até 256 KiB, excluindo metadados do envelope.
Os buffers originais continuam no documento e participam no token.

No Studio, o menu de contexto **Ask Agent** seleciona uma peça para o próximo
pedido. A composição acontece ao enviar a tarefa: associa a identidade e a
revisão atuais ao texto do utilizador e ao contexto estruturado. Seleção não
é uma instrução automática de edição. Mudança de documento, remoção da peça
ou referência inválida deve exigir uma seleção atual; não escolher outra peça.

Não é necessário enviar uma imagem base64 para identificar uma peça cujo
ID e receita já existem. Uma imagem pode fornecer feedback visual complementar,
mas transportar pixels não acrescenta a identidade persistida, expressões ou
referências exatas. Esta API não transmite pixels ao provider nem autoriza
declarar que o agente viu uma imagem.

## Verificação e medição

Os testes de contrato verificam cascatas de parâmetros, edits sem relação,
sketches ligados, colocação de pais, ocorrência isolada, persistência, referências
inválidas, seleção ambígua, paginação, limites e retenção local de buffers.
`ARK_CONTEXT_BENCHMARK` permite medir numa sessão autónoma de leitura os bytes
de JSON completo, contexto selecionado e delta inalterado, mais tempos CPU.
Não grava o modelo nem inclui desenhos do utilizador no repositório.

A implementação atual lê e serializa o documento completo localmente, produz
um inventário de design e resolve a assembly antes de construir o contexto
selecionado. Não descodifica buffers B-rep para o fingerprint nem reavalia as
features. O custo local depende da dimensão e conectividade; a expansão do
fecho de dependências repete trabalho por objeto. Contexto menor para o provider
não prova redução equivalente de trabalho local, tokens faturados ou custo.
Qualquer comparação de design/custo exige tarefas, modelos, inputs e critérios
equivalentes, incluindo tentativas falhadas.
