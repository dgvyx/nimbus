# TCP Debug Report - Nimbus OS

> **Atualização (2026-11-11):** este relatório é histórico. As conclusões de
> "checksum TCP inválido" e "tcptest não implementado" nas seções 4–6 estavam
> erradas: o verificador `tools/tcp_pcap_check.py` usava `data[0]` (byte do MAC)
> no lugar do IHL e rejeitava o preenchimento Ethernet. O verificador foi
> corrigido, a pilha TCP foi validada e o comando `tcptest` foi implementado em
> `kernel/ui.c`. O veredito atual está na seção "Atualização do verificador", no
> fim do documento.


## Resumo do Problema
O comando `tcptest` no Terminal do Nimbus OS não consegue estabelecer conexão TCP com o servidor HTTP no host (10.0.2.2:8000). O SYN é enviado mas nunca recebe SYN-ACK.

---

## Verificações Realizadas

### 1. Servidor HTTP no Host
- ✅ Porta 8000 escutando em `0.0.0.0:8000` (IPv4 e IPv6)
- ✅ `curl http://localhost:8000/` responde com HTTP 200 OK (directory listing)
- ✅ Servidor Python `http.server` rodando corretamente

### 2. Captura PCAP sem hostfwd (`tcp_nohostfwd.pcap`)
- **Arquivo**: 24 bytes (apenas header global PCAP, zero quadros)
- **Conclusão**: Nenhum tráfego capturado - QEMU finalizado antes do tráfego

### 3. Captura PCAP com hostfwd (`tcp_hostfwd.pcap`)
**4 quadros capturados (328 bytes):**

| Frame | Tipo | Detalhes |
|-------|------|----------|
| 0 | ARP REQUEST | SHA=52:55:0a:00:02:02 (gateway) SPA=10.0.2.2 → THA=00:00:00:00:00:00 TPA=10.0.2.15 |
| 1 | ARP REPLY | SHA=52:54:00:12:34:56 (guest) SPA=10.0.2.15 → THA=52:55:0a:00:02:02 TPA=10.0.2.2 |
| 2 | TCP SYN | 10.0.2.2:50332 → 10.0.2.15:8000, Seq=1472001, Flags=SYN, Win=65535 |
| 3 | TCP SYN | 10.0.2.2:50332 → 10.0.2.15:8000, Seq=1472001, Flags=SYN, Win=65535 (retransmissão) |

**Observações críticas:**
- **Nenhum SYN do guest (10.0.2.15) para 10.0.2.2:8000** - o comando `tcptest` não envia SYN
- SYN observados são **do host para o guest** (hostfwd: host 10.0.2.2:50332 → guest 10.0.2.15:8000)
- ARP funciona corretamente (gateway resolve MAC do guest)

### 4. Validação dos Quadros TCP (Frame 2 e 3)

| Campo | Valor no Quadro | Valor Esperado | Status |
|-------|----------------|----------------|--------|
| MAC Destino | 52:54:00:12:34:56 (guest) | MAC do guest | ✅ OK |
| IP Origem | 10.0.2.2 (gateway) | Gateway QEMU | ✅ OK |
| IP Destino | 10.0.2.15 (guest) | IP do guest | ✅ OK |
| Proto IP | 6 (TCP) | 6 | ✅ OK |
| Flags TCP | SYN (0x02) | SYN | ✅ OK |
| Seq Number | 1472001 | Qualquer | ✅ OK |
| Ack Number | 0 | 0 (SYN) | ✅ OK |
| Window | 65535 | Qualquer | ✅ OK |
| **Checksum TCP** | **0x2622** | **0x07be** | ❌ **INVÁLIDO** |
| IP total_len | 44 bytes | 44 (20 IP + 24 TCP) | ✅ OK |
| Tamanho quadro real | 60 bytes | 60 (mínimo Ethernet) | ✅ OK |

**Erro crítico**: Checksum TCP inválido
- **Encontrado no quadro**: `0x2622`
- **Recalculado (pseudo-header + TCP)**: `0x07be`
- Diferença: `0x1E64` (desvio sistemático)

### 5. Ausência de SYN do Guest → Host
- **Nenhum pacote** com origem 10.0.2.15 (guest) e destino 10.0.2.2:8000
- Comando `tcptest` **não implementado** no kernel (`kernel/ui.c:189` lista no help mas não há handler em `term_exec`)

---

## Causa Raiz (Prioridade)

### 🔴 CRÍTICO 1: Comando `tcptest` não implementado
- **Arquivo**: `kernel/ui.c` linha 189
- **Problema**: Help lista `tcptest` mas `term_exec()` não tem handler
- **Resultado**: Comando cai em "comando nao encontrado", nenhum SYN enviado

### 🔴 CRÍTICO 2: Checksum TCP inválido nos SYN recebidos
- **Valor no quadro**: `0x2622`
- **Esperado (pseudo-header RFC 793)**: `0x07be`
- **Causa provável**: Pseudo-header TCP em `net.c:73` (função `tcp_checksum_pseudo`) 
  - Falta byte zero entre IPs e protocolo
  - Cálculo: `sum += 0x0006` (proto 6) mas falta `sum += 0` (byte zero) + `sum += len` posicionado errado
- **Impacto**: Host descarta SYN com checksum inválido → sem SYN-ACK

### 🟡 MÉDIO 3: Ausência de SYN do Guest
- Comando `tcptest` não implementado → nenhum SYN enviado
- Mesmo se implementado, checksum TCP quebrado impediria handshake

---

## Veredito do tcp_pcap_check.py

### `tcp_hostfwd.pcap` (com hostfwd)
```
❌ ERROS ENCONTRADOS (4):
  - Frame 2: TCP checksum inválido (frame=0x2622, calc=0x07be)
  - Frame 3: TCP checksum inválido (frame=0x2622, calc=0x07be)
  - Frame 2: tamanho IP total_len=44, esperado quadro=58, real=60
  - Frame 3: tamanho IP total_len=44, esperado quadro=58, real=60
```

### `tcp_nohostfwd.pcap`
- 0 frames capturados (apenas header global PCAP)

---

## O que falta investigar (se SYN estivesse válido)

Se o checksum fosse corrigido e o comando `tcptest` implementado:

1. **ISN (Initial Sequence Number)**: Atual `0x12345678` fixo - OK para teste
2. **Porta de origem**: Alocação dinâmica `tcp_port_next++` (inicia em 1024) - OK
3. **MAC de origem**: `52:54:00:12:34:56` (guest) vs gateway `52:55:0a:00:02:02` - OK
4. **MSS option**: Anunciado como 1460 (TCP_MSS) - OK
4. **Janela de recepção**: 65535 (TCP_WINDOW) - OK
5. **Retransmissão**: RTO inicial 24 ticks (100ms), backoff exponencial - implementado
5. **ARP para gateway**: Funcionando (ARP reply recebido)

---

## Próximos Passos (Prioridade)

1. **Implementar `tcptest` em `kernel/ui.c:term_exec`** (5 min)
2. **Corrigir `tcp_checksum_pseudo()` em `kernel/net.c:73`** (10 min)
   - Adicionar byte zero explícito no pseudo-header
   - Verificar ordem dos campos: src_ip + dst_ip + zero + proto + tcp_len
3. **Testar novamente com PCAP** - capturar SYN do guest → host
4. Se SYN válido mas sem SYN-ACK: verificar firewall do host / binding do servidor
5. Implementar `tcp_handle_syn_ack` completo com validação de `ack == seq + 1`

---

## Comandos para Reproduzir

```bash
# Sem hostfwd (captura tráfego puro guest -> host)
qemu-system-i386 -drive file=nimbus.img,format=raw -m 128 \
  -netdev user,id=n0 -device rtl8139,netdev=n0 \
  -object filter-dump,id=f0,netdev=n0,file=rede_nohostfwd.pcap

# Com hostfwd (permite host -> guest)
qemu-system-i386 -drive file=nimbus.img,format=raw -m 128 \
  -netdev user,id=n0,hostfwd=tcp::8000-:8000 \
  -device rtl8139,netdev=n0 \
  -object filter-dump,id=f0,netdev=n0,file=rede_hostfwd.pcap

# Análise
python tools/tcp_pcap_check.py rede.pcap
```

---

## Atualização do verificador (2026-11-11)

### Correções em `tools/tcp_pcap_check.py`

| Problema | Correção |
|---|---|
| `tcp_len = len(ip_payload) - (data[0] & 0x0F) * 4` usava o primeiro byte do MAC de destino como IHL | `tcp_len = total_len - ihl * 4`, usando o total length do IPv4 |
| O checksum TCP era comparado com o valor do campo (resto diferente de zero) | O verificador agora espera complemento zero, o mesmo critério de `tests/verify_tcp.py` |
| `parse_ip` devolvia o padding Ethernet como se fosse payload IP | `payload = data[ihl_bytes:total_len]` |
| Qualquer quadro maior que `14 + total_len` era erro, mesmo sendo padding mínimo de 60 bytes | Só acusa quadro truncado (`actual_len < expected_len`) |
| Opções TCP nunca eram lidas (`data[12] >> 4`) | Usa o data offset real (`data_off`) e limita a opção ao cabeçalho TCP |
| Variáveis mortas `src`/`dst` no pseudo-cabeçalho | Removidas |
| SYN-ACK era contado também como SYN | `SYN` só conta quando o bit ACK está ausente |

### Veredito atual

`tests/rede.pcap` (captura antiga) e `tests/tcptest.pcap` (imagem real):

```
Frames TCP: 11 / 13
SYN: 1
SYN-ACK: 1
RST: 0
✅ TODOS OS CAMPOS VÁLIDOS
```

Nenhum checksum, offset ou campo de tamanho inválido. As linhas 46–74 originais
deste documento ficam mantidas apenas como registro do diagnóstico anterior, que
partiu de um cálculo errado do script.
