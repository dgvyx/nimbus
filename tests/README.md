# Testes de PC (HTML, layout e HTTP)

Windows: `powershell -ExecutionPolicy Bypass -File tests/run.ps1`

Linux: `sh tests/run.sh`

Requer GCC e `nm`. Os testes executam no PC e compilam os módulos com
`-m32 -ffreestanding -fno-builtin` em O0/O2, verificando símbolos externos.
Somente os programas de teste usam a biblioteca padrão do host.

`tests/test_http.c` compila `kernel/http.c` com `-DHTTP_PARSER_ONLY` e cobre
`http_parse_url`, `http_parse_ipv4`, `http_find_body`, `http_parse_status`,
`http_redirect_http` e `http_build_request`, sem abrir soquetes. O teste de
integração com a imagem real (`nimbus.img`, QEMU/SLIRP) é
`tests/qemu_tcptest.py --command "get http://10.0.2.2:8000/" --expect "Nimbus TCP OK"`.

O parser retorna uma árvore de fatias do HTML original. O layout decodifica
texto e URLs para armazenamento próprio e gera caixas em pixels, com métricas
fixas (16 px por caractere normal; títulos 32/28/24/20/18/16).
Palavras grandes e links quebram em caracteres; UTF-8 não é dividido no meio.
Espaços colapsam fora de `pre`; em `pre`, espaços, tabulações (4 meios-espaços)
e quebras de linha são preservados. Um glifo maior que a largura tem sua caixa
limitada à largura disponível. `layout_render` é apenas o ponto de integração;
o resultado público é a lista de itens.

Não há alocação dinâmica nos módulos. Use estruturas estáticas ou memória
adequada: parser e layout são grandes para uma pilha de kernel. Limites:
512 nós, profundidade 32, 1024 itens e 16 KiB de texto de layout. Ao esgotar
capacidade, a saída é parcial. Entidades básicas/números 1..255 produzem um
byte; entidades desconhecidas permanecem literais e `nbsp` vira espaço.
Não é uma implementação completa do algoritmo HTML5/CSS.
