# Benchmark nativo dos agentes — D.01 / D.02

`ark-benchmark` compara Single (`runAssistant`), Focused e Parallel (`AgentTeam`)
com o executor real `AgentSession`. Cada tentativa começa num documento novo,
com o mesmo baseline sintético, brief, modelo, esforço e critérios nativos.
As referências não entram no prompt do provider real. O corpus versionado
`benchmarks/agent_briefs_v1.json` contém 30 tarefas de edição dimensional,
parâmetros, primitivas exatas, sketches, loft, datums, assemblies, materiais,
transformações e booleanos. Não lê settings do Studio nem desenhos do utilizador.

As três repetições iniciais estão na configuração. A ordem das estratégias roda
entre repetições para reduzir o efeito da ordem/cache; a ordem das tarefas é
fixa. O estado local do modelo, aquecimento, quantização e hardware também
afetam os resultados e devem acompanhar qualquer experiência publicada.

```powershell
out/native-release/Release/ark-benchmark.exe --preflight
out/native-release/Release/ark-benchmark.exe --cases B01,P01,A01 --repetitions 1 --out out/verification/benchmark-offline.jsonl
out/native-release/Release/ark-benchmark.exe --live --config benchmarks/ollama-qwen-q3.json --cases B01,P01,A01 --repetitions 1 --out out/verification/benchmark-live-q3.jsonl
out/native-release/Release/ark-benchmark.exe --vision-smoke --live --config benchmarks/ollama-qwen-q3.json --out out/verification/vision-live-q3.jsonl
```

O primeiro comando verifica todos os baselines e receitas de referência no CAD
C++/OCCT. O segundo usa um transporte determinístico offline: qualifica o
harness, os commits, os checks e as medições. Nesse modo o lead executa a
receita de referência; os especialistas aguardam uma missão. Não mede a
capacidade, rapidez ou qualidade de um LLM nem a eficácia da delegação.
O terceiro ativa explicitamente o provider configurado. Para uma experiência
completa remover o filtro de casos/repetições e usar as três repetições do JSON.
Os presets locais não alteram a preferência do utilizador no Studio.

Os defaults procuram `benchmarks/agent_briefs_v1.json`, `benchmarks/offline.json`
e `examples/pneumatic-cylinder.arkops` junto ao executável antes de usar o cwd
de desenvolvimento. Assim o pacote funciona a partir de outra pasta. Valores
explícitos de `--suite`, `--config` e `--out` conservam exatamente o caminho do
caller; um ficheiro explícito ausente/inválido não é substituído pelo bundled.
O teste `tests/agent_benchmark_paths_tests.ps1 -Executable <ark-benchmark.exe>`
verifica estes contratos offline após copiar os recursos do pacote.

O quarto é um smoke separado de entrega de pixels. Carrega apenas a receita
sintética versionada `examples/pneumatic-cylinder.arkops`, disponibiliza apenas
`snapshot` e pede a descrição do mecanismo e das ligações visíveis, sem revelar
ao modelo o nome/tipo do exemplo. Guarda o PNG em `<out>.png`, metadata de
revisão/câmera, bytes reais, resposta textual e usage/performance do connector.
`pixels_in_completed_provider_turn` exige captura real, novo request e conclusão
do provider real; não é um teste automático de reconhecimento. A avaliação
visual humana continua explícita e pendente. No modo offline nunca declara
entrega a um LLM nem compreensão visual.

No ensaio local de `008D`, o perfil de teste separado
`ark-bench-008d-q3-16k` (contexto 16384, esforço `none`) completou a entrega
real em 43,474 s: quatro respostas com usage, cinco snapshots e 12861 tokens
reportados. O receipt é `out/verification/008D-vision-live-q3-16k-none.jsonl`.
O modelo descreveu haste, olhal e furo visíveis, mas também sugeriu taper e uma
ranhura que a receita não declara; afirmou um único sólido sem poder confirmar
a assembly pela imagem. A revisão humana deve separar esses acertos, hipóteses
e erros. A tentativa anterior com o preset Q3 original expirou no primeiro HTTP
após 90,008 s, com usage desconhecida, e permanece contabilizada em
`008D-vision-live-q3.jsonl`. Estes ensaios verificam transporte e descrição
visual no escopo observado; não validam design, mecanismo ou fabrico e não
demonstram redução de custo. O perfil separado não altera preferências globais.

Cada linha de tentativa é gravada e descarregada antes da seguinte. A última
linha agrega tentativas aceites e falhadas; uma interrupção posterior mantém
as linhas anteriores. São registados tokens reportados e respostas sem usage,
tokens cached/reasoning como subconjuntos, chamadas e tempo do provider,
chamadas e tempo das ferramentas, bytes dos envelopes/inputs/schemas, tamanho
máximo, erros, revision inicial/final, baseline, critérios e receipts nativos.
O tempo da tentativa exclui preparação do baseline e avaliação externa final;
o baseline tem o seu tempo separado. O tempo das ferramentas inclui a execução
e espera/serialização do host. Todas as tentativas falhadas contam no denominador.
O custo permanece `null` sem tarifa/recibo verificado; usage ausente nunca vale
zero custo. Os dois presets Ollama permitem comparar a mesma tarefa entre a
quantização Q3 e `qwen3.8:27b`, mas uma comparação entre estratégias deve manter
o mesmo preset/modelo/esforço.

`deadline_seconds` é uma janela finita do host de benchmark. Não altera budgets
dos agentes: `maxRounds=0` e `maxTurnsPerRole=0`. Ao expirar pede cancelamento;
uma operação CAD já em execução termina antes da limpeza, pelo que a duração
total pode exceder a janela. Os receipts existentes permanecem. Os timeouts HTTP
são os do connector configurado. `--live` não é inferido do preset.

`accepted` exige conclusão do agente/equipa, ausência de timeout/falha terminal
e todos os critérios medidos. A equipa deve declarar a conclusão do objetivo;
idle sozinho não satisfaz esse contrato. Falhas recuperadas das ferramentas
continuam nos receipts. A aprovação nativa cobre apenas os requisitos da tarefa;
`human_blind_review: pending` não é uma avaliação estética/conceptual, nem uma
qualificação de fabrico. D.01 permanece parcial até completar as repetições e a
avaliação humana cega. Uma execução pequena pode revelar falhas e overhead;
não demonstra redução de custo nem melhoria de design.

D.02 usa as APIs existentes de contexto por objeto/deltas. O lead recebe
instruções para resolver um design coerente, agrupar operações relacionadas e
delegar apenas missões concretas com IDs, revisão e critérios mensuráveis.
O `team_send` admite uma missão tipada do lead para um autor:

```json
{"to":"designer","kind":"assignment","message":"Editar apenas Block; manter a interface medida.","scope":{"ids":["Block"],"expected_revision":12}}
```

O host resolve até oito alvos existentes pelo `object_context`, serializado com
as ferramentas CAD, e anexa a revisão de origem e o contexto compacto à missão.
Uma revisão antiga, alvo ausente, scope inválido ou mailbox demasiado grande
rejeita a entrega. O especialista tem de reinspecionar a revisão atual antes
de escrever. Este scope é contexto da missão: não implementa branches isolados
nem uma ACL que proíba editar outras peças. Mensagens sem scope conservam o
comportamento existente.

Focused conserva uma conversa de cada vez; Parallel usa o scheduler real com
até três workers neste benchmark. Nenhuma estratégia pode rebasear writes
silenciosamente. São deliberadamente diferentes as instruções de coordenação
e ferramentas da equipa; o objetivo, baseline, provider e aceitação são iguais,
e os bytes desses envelopes são medidos.

## Fecho do piloto local de 008D — 11 de outubro de 2026

O piloto já iniciado terminou com nove recibos e agregado final. Usou os casos
`B01,S02,A01`, uma repetição, o mesmo modelo local `ark-bench-008d-q3-16k`
(Qwen Q3, contexto 16384, esforço `none`) e os mesmos baselines/critérios do
piloto anterior. A janela de 120 s pertence apenas ao host do benchmark.

| Estratégia | B01 | S02 | A01 | Aceites | Tokens reportados, incluindo falhas | Respostas sem usage |
|---|---|---|---|---|---|---|
| Single | Passou, 19,111 s | Falhou, 87,482 s | Passou, 22,931 s | 2/3 | 481 472 | 0 |
| Focused | Passou, 22,762 s | Falhou, 58,071 s | Falhou, 94,071 s | 1/3 | 391 386 | 2 |
| Parallel | Falhou, 119,815 s | Falhou, 82,951 s | Passou, 50,399 s | 1/3 | 536 739 | 1 |

Resultado posterior ao schema: **4/9 aceites, 5/9 falhados**, conservados em
`out/verification/008D-comparison-live-q3-16k-none-after-schema.jsonl`. O anterior
**0/9** permanece em `out/verification/008D-comparison-live-q3-16k-none.jsonl`.
Os hashes, agregados e resumos das nove tentativas de cada piloto estão no
[registo de verificação](verification-008D.json); os recibos integrais ficam locais.

B01 Parallel terminou interrompido após erro HTTP 400; S02 nas três estratégias
e A01 Focused terminaram sem conclusão válida do provider. As cinco falhas
mantiveram a revisão inicial. Rejeições de comandos malformados e erros
recuperados nas tentativas aceites também permanecem contabilizados. Nenhuma
tentativa posterior declarou timeout do host; 119,815 s não é classificado como
timeout apenas por estar próximo da janela de 120 s.

Nas seis tentativas de equipa, designer e reviewer ficaram sem missão e com
zero turns: este piloto exercitou os modos de equipa, mas não demonstra benefício
de delegação ou execução paralela. Não se repetiu o ensaio para obter um score verde.
Os tokens são os totais reportados pelo provider, incluindo inputs repetidos/cache;
não são um preço. Custos continuam desconhecidos, incluindo as respostas sem usage.
**D.01 permanece Parcial**: faltam as três repetições do corpus completo e a
avaliação humana cega. A amostra não isola causalidade da correção de schema,
nem sustenta ganhos gerais de custo, qualidade ou fabrico.
