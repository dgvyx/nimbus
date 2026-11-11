$ErrorActionPreference = 'Stop'
Set-Location "$PSScriptRoot/.."
python tests/tcp_vector.py
if ($LASTEXITCODE) { throw 'Python reference failed' }
gcc -std=c99 -Wall -Wextra -Werror -ffreestanding -fno-builtin -DNET_CHECKSUM_ONLY tests/test_tcp_checksum.c kernel/net.c -o tests/test_tcp_checksum.exe
if ($LASTEXITCODE) { throw 'Checksum compilation failed' }
./tests/test_tcp_checksum.exe
if ($LASTEXITCODE) { throw 'Checksum test failed' }
gcc -std=c99 -ffreestanding -fno-builtin -Wno-pointer-to-int-cast tests/test_tcp_packet.c -o tests/test_tcp_packet.exe
if ($LASTEXITCODE) { throw 'Packet compilation failed' }
./tests/test_tcp_packet.exe
if ($LASTEXITCODE) { throw 'Packet test failed' }
python tests/verify_syn.py tests/syn_frame.pcap
if ($LASTEXITCODE) { throw 'Captured SYN is invalid' }
