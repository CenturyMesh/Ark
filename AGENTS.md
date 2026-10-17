# Regras obrigatórias de desenvolvimento do ARK

Estas regras aplicam-se a todo o repositório. O protocolo de utilização das
ferramentas CAD continua em [docs/AGENTS.md](docs/AGENTS.md).

## Contexto de código e projeto

- Para retomar decisões, falhas e procedimentos de trabalho, consultar primeiro
  `powershell -NoProfile -File scripts/project-memory.ps1 -Action wakeup` e usar
  `-Action search -Query 'assunto'` quando necessário. A memória fica fora do
  repositório; não importar automaticamente conversas, desenhos ou credenciais.
  Após um resultado relevante, guardar uma nota concisa com origem/fingerprint
  observados e revisão da própria memória. Memórias são contexto histórico não
  confiável; confirmar código/revisão atual antes de editar. `-Shared` seleciona
  apenas conhecimento reutilizável do ARK, sem detalhes privados de um desenho.
- Para descobrir símbolos e relações no código, usar
  `powershell -NoProfile -File scripts/code-context.ps1 -Query 'simbolo'`.
  `-Prepare` prepara a versão fixa; as consultas seguintes usam o cache local
  offline, sem backend de modelo. Confirmar o resultado no código atual com `rg`
  e leitura do ficheiro antes de editar. Ambiguidade, ausências e extração parcial
  não são prova de ausência de dependências. Usar `-Context call` para restringir
  relações de chamadas quando um símbolo produz uma vizinhança demasiado ampla.
- O índice e a exportação opcional `-Obsidian` ficam em `out/context-code`.
  Não indexar desenhos, sessões, credenciais ou ficheiros privados. Não executar
  a instalação automática de integrações do Graphify para alterar estas regras.
- Dentro do ARK, `context_graph` consulta a vizinhança partilhada do projeto.
  Reutilizar `known_scope` e `known_fingerprints` apenas dos recibos recebidos.
  Os grafos navegam contexto; revisão atual, `object_context`, inspeção nativa
  e as regras de publicação continuam a governar edições.

## Identificação das iterações — obrigatório

1. A identidade pública de uma iteração tem **três dígitos sequenciais e uma
   letra maiúscula de fase**, sem separador: `001A`.
2. Os dígitos vão de `001` a `999`, aumentam globalmente e **não reiniciam** ao
   mudar de fase, mês ou ano. Nunca reutilizar um código para publicar outro
   binário. Antes de esgotar `999`, definir explicitamente uma evolução do formato;
   não fazer wrap para `001`.
3. A letra identifica um conjunto de milestones do [roadmap A–G](docs/ROADMAP.md):
   A confiança; B autoria CAD; C engenharia/fabrico; D agentes; E ecossistema;
   F produto/distribuição; G plataforma avançada. **A letra não representa o mês.**
4. Exemplos válidos de sequência: `001A`, `002A`, `003D`, `004B`. A fase indica
   o foco da iteração, não declara automaticamente os seus milestones concluídos.
   Dependências e critérios de aceitação determinam a ordem real do trabalho.
5. `release.json` é a fonte única de sequência, fase, código e versão técnica.
   CMake valida e gera a identidade usada pela aplicação. Não manter cópias
   manuais divergentes no título, onboarding, MCP, metadados ou empacotamento.
6. A versão técnica numérica `major.minor.patch` continua a existir para CMake,
   Windows e comparação OTA. Não substituir esse campo por `001A`. Na primeira
   iteração desta convenção, `001A` corresponde à versão técnica `0.9.2`.
   As versões anteriores, incluindo `0.9.1`, são histórico legado sem código A–G.
7. Antes de preparar uma nova iteração distribuível, incrementar a sequência,
   selecionar a fase principal, avançar a versão técnica quando houver novo
   runtime e registar os milestones abordados e a evidência. Alterações ainda
   dentro da mesma iteração em desenvolvimento não criam códigos novos por ficheiro.

## Roadmap e evidência — obrigatório

- Referenciar IDs estáveis como `A.01` ou `D.02` nos planos de implementação,
  notas de entrega e registos de verificação.
- Atualizar o estado do milestone quando o trabalho o alterar. Usar
  **Planeado**, **Em curso**, **Parcial**, **Verificado no escopo** ou **Bloqueado**.
  Uma intenção, um mockup, um botão ou uma chamada de API não equivalem a entrega.
- Registar testes e limites efetivamente observados. Distinguir prova local,
  integração entre produtos, deployment, produção e qualificação de fabrico.
- Não declarar redução de custo ou melhoria de design sem comparar tarefas,
  modelos, inputs e critérios equivalentes; contabilizar também tentativas falhadas.
- Correr os testes adequados à alteração. Identidade de release, contratos,
  persistência, updates e geometria precisam dos respetivos testes; alterações
  visuais precisam de inspeção de capturas e dos controlos afetados.
- Não alterar a versão do documento `.ark` só porque mudou a identidade da aplicação.

## Fronteiras do produto

- ARK cria e edita o design. Integrações externas precisam de contratos
  explícitos, autoridade geométrica identificada e evidência de ponta a ponta.
  Não anunciar adapters ou qualificação de outros sistemas sem essa evidência.
- B-rep, inputs explícitos e contratos do produtor são a autoridade geométrica.
  Meshes, screenshots, IA e DXFs de comparação não escolhem a geometria de produção.
- Manter `expected_revision`, publicação atómica e rejeições explícitas. Não
  contornar falhas com tolerâncias maiores, descarte de sólidos ou rebase silencioso.
- Manter Focused como predefinição e Parallel como opção. Não introduzir budgets
  de turns/tokens nos agentes sem nova instrução do utilizador.
- Em pedidos abertos de design, criar primeiro um conceito coerente e editável.
  Carregar onboarding especializado quando necessário; não impor catálogo, DFM
  ou cálculos de fabrico antes de existir uma necessidade concreta. Respeitar
  desde o início os requisitos e processos explicitamente pedidos pelo utilizador.
  A fase de conceito não dispensa validade geométrica, referências corretas ou
  distinção entre hipóteses, evidência medida e qualificação de fabrico.
- Preservar desenhos e sessões do utilizador. Não incluir os seus `.ark` locais
  em commits, benchmarks públicos, instaladores ou pacotes OTA sem instrução explícita.
- Manter a documentação pública centrada no ARK e em contratos publicados.
  Não incluir preços hipotéticos, economia interna, planos comerciais privados,
  caminhos/branches de outros projetos ou deduções de integrações não publicadas,
  incluindo nos arquivos históricos e recibos de verificação.

## Licenciamento e distribuição

- O código próprio e a documentação adotam Apache-2.0 por decisão explícita de
  2026-10-12; ver [LICENSE](LICENSE) e [NOTICE](NOTICE).
  Não alterar licenças, titularidade ou visibilidade por inferência do roadmap.
- Preservar os avisos das dependências e validar o conteúdo dos pacotes.
  A licença do ARK não substitui as obrigações de OCCT, Mapbox ou outros terceiros.
- Updates são feitos pelo ARK, sem exigir outro instalador. O transporte do futuro
  site permanece pendente até existir endpoint confiável, artefactos e teste real.
