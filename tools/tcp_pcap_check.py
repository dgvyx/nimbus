#!/usr/bin/env python3
"""
tools/tcp_pcap_check.py - Analisa arquivos PCAP para diagnosticar problemas TCP
Valida campos do cabeçalho Ethernet, IP, TCP e recalcula checksum TCP com pseudo-cabeçalho.
"""

import sys
import struct
from pathlib import Path


def parse_pcap(path):
    """Lê arquivo PCAP e retorna lista de quadros (timestamp, bytes)."""
    with open(path, 'rb') as f:
        # Header global PCAP (24 bytes)
        global_header = f.read(24)
        if len(global_header) < 24:
            raise ValueError("PCAP muito curto")
        magic, version_major, version_minor, thiszone, sigfigs, snaplen, network = struct.unpack('<IHHIIII', global_header)
        if magic != 0xa1b2c3d4:
            raise ValueError(f"Magic PCAP inválido: 0x{magic:08x}")
        
        frames = []
        while True:
            hdr = f.read(16)
            if len(hdr) < 16:
                break
            ts_sec, ts_usec, incl_len, orig_len = struct.unpack('<IIII', hdr)
            data = f.read(incl_len)
            if len(data) < incl_len:
                break
            frames.append((ts_sec + ts_usec / 1e6, data))
        return frames


def parse_ethernet(data):
    """Parseia cabeçalho Ethernet (14 bytes). Retorna (dst_mac, src_mac, ethertype, payload)."""
    if len(data) < 14:
        return None
    dst_mac = data[0:6]
    src_mac = data[6:12]
    ethertype = struct.unpack('>H', data[12:14])[0]
    payload = data[14:]
    return dst_mac, src_mac, ethertype, payload


def mac_to_str(mac):
    return ':'.join(f'{b:02x}' for b in mac)


def ip_to_str(ip):
    return '.'.join(str((ip >> (8 * i)) & 0xFF) for i in [3, 2, 1, 0])


def ip_checksum(data):
    """Calcula checksum IP (RFC 1071)."""
    s = 0
    for i in range(0, len(data) & ~1, 2):
        s += (data[i] << 8) + data[i + 1]
    if len(data) & 1:
        s += data[-1] << 8
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def tcp_checksum_pseudo(src_ip, dst_ip, tcp_len):
    """Calcula soma do pseudo-cabeçalho TCP (RFC 793)."""
    # Pseudo-header: src_ip (4) + dst_ip (4) + zero (1) + proto (1) + tcp_len (2)
    # zero byte = 0, protocol = 6 (TCP)
    s = 0
    s += (src_ip >> 16) & 0xFFFF
    s += src_ip & 0xFFFF
    s += (dst_ip >> 16) & 0xFFFF
    s += dst_ip & 0xFFFF
    s += 0x0006  # zero byte + proto TCP (6)
    s += tcp_len
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return s & 0xFFFF


def tcp_checksum_full(src_ip, dst_ip, tcp_hdr, tcp_len):
    """Calcula checksum TCP completo com pseudo-cabeçalho."""
    pseudo = tcp_checksum_pseudo(src_ip, dst_ip, tcp_len)
    s = pseudo
    p = tcp_hdr
    n = tcp_len
    while n > 1:
        s += (p[0] << 8) | p[1]
        p = p[2:]
        n -= 2
    if n:
        s += p[0] << 8
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def parse_ip(data):
    """Parseia cabeçalho IP. Retorna (version, ihl, total_len, proto, src_ip, dst_ip, payload)."""
    if len(data) < 20:
        return None
    version_ihl = data[0]
    version = version_ihl >> 4
    ihl = version_ihl & 0x0F
    if version != 4 or ihl < 5:
        return None
    total_len = struct.unpack('>H', data[2:4])[0]
    proto = data[9]
    src_ip = struct.unpack('>I', data[12:16])[0]
    dst_ip = struct.unpack('>I', data[16:20])[0]
    ihl_bytes = ihl * 4
    if total_len < ihl_bytes or total_len > len(data):
        return None
    # O restante do quadro Ethernet pode ser preenchimento; o IP termina em total_len.
    payload = data[ihl_bytes:total_len]
    return version, ihl, total_len, proto, src_ip, dst_ip, payload


def parse_tcp(data):
    """Parseia cabeçalho TCP. Retorna (sport, dport, seq, ack, data_offset, flags, window, checksum, urg_ptr, payload)."""
    if len(data) < 20:
        return None
    sport = struct.unpack('>H', data[0:2])[0]
    dport = struct.unpack('>H', data[2:4])[0]
    seq = struct.unpack('>I', data[4:8])[0]
    ack = struct.unpack('>I', data[8:12])[0]
    data_offset = (data[12] >> 4) & 0x0F
    flags = data[13]
    window = struct.unpack('>H', data[14:16])[0]
    checksum = struct.unpack('>H', data[16:18])[0]
    urg_ptr = struct.unpack('>H', data[18:20])[0]
    hdr_len = data_offset * 4
    payload = data[hdr_len:]
    return sport, dport, seq, ack, data_offset, flags, window, checksum, urg_ptr, payload


def flag_str(flags):
    s = []
    if flags & 0x01: s.append('FIN')
    if flags & 0x02: s.append('SYN')
    if flags & 0x04: s.append('RST')
    if flags & 0x08: s.append('PSH')
    if flags & 0x10: s.append('ACK')
    if flags & 0x20: s.append('URG')
    return '|'.join(s) if s else 'NONE'


def parse_tcp_options(payload):
    """Parseia opções TCP (MSS, etc.)."""
    opts = []
    i = 0
    while i < len(payload):
        kind = payload[i]
        if kind == 0:  # EOL
            break
        if kind == 1:  # NOP
            opts.append(('NOP', None))
            i += 1
            continue
        if i + 1 >= len(payload):
            break
        length = payload[i + 1]
        if i + length > len(payload):
            break
        value = payload[i + 2:i + length]
        opts.append((kind, value))
        i += length
    return opts


def analyze_pcap(path):
    frames = parse_pcap(path)
    print(f"\n=== Análise de {path} ===")
    print(f"Total de quadros: {len(frames)}")
    
    tcp_frames = 0
    syn_count = 0
    synack_count = 0
    rst_count = 0
    errors = []
    
    for i, (ts, data) in enumerate(frames):
        eth = parse_ethernet(data)
        if not eth:
            continue
        dst_mac, src_mac, ethertype, payload = eth
        
        if ethertype == 0x0800:  # IPv4
            ip = parse_ip(payload)
            if not ip:
                continue
            _, ihl, total_len, proto, src_ip, dst_ip, ip_payload = ip
            
            # Demais bytes depois de 20+total_len são preenchimento Ethernet.
            expected_len = 14 + total_len
            actual_len = len(data)
            if actual_len < expected_len:
                errors.append(f"Frame {i}: quadro truncado (IP total_len={total_len}, esperado>={expected_len}, real={actual_len})")
            elif actual_len > expected_len and actual_len < 60:
                errors.append(f"Frame {i}: preenchimento Ethernet atipico (real={actual_len})")
            
            # Checksum IP
            ip_hdr = payload[:ip[1] * 4]
            calc_ip_cksum = ip_checksum(ip_hdr)
            ip_cksum = struct.unpack('>H', ip_hdr[10:12])[0]
            if calc_ip_cksum != 0:
                errors.append(f"Frame {i}: IP checksum inválido (calc=0x{calc_ip_cksum:04x}, frame=0x{ip_cksum:04x})")
            
            if proto == 6:  # TCP
                tcp = parse_tcp(ip_payload)
                if not tcp:
                    continue
                sport, dport, seq, ack, data_off, flags, window, checksum, urg_ptr, tcp_payload = tcp
                tcp_hdr_len = data_off * 4
                if data_off < 5 or tcp_hdr_len > len(ip_payload):
                    errors.append(f"Frame {i}: data offset TCP invalido ({tcp_hdr_len})")
                    continue
                tcp_len = total_len - ihl * 4
                if tcp_len < tcp_hdr_len:
                    errors.append(f"Frame {i}: segmento TCP curto ({tcp_len})")
                    continue

                # Recalcula o checksum sobre exatamente o segmento TCP (sem padding).
                calc_tcp_cksum = tcp_checksum_full(src_ip, dst_ip, ip_payload[:tcp_len], tcp_len)

                tcp_frames += 1
                flags_str = flag_str(flags)

                print(f"\nFrame {i} TCP: {ip_to_str(src_ip)}:{sport} -> {ip_to_str(dst_ip)}:{dport}")
                print(f"  Seq={seq} Ack={ack} Flags={flags_str} Win={window} Len={len(tcp_payload)}")
                print(f"  Checksum: frame=0x{checksum:04x} calc=0x{calc_tcp_cksum:04x} {'OK' if calc_tcp_cksum == 0 else 'ERRO!'}")
                
                if calc_tcp_cksum != 0:
                    errors.append(f"Frame {i}: TCP checksum invalido (frame=0x{checksum:04x}, soma complemento=0x{calc_tcp_cksum:04x})")
                
                if (flags & 0x02) and not (flags & 0x10):  # SYN puro
                    syn_count += 1
                if (flags & 0x12) == 0x12:  # SYN|ACK
                    synack_count += 1
                if flags & 0x04:  # RST
                    rst_count += 1
                
                # Opções TCP
                if data_off > 5:
                    opts = parse_tcp_options(ip_payload[20:tcp_hdr_len])
                    for kind, val in opts:
                        if kind == 2:  # MSS
                            mss = struct.unpack('>H', val)[0]
                            print(f"  Opção MSS: {mss}")
        
        elif ethertype == 0x0806:  # ARP
            if len(payload) >= 28:
                op = struct.unpack('>H', payload[6:8])[0]
                sha = payload[8:14]
                spa = struct.unpack('>I', payload[14:18])[0]
                tha = payload[18:24]
                tpa = struct.unpack('>I', payload[24:28])[0]
                op_str = {1: 'REQUEST', 2: 'REPLY'}.get(op, f'UNK({op})')
                print(f"\nFrame {i} ARP: {op_str} SHA={mac_to_str(sha)} SPA={ip_to_str(spa)} THA={mac_to_str(tha)} TPA={ip_to_str(tpa)}")
    
    print(f"\n=== Resumo ===")
    print(f"Frames TCP: {tcp_frames}")
    print(f"SYN: {syn_count}")
    print(f"SYN-ACK: {synack_count}")
    print(f"RST: {rst_count}")
    
    if errors:
        print(f"\n❌ ERROS ENCONTRADOS ({len(errors)}):")
        for e in errors:
            print(f"  - {e}")
        return False
    else:
        print(f"\n✅ TODOS OS CAMPOS VÁLIDOS")
        return True


def main():
    if len(sys.argv) < 2:
        print(f"Uso: {sys.argv[0]} <arquivo.pcap>")
        sys.exit(1)
    
    ok = analyze_pcap(sys.argv[1])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    import sys
    main()