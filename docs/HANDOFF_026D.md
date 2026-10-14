# Continuar em casa: 026D e paragem da equipa

Registo de 2026-10-14. Milestones **D.02 / A.03: Parcial**. Esta passagem de
trabalho documenta o diagnóstico; não altera o runtime nem corrige ainda o bloqueio.

O conceito foi preservado, mas a equipa não conseguiu declarar a conclusão.
Os registos apontam para recuperação incompleta do estado do revisor após uma
falha do fornecedor. **Não há evidência de esgotamento de contexto nesta sessão.**

## Código e versão para retomar

- Repositório: `CenturyMesh/Ark`, privado; licença Apache-2.0 mantida.
- Branch: `feature/024B-steer-decals-split`.
- [PR #11](https://github.com/CenturyMesh/Ark/pull/11), draft, dependente do PR #10.
  Manter sem merge até ao teste e aprovação do utilizador.
- Runtime: **026D / 0.9.27**; implementação `a06005e`, recibo de entrega `0389dfc`.
- [Notas 026D](iterations/026D.md), [conectores locais](LOCAL_MODELS_026D.md),
  [recibo de verificação](verification-026D.json).

Num diretório novo em casa, com autenticação GitHub configurada:

```powershell
git clone --branch feature/024B-steer-decals-split https://github.com/CenturyMesh/Ark.git Ark-026D
Set-Location Ark-026D
Get-Content docs/HANDOFF_026D.md -Encoding utf8
```

O Git transporta código e documentação; não inclui os executáveis em `out/`.
Para compilar em casa, seguir [NATIVE_BUILD.md](NATIVE_BUILD.md), usando os
caminhos de dependências desse computador. Se transportares a pasta completa
do preview já preparado, podes executar `ark-studio.exe` dessa pasta.
Neste computador, o comando do preview verificado é:

```powershell
& 'C:\Sw_templates\Ark-024B\out\test-preview\026D-test1\ark-studio.exe'
```

**Levar também `Downloads\soubarnew.ark` por um meio privado.** Confirmou-se
por leitura SQLite sem escrita que contém o conceito da captura e os 113
registos de atividade. O ficheiro e os logs brutos não entram no Git.
Os registos de atividade persistem; não assumir que o processo, as conversas
dos quatro workers ou os flags da equipa serão restaurados ao abrir o projeto.

## Porque parou

A captura é de uma equipa **ChatGPT / gpt-6.1-sol / high**, não do teste Ollama.
A inspeção read-only da sessão aberta confirmou revisão 10, 12 features,
8 ocorrências de assembly e nenhuma criação automática de desenhos 2D.
Isso confirma a presença do trabalho, não qualificação funcional ou de fabrico.

Sequência observada na atividade; horas UTC:

| Hora | Evidência |
| --- | --- |
| 16:12:37 | O snapshot do revisor falhou; o feedback atribui a rejeição a uma revisão que avançou durante autoria concorrente. |
| 16:15:53 | Último `doc_info` do revisor no registo, antes da recuperação da falha do fornecedor. |
| 16:16:57–16:17:09 | O coordenador registou inspeção do estado de falha do fornecedor e enviou uma mensagem para retomar o revisor. O erro bruto do fornecedor não consta destes recibos. |
| 16:18:08 | O revisor registou que identificara o snapshot antigo como impedimento à conclusão. |
| 16:18:19 | Novo snapshot do revisor terminou com sucesso na revisão 10. |
| 16:19:23–16:19:27 | Revisor e coordenador reportaram que a finalização continuava bloqueada pelo estado da plataforma, apesar de considerarem a revisão substancialmente concluída. |

**Causa mais provável, sustentada pelo código e pela ordem dos registos:**
uma interrupção do worker ativa `needsInspection`. Retomar uma resposta ou obter
um snapshot com sucesso não limpa esse flag. Só um `doc_info` bem-sucedido,
executado pelo próprio papel e com uma revisão válida, o limpa. Não existe tal
leitura pelo revisor depois da recuperação nos 113 registos observados.

Em [agent_team.cpp](../src/agent_team.cpp), verificar:

- `work()`: falhas do fornecedor/worker ativam `needsInspection`; uma nova
  execução limpa `view.error`, mas mantém a obrigação de inspecionar.
- `call()`: `doc_info` válido limpa `needsInspection`; um snapshot válido resolve
  a falha de snapshot desse papel, mas não substitui a inspeção do documento.
- `report()`: a conclusão é rejeitada perante erros pendentes ou `needsInspection`.
- `snapshot()`: esses estados fazem o objetivo aparecer como `Interrupted` e
  `goalDeclared=false`, mesmo com todos os workers `Idle`.

`Idle` significa que a resposta terminou, não que a tarefa foi concluída.
A frase genérica “Turn finished / task status requires explicit report” aparece
no fim de respostas bem-sucedidas e não identifica sozinha o bloqueio.

O diagnóstico do flag é uma inferência forte, não uma captura direta da memória
da equipa. O MCP externo consultado expõe documento/atividade/falhas CAD, mas não
o `team_context` interno. O erro original do fornecedor e a causa desse erro
ficam por confirmar. Não atribuir a paragem a contexto, timeout ou rede sem esse
recibo. A inspeção externa feita para este documento não limpa os flags do revisor.

## Retomar a sessão e corrigir o comportamento

Se a mesma equipa ainda estiver aberta, enviar este steer ao coordenador:

> Inspeciona team_context. Pede ao reviewer que execute diretamente doc_info
> detail design para verificar a revisão atual após a interrupção. Depois,
> verifica os erros ainda pendentes e reporta a tarefa concluída apenas se a
> revisão estiver efetivamente terminada. Integra os relatórios e finaliza o
> objetivo no escopo de conceito. Preserva o modelo; não repitas operações já
> aplicadas e não declares qualificação de fabrico.

Este steer ainda **não foi executado** nesta auditoria. Se houver outro bloqueio,
guardar o respetivo recibo e identificar a operação/papel antes de tentar resolver.
Numa nova sessão em casa, abrir o projeto, inspecionar o estado atual e retomar
o conceito; não tentar reproduzir a antiga sequência de escritas.

Trabalho de desenvolvimento seguinte, **Planeado — D.02 / A.03**:

1. Criar uma regressão sintética: falha do fornecedor → retoma → snapshot válido
   → conclusão rejeitada enquanto falta `doc_info` → inspeção válida → conclusão
   permitida quando os restantes requisitos estiverem satisfeitos.
2. Tornar o bloqueio explícito em `team_context` e na UI: papel afetado, tipo de
   erro e próxima leitura necessária. Distinguir `needsInspection` de falha CAD
   pendente e de falta de relatório de conclusão.
3. Melhorar o onboarding de recuperação e testar o encaminhamento entre revisor
   e coordenador. Não limpar erros só por terminar uma resposta; preservar
   `expected_revision`, validação e publicação atómica.
4. Verificar o comportamento com transporte simulado antes de repetir a tarefa
   real. Um novo runtime distribuído requer uma nova identidade sequencial;
   não substituir o binário publicado como 026D.

## Melhorias pedidas pelos agentes nesta criação

Estes pontos são feedback dos agentes, não correções já implementadas:

- **Finalização:** estado/controlo explícito que distinga erros de leitura
  recuperados de interrupções do fornecedor. É a prioridade deste diagnóstico.
- **Reservas de escrita:** recibo com instante de expiração e tratamento claro
  da libertação de uma reserva expirada. Não reutilizar tokens expirados.
- **Snapshots:** cores de aparência grafite eram reportadas como bege uniforme.
  Comparar renderer de snapshot com viewport num exemplo sintético antes de
  concluir que todos os caminhos de aparência falham.
- **Superfícies:** melhorar diagnóstico dos limites UV e das rejeições de trim
  por conservação de área. As falhas CAD locais confirmam rejeições de loft
  e trim; não relaxar tolerâncias para aceitar geometria inválida.
- **Preview:** o agente reportou uma proposta acima do cache de 128 MiB que
  precisou de recomputação no commit. Medir custo/cache com um caso reproduzível.

Limites do conceito registados: interfaces com sobreposição, fixações por
definir, envelope ainda sem engenharia acústica/eletrónica e BOM de conceito.
Não transformar estes limites em requisitos automáticos de fabrico para um
pedido apenas estético. Desenhos 2D continuam dependentes de pedido explícito.

## Teste local do modelo pequeno — assunto separado

A integração Ollama já descobre modelos instalados, consulta capacidades e
permite seleção global ou por agente. A verificação anterior do runtime passou
**129/129 testes locais**; ver o recibo. Isso não prova sucesso CAD de cada modelo.

Só `qwen3.5:2b` foi usado para inferência. As duas experiências scratch chegaram
ao limite de oito respostas do harness de teste, sem concluir autoria CAD.
No teste reduzido, duas escritas omitiram a operação nativa e foram rejeitadas;
o documento ficou na revisão 0. No teste com catálogo completo observaram-se
contexto alocado de 262144, 33293 tokens e `truncated=0` no servidor. Não há
evidência de truncagem nessa execução. O limite do harness não é um novo budget
dos agentes na aplicação.

Em casa, testar modelos maiores instalados, um de cada vez, com o mesmo brief
e os mesmos critérios. Registar sucessos e falhas, chamadas reais, latência e
validade geométrica; capacidade `tools` declarada não garante competência CAD.
Manter Focused por predefinição, sem fallback para cloud implícito.

## Verificação desta passagem de trabalho

Documento baseado em leitura do código, captura fornecida, inspeção MCP somente
de leitura e leitura SQLite do projeto guardado. Foram consultados os 113
registos de atividade, com zero feedback pendente, e as falhas CAD locais.
Não se executaram novos comandos de modelação, steers ou gravações do projeto.
Esta alteração é só documentação: não se repetiu a suite de runtime nem se
declarou o problema resolvido. A aceitação do utilizador e o merge continuam pendentes.
