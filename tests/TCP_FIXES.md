# Correção do TCP (SYN sem SYN-ACK)

Arquivos alterados: `kernel/net.c`, `kernel/net.h` e `tests/`.
`tools/`, `docs/` e demais fontes do kernel não foram modificados.

## 1. Suspeitas encontradas antes das mudanças

Linhas da versão original de `kernel/net.c`:

| Linhas | Problema |
|---|---|
| 413-414, 439-447, 471 | IPv4 escrito em `packet[0]`, TCP em `packet[34]`, mas `ethernet()` recebia `packet + 14`. O quadro saía com o cabeçalho IP deslocado; o SLIRP descarta em silêncio. **Causa raiz.** |
| 420, 426-431 | Data offset `0x50` (20 bytes) anunciado com 24 bytes escritos (opção MSS). |
| 426-431 | MSS era emitido em todo segmento, não só no SYN. |
| 433-437, 537 | Payload sem limite: até 64 KiB copiados para buffer de 1500 bytes (segmentação ausente). |
| 535-537, 608-615, 674-679, 783-792 | Confusão entre ACK absoluto e índice no buffer; janela local em vez da janela anunciada pelo par; `seq` não avançava com os dados. |
| 454-460, 515-517 | ARP usava sempre o IP remoto; destinos fora da sub-rede não eram encaminhados ao gateway. |
| 278-279 | Resposta ARP só era aceita com `net.busy`; `tcp_connect` não ativa essa flag, então o MAC podia nunca ser resolvido. |
| 269-308 | `receive()` não despachava protocolo 6 para `tcp_receive`. |
| 310-345 | `net_poll()` não chamava `tcp_poll()`. |
| 471-473 | Falha de transmissão ignorada; `tx_packets` incrementado duas vezes. |
| 500 | ISN fixo `0x12345678`. |
| 542-550 | `maxlen` negativo em `tcp_recv` podia gerar cópia enorme. |
| 554-563, 619-635, 697-738 | FIN não contabilizava dados do mesmo segmento e havia caminhos que liberavam a conexão sem enviar FIN/ACK corretamente. |
| 572-636, 638-761 | Handlers duplicados e retransmissão dentro do processamento de recepção. |
| 646-649, 797-817 | Sem validação de comprimento do cabeçalho TCP, checksum TCP, limites do IPv4 e fragmentação. |
| 713-715 | RST não era tratado em `SYN_SENT`. |
| 771-773 | `last_send == 0` usado como sentinela (zero é um instante válido). |
| 841 | Chave extra: `tcp_poll`/`tcp_receive` ficavam aninhadas, não eram símbolos globais. |

Conferidos e corretos: `put16/put32` (big-endian), checksum IP/TCP (pseudo-cabeçalho,
carry, complemento, byte ímpar), protocolo 6, TTL 64, SYN com `0x02` apenas.

## 2. Correções aplicadas e testes (uma de cada vez)

1. **Checksum TCP reutilizável** — `tcp_checksum()` em `kernel/net.c`/`net.h`,
   compilável isoladamente com `-DNET_CHECKSUM_ONLY`.
   Teste: `tests/test_tcp_checksum.c` + vetor independente `tests/tcp_vector.py`
   (SYN de 24 bytes = `0x3829`, 25 bytes = `0xe027`). Compila e passa com gcc comum.
2. **Posicionamento do pacote** — IP em `packet[0]`, TCP em `packet+20`, envio de
   `packet` (o Ethernet prefixa 14 bytes). Teste: `tests/test_tcp_packet.c`.
3. **Data offset/MSS** — `0x60` no SYN com MSS; `0x50` nos demais; MSS só no SYN.
4. **Chaves/duplicidade** — fim do aninhamento de `tcp_poll`/`tcp_receive`.
5. **ARP e entrada TCP** — resposta ARP aceita sem `net.busy`; `receive()` chama
   `tcp_receive`; `net_poll()` chama `tcp_poll`; destinos externos usam o MAC do
   gateway (`tcp_next_hop`).
6. **Envio/ACK** — segmentos limitados a MSS, janela do par respeitada, seq/ack
   relativos corretos, retransmissão só em `tcp_poll`, estado `sent` próprio.
7. **Recepção** — validação de IPv4/TCP (comprimento, checksum, offset), dados e
   FIN contabilizados juntos, RST válido, fechamento FIN_WAIT_1/2, CLOSING,
   LAST_ACK e TIME_WAIT.

## 3. Validação

- `powershell -ExecutionPolicy Bypass -File tests/run_tcp.ps1`
- Resultado: checksums, SYN, handshake, GET, ACK, resposta e fechamento passam.
- Compilação `gcc -m32 -ffreestanding` de `net.c` em O0/O2 sem símbolos externos novos.
- **QEMU real (SLIRP)**: `tests/qemu_run.ps1` compila um kernel multiboot de teste
  em `tests/` (Zig/lld), sobe um servidor HTTP em 127.0.0.1:8000 e captura
  `tests/rede.pcap`. Saída serial:

  ```
  established
  sent GET
  HTTP/1.0 200 OK
  ...
  RESULT PASS
  valid SYN: 49152->8000 seq=305419896 window=65535 mss=1460 mac=52:55:0a:00:02:02
  ```

  `tests/verify_tcp.py tests/rede.pcap` valida os 11 quadros TCP (IP e TCP
  checksums, offsets, SYN=1, SYN-ACK=1).

### Sobre `tools/tcp_pcap_check.py`

O script já foi corrigido: passou a usar o IHL real (`total_len - ihl*4`), limita
o payload IP ao total length, aceita o preenchimento Ethernet e espera
complemento zero no checksum TCP. `tests/rede.pcap` e `tests/tcptest.pcap`
retornam `✅ TODOS OS CAMPOS VÁLIDOS`; o histórico do diagnóstico errado está em
`docs/tcp-debug.md`.

## 4. Comando `tcptest` no Terminal

`kernel/ui.c` implementa `tcptest <ip> <porta>`:

- valida IPv4 e porta (0..65535) e mostra `uso: tcptest <ip> <porta>`;
- usa a API real de `kernel/net.h`: `tcp_connect(ip, porta, 0)`, `tcp_state`,
  `tcp_send`, `tcp_recv` e `tcp_close`;
- envia `GET / HTTP/1.0\r\n\r\n` e imprime até 500 bytes da resposta; `CR` é
  descartado, `LF`/`TAB` são preservados e os demais bytes não imprimíveis viram
  `.`;
- como o kernel é single-thread, chama `net_poll()` (que aciona `tcp_poll()`) nos
  laços de espera, com limite de 4 s para conectar e 5 s para a resposta;
- mensagens de erro: sem placa, sem link, sem ARP/SYN-ACK, timeout, conexão
  recusada por RST, link caiu e conexão encerrada sem resposta.

Validação na imagem real (`nimbus.img` bootável, QEMU + SLIRP + HTTP local):
`tests/qemu_tcptest.py` sobe `http.server` em `127.0.0.1:8000`, digita o comando
no Terminal via QMP e lê o buffer `tbuf` por `pmemsave`. Saída:

```
nimbus$ tcptest 10.0.2.2 8000
tcptest: conectando a 10.0.2.2:8000 ...
tcptest: conectado, enviando GET ...
tcptest: resposta:
HTTP/1.0 200 OK
...
<!doctype html>
<html><body><h1>Nimbus TCP OK</h1></body></html>
tcptest: 246 bytes recebidos; conexao encerrada
```

`tests/verify_tcp.py tests/tcptest.pcap` valida os 13 quadros (handshake, GET,
resposta e FIN/ACK). Casos de erro também exercitados: RST imediato (servidor que
aceita e aborta com `SO_LINGER 0`), destino inalcançável e IP inválido.

## 5. Cliente HTTP e comando `get`

`kernel/http.h`/`kernel/http.c` implementam um cliente HTTP/1.0 mínimo sobre a
pilha TCP existente:

- `http_parse_url` aceita `http://host[:porta]/caminho` (esquema sem diferenciar
  maiúsculas, porta padrão 80, caminho padrão `/`, fragmento removido) e rejeita
  IPv6, espaço/controle, porta inválida e host/caminho que não caibam nos buffers.
- `http_parse_ipv4` distingue IPv4 válido (`1`), nome DNS (`0`) e IPv4 malformado
  (`-1`). Nomes passam por `net_resolve()` e o cliente espera `net.resolved`.
- O pedido é `GET <caminho> HTTP/1.0`, com `Host` (e `:porta` quando não for 80) e
  `Connection: close`; a resposta é lida até o servidor fechar a conexão.
- `http_find_body` separa cabeçalho e corpo por `\r\n\r\n` (fallback `\n\n`) e o
  acumulado é limitado a `HTTP_MAX_RESPONSE` (256 KB), alocado com o `kmalloc`
  que já existia em `kernel/mem.c` (não foi preciso criar alocador nem `memtest`).
- 301/302 só são seguidos quando `Location` começa com `http://`, até 3 saltos;
  caso contrário o status e o corpo são entregues ao chamador. Timeouts: 5 s no
  DNS, 5 s para conectar e 10 s de recepção.

O Terminal ganhou `get <url>` (`kernel/ui.c`). O laço bloqueante chama
`net_poll()` e `http_poll()`, imprime `get: HTTP <status>, <n> bytes` e no máximo
4 KiB do corpo; se o corpo for maior, imprime `(truncado, N bytes no total)`.
`CR` é descartado e bytes não imprimíveis viram `.`.

Testes automáticos no PC: `tests/test_http.c` cobre URL/host/porta/caminho,
IPv4, separação cabeçalho/corpo, status, `Location` e montagem do request. Ele
roda com `-DHTTP_PARSER_ONLY` (sem depender de rede) e está incluído em
`tests/run.ps1` e `tests/run.sh`, junto com a checagem 32-bit sem símbolos
externos.

Validação na `nimbus.img` real com `tests/qemu_tcptest.py`:

```
nimbus$ get http://10.0.2.2:8000/
get: http://10.0.2.2:8000/
get: HTTP 200, 65 bytes
<!doctype html>
<html><body><h1>Nimbus TCP OK</h1></body></html>
```

O pcap `tests/http.pcap` contém exatamente
`GET / HTTP/1.0\r\nHost: 10.0.2.2:8000\r\nConnection: close\r\n\r\n` e foi validado
por `tools/tcp_pcap_check.py` e `tests/verify_tcp.py` (13 quadros, SYN=1,
SYN-ACK=1). Também foram exercitados:

- `/redirect` (302 com `Location: http://10.0.2.2:8000/`): o segundo GET aparece
  em `tests/http_redirect.pcap` e o Terminal mostra a página final;
- `/redirect-https` (301 com `Location: https://...`): não é seguido, Terminal
  mostra `get: HTTP 301, 0 bytes`;
- `get https://...`: `get: URL invalida (use http://host[:porta]/caminho)`.

Testes adicionais de limite e redirecionamento:

- `/r0` → `/r1` → `/r2` → `/`: três redirecionamentos encadeados. O pcap
  `tests/http_redirect3.pcap` tem 4 conexões (portas 1024–1027), 4 SYN, 4
  SYN-ACK, 46 quadros, sem RST, e o Terminal imprime a página final. Os 4 slots
  de TCP bastam porque cada conexão anterior já foi liberada.
- `/large`: corpo de 8192 bytes. O Terminal imprime exatamente 4096 bytes
  (42 linhas) e depois `(truncado, 8192 bytes no total)`; pcap
  `tests/http_large.pcap` com 20 quadros válidos.

### Heap × wallpaper

O heap em `kernel/mem.c` vai de `0x200000` a `0xA00000` (8 MiB), mas o wallpaper
fica em `0x400000` e o `scene` em `0x800000`. Com 1280×720×4 bytes (0x384000),
há sobreposição de 0x384000 bytes com o wallpaper e 0x200000 bytes com o scene.
O `get` não é afetado porque seu buffer de 256 KiB fica no início do heap
(~0x20000c–0x24000b), mas a definição precisa ser corrigida. Ajuste proposto
(sem aplicar): `HEAP_SIZE (2 * 1024 * 1024)`, terminando em `0x400000`, antes do
wallpaper.

### Build neste ambiente

WSL2 não está disponível nesta máquina (sem distribuição e com virtualização
desativada), então `build.sh` não pôde rodar. A imagem foi gerada com o
`zig cc`/`zig ld.lld` disponível, usando os mesmos fontes e flags do projeto
(`-target x86-freestanding-none -march=i686 -ffreestanding -fno-builtin
-fno-stack-protector -O2`) e `objcopy`/`mkimage.py`. O `build.sh` foi atualizado
para compilar e linkar `http.o`, e `kernel/net.c` permaneceu inalterado; em
`kernel/net.h` só existia o bloco de declarações HTTP que também está em
`kernel/http.h`.

