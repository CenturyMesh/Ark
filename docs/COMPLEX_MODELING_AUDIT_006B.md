# Contexto de objetos, mecanismos e diagnóstico — 006B

Escopo: **B.01/B.04, C.02, D.02/D.04**. Comparação local com `005B`.

## Erro de múltiplos sólidos

A captura mostra uma operação rejeitada com sete sólidos e volume aproximado
de 1 333 870 mm³. A consulta de leitura do modelo aberto encontrou uma feature
guardada `#193` de corte por plano e uma estrutura composta `#186` com três
membros e quatro cordões. A inspeção nativa desta estrutura confirmou sete
sólidos e 685 671,52 mm³: **não comprova ser a operação da captura**. Uma tentativa
rejeitada atomicamente não tem necessariamente a definição que ficou guardada
com o mesmo número. Não foi recuperado o pedido exato que falhou.

O diagnóstico passa a indicar a operação e o número de sólidos dos seus
operandos. A descoberta de ajuste de tubos expõe linhagem, extremidades retas,
estado das referências das tampas e faces planas dos hosts. Isto permite escolher
a operação com base nos inputs reais. Não descarta sólidos, aumenta tolerâncias,
converte uma estrutura composta numa peça fundida nem declara a junta viável
apenas pela existência de uma extremidade candidata.

## Movimento e limites

O controlo de arrasto de `005B` selecionava e aplicava sempre rotação. A nova
iteração acrescenta o percurso de translação, mates prismáticos e limites
persistentes por expressões. O cenário de aceitação inclui um corpo fixo, haste,
peça solidária e curso limitado, além de uma junta revoluta limitada.

Este é um modelo cinemático rígido ideal. Não constitui um solver geral de
mecanismos fechados, contacto, colisão ou forças. Limites revolutos identificam
um intervalo inferior a 360°, incluindo intervalos que atravessam 180°;
contagem de voltas acumuladas não é inferida da orientação.

## Técnica de contexto escolhida

IDs persistentes identificam definições e ocorrências. O contexto seletivo junta
receita paramétrica, dependências, recursos de autoria, mates e evidência medida.
Fingerprints canónicos detetam mudanças nesse conteúdo e permitem omitir corpos
de resposta inalterados. Continuam obrigatórios a revisão observada e as
referências semânticas na edição. O hash não prova identidade geométrica,
segurança criptográfica ou qualidade de fabrico.

Uma imagem ajuda a interpretar aparência e intenção visual, mas não oferece
IDs, parâmetros ou relações mecânicas fiáveis. Base64 é apenas a codificação dos
bytes da imagem. Nesta iteração o contexto estruturado é a base do pedido;
o conector interno continua a receber caminhos/metadados de snapshots, não
pixels automaticamente. Não se anuncia compreensão multimodal que não foi
integrada e testada.

O pedido do menu **Ask Agent** fixa o objeto escolhido, permite escrever a tarefa
e só a envia por ação explícita. O contexto é consultado novamente no envio;
outro documento ou alvo eliminado não são substituídos silenciosamente.

As medições de payload e os testes estão no [recibo da iteração](verification-006B.json).
Na ocorrência do corpo do amortecedor traseiro, a resposta de contexto ocupou
2 272 bytes, ou 977 bytes quando inalterada, contra 143 543 bytes do inventário
de design completo. O modelo e a revisão ficaram inalterados. Os tempos locais
de consulta foram 4,9 ms e 4,6 ms, respetivamente.
Redução do tamanho de uma resposta não prova redução de custo, latência do
fornecedor ou melhoria da qualidade do design.
