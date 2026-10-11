# Evidência visual dos agentes — 008D

Escopo D.04: o agente recebe pixels reais juntamente com a revisão, vista e
identificadores CAD disponíveis. Uma descrição ou um caminho de imagem não
substitui a entrega dos bytes ao modelo. A geometria continua a ser decidida
por B-rep, inputs explícitos e contratos do produtor.

Na aplicação, `snapshot` com `view:"viewport"` captura o viewport na thread da
interface, com resolução limitada a 1024×640. O recibo identifica a câmara,
seleção e estado não confirmado. As vistas nomeadas usam o renderizador do
documento. `candidate_id` permite observar um candidato retido de `doc_preview`
sem o publicar; a revisão e validade do candidato são verificadas.

Os conectores convertem os bytes PNG/JPEG para o formato visual do protocolo
selecionado. Só são aceites imagens inline válidas; não são descarregados URLs
ou ficheiros arbitrários indicados por resultados de ferramentas. A retenção
dos pixels é limitada às duas mensagens visuais mais recentes; metadados
anteriores permanecem e o agente pode pedir uma captura atualizada.

O modelo escolhido tem de suportar visão. Os testes de connector verificam os
envelopes de transporte; o ensaio real local em Ollama verificou que os pixels
chegaram a uma interação concluída. Isso não certifica reconhecimento perfeito:
o modelo também propôs detalhes ausentes da receita. Ver
[benchmark e resultados observados](AGENT_BENCHMARK.md).

Para rever uma alteração, inspecionar primeiro os objetos relevantes, obter a
imagem antes/depois ou do candidato e confirmar o resultado com ferramentas
nativas. Inferências visuais não substituem medidas, referências ou validação
de sólidos. O feedback visível dos sketches apresenta os comandos realmente
recebidos; não inventa etapas para simular progresso.
