# ARK — open source e licença perpétua de 200 €

> **Historical proposal, superseded on 2026-10-12:** the project owner selected
> Apache-2.0 for ARK and authorized public source publication after verification.
> See [LICENSE](../LICENSE). The closed-source launch recommendation and the
> proposed 200 € offer below are retained as historical analysis, not current
> licensing terms or an active sale. Enterprise products/services remain possible;
> no Enterprise edition or billing capability is declared delivered by this decision.

**Análise de 10 de outubro de 2026.** O utilizador confirmou **200 € por licença
perpétua**. Este documento compara opções; não adota uma licença, não define
termos de venda e não autoriza a publicação do repositório.

## Recomendação

Começar com **ARK comercial fechado, acessível por uma compra perpétua**, e
preparar a abertura da especificação `.ark`, dos protocolos e de um SDK separado.
Reavaliar a abertura da aplicação depois de um piloto comercial e da auditoria
de titularidade/dependências. É uma escolha recomendada, ainda não executada.

Esta opção é mais coerente se a intenção for que cada cliente comercial compre
uma licença. Abrir todo o ARK pode ser uma boa estratégia de adoção e confiança,
mas pressupõe aceitar utilização comercial gratuita e forks conformes. Cobrar
por builds oficiais, serviços ou exceções de licença continua possível.
[Definição de open source, OSI](https://opensource.org/osd),
[FAQ OSI](https://opensource.org/faq).

**O preço baixo não impede uma reimplementação.** Pode tornar a compra mais
conveniente, mas concorrentes também podem copiar funcionalidades ou reutilizar
código permitido pela licença. A vantagem defensável deve resultar de geometria
fiável, corpus de regressão, compatibilidade de documentos, UX, marca, suporte e
integrações industriais que funcionem com dados reais.

## Opções comparadas

| Opção | O que permite | Como se cobra | Adequação ao objetivo atual |
|---|---|---|---|
| Aplicação proprietária; formatos/SDK abertos | Interoperabilidade e extensões sem publicar toda a implementação | Licença ARK, atualizações elegíveis e serviços | **Recomendada para o primeiro piloto de 200 €**; licenças das partes abertas ainda por escolher |
| ARK GPLv3 + alternativa comercial | Comunidade e forks copyleft; uso empresarial interno gratuito | Build/serviço oficial ou permissão proprietária adicional para casos que a precisem | Viável se aceitarmos que a licença paga não é obrigatória para todo o uso comercial |
| ARK AGPLv3 + alternativa comercial | Copyleft com obrigação adicional em versões modificadas usadas por rede | Serviços e permissões proprietárias adicionais | Considerar se surgir CAD hospedado; não escolher só para monetizar o desktop |
| ARK permissivo, por exemplo Apache-2.0/MIT | Reutilização ampla, incluindo por concorrentes proprietários | Distribuição, suporte, cloud e integrações | Forte para adoção; fraco se a prioridade for dificultar cópias fechadas |
| Código visível com uso comercial restrito | Direitos definidos por licença própria | Licença por utilização | Pode servir objetivos comerciais, mas deve chamar-se **source available**, não open source |

A GPL admite uso interno nas empresas e forks comerciais que cumpram a licença.
Os desenhos criados pelo utilizador não se tornam GPL apenas por serem produzidos
na aplicação. Para vender exceções proprietárias ao código próprio, é necessário
ter os direitos relevantes, incluindo sobre contribuições aceites. A política de
contribuições deve tratar isso antes de receber código externo.
[GNU: uso interno](https://www.gnu.org/licenses/gpl-faq.en.html#InternalDistribution),
[GNU: outputs](https://www.gnu.org/licenses/gpl-faq.en.html#GPLOutput),
[GNU: contribuições](https://www.gnu.org/licenses/gpl-faq.en.html#Consider),
[GNU: venda de exceções](https://www.gnu.org/philosophy/selling-exceptions.html).

A AGPL acrescenta a oferta do código correspondente a utilizadores que interagem
remotamente com uma versão modificada; não cria uma taxa obrigatória por empresa.
[AGPL, secção 13](https://opensource.org/license/agpl-3-0).

## O que os 200 € devem comprar

Proposta para validar com clientes, sem a apresentar já como termos definitivos:

- Direito sem expiração de usar a versão adquirida e as versões incluídas na compra.
- Modelação CAD local, documentos locais e exportações essenciais sem subscrição obrigatória.
- Uma política simples de utilizador/dispositivos e transferência de máquina, com
  funcionamento offline e recuperação de ativação. Evitar DRM que bloqueie desenhos.
- Um período explícito de novas funcionalidades, por exemplo **12 meses**, após o
  qual upgrades continuam opcionais; a versão licenciada continua a funcionar.
- Suporte inicial delimitado, documentação e comunidade. Suporte especializado,
  formação e integração empresarial são serviços separados.
- Conta/chave de IA do cliente ou consumo cobrado separadamente. Não incluir
  créditos ilimitados numa compra de 200 €.

“Perpétua” não deve ser apresentada como “todas as versões futuras e suporte humano
ilimitado”. Também não permite excluir atualizações necessárias por lei: na venda
a consumidores da UE, conformidade e segurança têm obrigações próprias.
Os termos finais precisam de revisão jurídica/fiscal antes da venda.
[Diretiva 2019/770, artigo 8](https://eur-lex.europa.eu/legal-content/EN/TXT/PDF/?uri=CELEX%3A32019L0770).

Decisões a fechar: 200 € com ou sem IVA, licença nominativa ou concorrente, número
de dispositivos, período de novas funcionalidades, suporte incluído, duração de
suporte das versões e política de upgrades. Os 12 meses acima são uma proposta,
não uma alteração à instrução de licença perpétua.

## Referências de mercado

Valores observados nas páginas oficiais em 10-10-2026. Planos, impostos, regiões
e capacidades diferem; a comparação não demonstra paridade funcional do ARK.

| Produto | Valor observado | Condições da referência |
|---|---:|---|
| FreeCAD | Gratuito | Funcionalidades não são vendidas; donativos opcionais. [Fonte](https://www.freecad.org/sponsor.php?lang=en) |
| Rhino 8, Europa | 995 € | Permanente, um utilizador concorrente, impostos excluídos; suporte e service releases da versão atual incluídos. [Fonte](https://www.rhino3d.com/sales/europe/) |
| Autodesk Fusion, loja portuguesa | 714 €/ano | Subscrição anual, IVA estimado incluído na página consultada. Promoções podem mudar. [Fonte](https://www.autodesk.com/pt/products?mktvar002=selfservefindproduct) |
| Onshape Standard | US$ 1 500/utilizador/ano | Faturação anual; preço regional em euros não confirmado nesta análise. [Fonte](https://www.onshape.com/en/pricing) |

A existência de um CAD gratuito significa que o ARK precisa de vender conveniência,
qualidade e integração. O preço de 200 € é uma hipótese atraente de entrada, ainda
sem prova de disposição a pagar ou de retenção no nosso mercado.

## Economia de uma licença perpétua

Exemplo **hipotético**, não previsão nem custos observados da empresa. Assume
200 € de receita líquida de impostos por licença, para tornar a conta explícita:

| Item | Hipótese por licença |
|---|---:|
| Receita | 200 € |
| Pagamento/operacional variável | −6 € |
| Distribuição/updates provisionados | −6 € |
| 1 hora de suporte a 30 €/h | −30 € |
| Contribuição antes de desenvolvimento, aquisição de clientes e custos fixos | **158 €** |

Com quatro horas de suporte, essa contribuição cairia para 68 €. A 158 €, cobrir
10 000 € de custos fixos mensais exigiria cerca de **64 novas licenças por mês**,
antes de outros custos. Se os 200 € incluírem impostos, a receita disponível será
menor. Substituir estas hipóteses pelos dados do piloto antes de fixar o modelo.

Medir: conversão, custo de aquisição, horas de suporte, ativações por licença,
pedidos de reembolso, custo de updates e procura de upgrades opcionais. Não usar
o valor das licenças perpétuas como se fosse receita recorrente garantida.

## Relação com MeshWorks, STAGE e Trace

O ARK pode ser a entrada acessível para o ecossistema. Os 200 € não devem incluir
automaticamente infraestrutura cloud, nesting intensivo, serviços STAGE ilimitados
ou todos os módulos MeshWorks. Os preços e direitos desses produtos mantêm decisões
próprias; este roadmap não os redefine.

- **ARK:** design e edição local; documentos portáveis e workflow de agentes.
- **STAGE:** análise/desenvolvimento qualificado; integração local ou serviço com contrato próprio.
- **Trace:** nesting, stock/remanescentes e packages; licença/serviço a definir separadamente.
- **MeshWorks:** colaboração industrial, BOM, compras, custos e produção.

O adapter básico pode reduzir fricção comercial; operações pagas e efeitos como
consumir stock ou libertar compras precisam de direitos e ações explícitas. A
integração deve continuar a funcionar sem tornar o ficheiro do cliente refém do SaaS.

## Auditoria de dependências e direitos

| Componente | Evidência atual | Trabalho necessário antes de distribuição comercial geral |
|---|---|---|
| Código próprio ARK | Não havia LICENSE/COPYING principal adotada na raiz na auditoria | Confirmar titularidade, contribuições e termos do produto; não presumir open source por ausência de licença |
| OCCT 8.0.1 | DLLs; LGPL 2.1 com exceção para material dos headers | Avisos, fontes correspondentes, instruções e caminho efetivo para bibliotecas modificadas; opção comercial disponível sob consulta. [Documentação oficial](https://occt3d.com/dev/doc/overview/html/index.html) |
| Mapbox earcut 2.2.4 | ISC em `third_party/mapbox/LICENSE` | Preservar aviso no pacote; correção incluída na preparação de `001A`. [Licença oficial](https://raw.githubusercontent.com/mapbox/earcut.hpp/v2.2.4/LICENSE) |
| SQLite | Domínio público | Conservar proveniência e identificar versão no SBOM. [Fonte oficial](https://www.sqlite.org/copyright.html) |
| Runtime Visual C++ | Redistribuíveis provenientes do toolchain | Confirmar direito e componentes redistribuíveis aplicáveis. [Microsoft](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170) |
| Ícones, texturas, fontes e biblioteca de peças | Origens e direitos variam por asset/fornecedor | Inventário completo, permissões de distribuição e atribuições; conteúdo de cliente não entra no pacote por defeito |

Uma aplicação proprietária sobre OCCT é possível sob as condições aplicáveis da
LGPL; não se deve declarar cumprimento integral apenas por usar DLLs. Existe
licenciamento comercial alternativo do fornecedor, sem preço público nesta análise.
[Documentação OCCT](https://occt3d.com/dev/doc/overview/html/index.html).

O self-update protege a árvore oficial com hashes. O caminho `ARK_OCCT_WORKER`,
já existente em `src/step_export.cpp`, permite apontar para um worker externo.
É necessário documentar e testar a utilização de um worker/bibliotecas modificados
fora da instalação verificada e fornecer o material de fontes exigido. A auditoria
de direitos, relinking/modificação e conteúdos **não está concluída**.

## Decisão proposta e próximos passos

1. Usar o piloto F.05 para testar a proposta de 200 € perpétuos, sem subsídio oculto de IA.
2. Fechar A.05: titularidade, dependências, fontes/avisos, pacote e caminho de modificação OCCT.
3. Publicar primeiro documentação de contratos e preparar SDK/formato abertos;
   escolher a licença dessas partes antes de as distribuir.
4. Só depois decidir entre aplicação proprietária e GPL/comercial, incluindo a
   política de contribuições e a aceitação explícita de forks/uso comercial gratuito.
5. Ligar self-update e compra/ativação ao futuro site, mantendo os desenhos locais e exportáveis.

Nada nesta análise tornou o repositório público, adotou uma licença do código
próprio, alterou preços de outros produtos ou ativou cobrança.
