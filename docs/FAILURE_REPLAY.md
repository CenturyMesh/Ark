# Rejeições locais e previews de ajuste — 008D

Escopo de A.02, B.03 e D.04: preservar a primeira rejeição real, voltar a
avaliar os mesmos comandos contra a mesma baseline e inspecionar pixels de
um candidato antes de qualquer commit. O replay não corrige geometria, não
altera tolerâncias e não modifica a revisão viva.

`failure_info{}` devolve resumos locais recentes. `failure_info{id}` acrescenta
o pedido original e os comandos/checks efetivamente rejeitados; a baseline
completa permanece privada. O histórico é limitado a oito registos e 16 MiB
por sessão. Quando um registo excede a capacidade, a rejeição original é
preservada no recibo e o limite da recolha é explicitado.

`failure_export{id,path}` grava, mediante pedido explícito, um bundle JSON
local com baseline `.ark`, revisão, comandos, checks, recibo da rejeição e
proveniência da versão da aplicação/kernel disponível.
O bundle contém desenho do utilizador e não pertence ao instalador, OTA,
corpus público ou commits. Exportar é a forma de manter a prova após restart.

`failure_replay{id,expected_revision}` reavalia um registo retido numa sessão
isolada. `failure_replay{path,expected_revision}` valida e reavalia um bundle
exportado. O resultado conserva a proveniência registada e mostra a versão
atual da aplicação/kernel; diferenças são reportadas sem bloquear o replay.
Uma versão de kernel ausente permanece desconhecida. `same_error` compara a
causa observada e não certifica igualdade de toolchain.
Abrir/listar/exportar nunca executa o pedido armazenado. A revisão
pedida refere-se ao documento vivo, que permanece inalterado; o replay usa
a revisão e o modelo congelados no registo e compara a causa observada.

Para um ajuste proposto, usar `doc_preview{expected_revision,commands,checks,
detail:"changes"}` para resumos parciais dos IDs alterados e evidência de ajuste.
O detalhe por omissão `design` conserva os inventários before/after existentes.
Um candidato válido fornece `candidate_id`; `snapshot{candidate_id,
expected_revision,id|node,view,label}` mostra o resultado não comprometido.
`snapshot{expected_revision,id|node,view,label:"before"}` mostra a baseline.
Os pixels identificam revisão, alvo e câmera; a autoridade continua a ser
o B-rep e as referências semânticas. Só `doc_apply{candidate_id,
expected_revision}` publica o candidato. Cache expirada ou baseline alterada
produz rejeição explícita, sem rebase.

O estado de fabrico, custo por design aceite, qualidade entre modelos e
qualificação de cortes/uniões fora dos testes observados permanecem por medir.

No Studio, **Manage > Last failure** abre a causa local e permite selecionar
**Inspect commands**, **Export bundle** ou **Replay isolated**. O inspetor de
comandos é de leitura; a exportação pede um caminho local explícito. Depois de
uma rejeição retida, o viewport também mostra **Inspect failed operation**.
Reproduzir conserva o desenho aberto e mostra a causa observada, `same_error`
e a evidência de isolamento; não repete automaticamente uma operação de fabrico.

**Model > Tube end fit** avalia primeiro um candidato com `detail:"changes"`.
Uma janela nativa mostra pixels PNG do candidato efetivamente avaliado e o
recibo geométrico, antes de apresentar **Apply** e **Cancel**. Apply publica o
mesmo candidato retido; Cancel deixa a revisão e as features originais intactas.
Uma revisão alterada ou cache expirada exige um novo preview. A imagem temporária
é criada na pasta local de previews do ARK e removida depois de ser carregada;
não contém uma publicação de um sólido parcial.

O formulário pede também **Placement scope**. **Selected occurrence** é a
predefinição quando a ocorrência original da definição fonte está selecionada;
altera somente a ligação `feature` desse node no mesmo batch que cria o ajuste.
**All source occurrences** é uma escolha explícita para todos os nodes que
referenciam essa definição. IDs, nomes, parents, transformações, estado fixed,
patterns e mates são conservados e novamente validados pelo preview.
Não é possível substituir isoladamente uma cópia de um pattern, nem escolher
uma ocorrência de outra definição por inferência.

Quando não existe uma ocorrência fonte selecionada, a predefinição é
**Definition only**. **New occurrence** cria explicitamente uma ocorrência na
raiz/origem da assembly e conserva as anteriores. A janela de revisão identifica
os IDs afetados e o âmbito escolhido. A definição fonte permanece no histórico
como input do ajuste. Os cálculos continuam no espaço das definições; esta UI
não converte silenciosamente posições da assembly para outro contrato de corte.

O teste local `--self-test-frame-tools-ui` cobre a janela de candidato, o commit
e o cancelamento; `--self-test-sketch-drawing-ui` cobre o inspetor de falha,
exportação local explícita e replay isolado. Capturas e resultados efetivamente
observados devem acompanhar qualquer declaração de verificação.
