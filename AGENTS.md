# Regras obrigatórias de desenvolvimento do ARK

Estas regras aplicam-se a todo o repositório. O protocolo de utilização das
ferramentas CAD continua em [docs/AGENTS.md](docs/AGENTS.md).

## Identificação das iterações — obrigatório

1. A identidade pública de uma iteração tem **três dígitos sequenciais e uma
   letra maiúscula de fase**, sem separador: `001A`.
2. Os dígitos vão de `001` a `999`, aumentam globalmente e **não reiniciam** ao
   mudar de fase, mês ou ano. Nunca reutilizar um código para publicar outro
   binário. Antes de esgotar `999`, definir explicitamente uma evolução do formato;
   não fazer wrap para `001`.
3. A letra identifica um conjunto de milestones do [roadmap A–G](docs/ROADMAP.md):
   A confiança; B autoria CAD; C engenharia/fabrico; D agentes; E ecossistema;
   F produto/comercialização; G plataforma avançada. **A letra não representa o mês.**
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

- ARK cria e edita o design. STAGE é o produtor de análise/desenvolvimento no
  domínio que conseguir comprovar. Trace consome contornos e posiciona peças.
  MeshWorks gere a estrutura e o processo industrial. Não duplicar silenciosamente
  os seus motores nem anunciar adapters que ainda não existem.
- B-rep, inputs explícitos e contratos do produtor são a autoridade geométrica.
  Meshes, screenshots, IA e DXFs de comparação não escolhem a geometria de produção.
- Manter `expected_revision`, publicação atómica e rejeições explícitas. Não
  contornar falhas com tolerâncias maiores, descarte de sólidos ou rebase silencioso.
- Manter Focused como predefinição e Parallel como opção. Não introduzir budgets
  de turns/tokens nos agentes sem nova instrução do utilizador.
- Preservar desenhos e sessões do utilizador. Não incluir os seus `.ark` locais
  em commits, benchmarks públicos, instaladores ou pacotes OTA sem instrução explícita.

## Licenciamento e distribuição

- [A estratégia comercial](docs/COMMERCIAL_STRATEGY.md) é uma proposta, não uma
  licença adotada. Não publicar o código, mudar a visibilidade do repositório ou
  adicionar uma licença ao código próprio por inferência a partir do roadmap.
- Preservar os avisos das dependências e validar o conteúdo dos pacotes.
  A licença do ARK não substitui as obrigações de OCCT, Mapbox ou outros terceiros.
- Updates são feitos pelo ARK, sem exigir outro instalador. O transporte do futuro
  site permanece pendente até existir endpoint confiável, artefactos e teste real.
