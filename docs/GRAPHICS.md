# Gráficos do Studio — B.01 / A.04

O viewport é uma apresentação do modelo. B-reps OCCT, inputs explícitos,
referências semânticas e revisões conservam a autoridade geométrica. Materiais,
luzes e imagens não qualificam fabrico nem modificam o resultado do kernel.

## Opções gráficas

O modo normal continua a ser rasterização. A opção de path tracing encontra-se
em **System options / Graphics**, desligada por defeito. A mudança de modo não
exige reiniciar a aplicação. Os efeitos de apresentação não alteram a versão
ou geometria do documento `.ark`.

O path tracer usa shaders OpenGL, interseções de raios e uma hierarquia BVH da
tesselação de visualização. A imagem acumula amostras progressivamente; a GPU
não altera ou reconstrói os B-reps. Este backend não usa RT cores, DXR, OptiX
ou Vulkan. O equipamento de verificação local é uma RTX 4080 SUPER com 16 GB,
driver 617.14; os resultados não qualificam automaticamente outras placas.

[OptiX](https://developer.nvidia.com/designworks/optix/download) e as
[extensões de ray tracing Vulkan](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)
oferecem caminhos distintos para aceleração por hardware. A existência da GPU
e do driver não significa que um desses backends esteja integrado no ARK.

## Cache de programas GPU

Quando o driver suporta `GL_ARB_get_program_binary` ou OpenGL 4.1+, o Studio
pode guardar programas compilados em `%LOCALAPPDATA%/ARK/cache/shaders-v1`.
A identidade inclui fontes GLSL, versão da aplicação, fabricante, GPU e versões
OpenGL/GLSL do driver. Cada payload tem um SHA-256 e o formato tem de ser
anunciado pelo driver atual; uma entrada rejeitada volta à compilação GLSL.

O cache não contém desenhos, materiais do utilizador ou credenciais. Uma
entrada fica limitada a 16 MiB e novas escritas param quando a pasta atinge
128 MiB ou 512 entradas. A publicação usa um ficheiro temporário e substituição
atómica; falhas de cache não impedem a compilação normal. Os testes headless
não usam o cache pessoal: `ARK_SHADER_CACHE_DIR` seleciona uma pasta isolada.

Hits/misses comprovam reutilização, não uma redução geral de tempo de arranque.
Uma comparação de desempenho tem de controlar driver, fontes, resolução,
modelo, estado do cache e carga de GPU. Ver a
[semântica oficial de program binaries](https://wikis.khronos.org/opengl/GlProgramBinary).

## Arranque e informação do produto

O splash apresenta a identidade gerada e **Created by MeshSphere**. A animação
indica atividade, sem inventar percentagens de progresso. Fecha quando o
primeiro frame fica pronto ou ao atingir o limite de 3 segundos, sem uma espera
mínima artificial. Um timeout do splash não certifica que o arranque acabou.

**About ARK** / `F1` apresenta MeshSphere, **Bruno Santos — Project lead**,
versão, stack, guia e [avisos de terceiros](THIRD_PARTY_NOTICES.md). A licença
do código próprio e a eventual publicação open source continuam por decidir.

Os resultados medidos e limites da implementação estão no
[registo da 009B](iterations/009B.md).
