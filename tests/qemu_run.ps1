$ErrorActionPreference = 'Stop'
Set-Location "$PSScriptRoot/.."
$zig = "C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe\zig-x86_64-windows-0.17.1\zig.exe"
$qemu = "C:\Program Files\qemu\qemu-system-i386.exe"
if (!(Test-Path $zig)) { throw "zig not found at $zig" }
if (!(Test-Path $qemu)) { throw "qemu not found at $qemu" }

& $zig cc -target x86-freestanding -m32 -ffreestanding -fno-pic -fno-pie `
    -fno-stack-protector -fno-builtin -O2 -nostdlib -mno-sse -mno-sse2 -mno-mmx -I kernel `
    "-Wl,-T,tests/qemu/link.ld" "-Wl,--build-id=none" `
    tests/qemu/net_boot.c kernel/net.c -o tests/net_test.elf
if ($LASTEXITCODE) { throw 'test kernel build failed' }

Remove-Item tests/rede.pcap, tests/serial.txt -ErrorAction SilentlyContinue
$server = Start-Process python -ArgumentList '-m','http.server','8000','--bind','127.0.0.1',
    '--directory','tests/qemu/www' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 800
try {
    $serial = Join-Path (Get-Location) 'tests/serial.txt'
    $pcap = Join-Path (Get-Location) 'tests/rede.pcap'
    $q = Start-Process $qemu -ArgumentList '-kernel','tests/net_test.elf','-m','128',
        '-display','none','-monitor','none','-serial','stdio',
        '-netdev','user,id=n0','-device','rtl8139,netdev=n0',
        '-object',"filter-dump,id=f0,netdev=n0,file=$pcap",
        '-device','isa-debug-exit' -PassThru -NoNewWindow `
        -RedirectStandardOutput $serial -RedirectStandardError 'tests/qemu_err.txt'
    $exited = $q.WaitForExit(30000)
    if (!$exited) { Stop-Process -Id $q.Id -Force -ErrorAction SilentlyContinue }
} finally {
    Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
}

Get-Content tests/serial.txt
if (!(Select-String -Path tests/serial.txt -Pattern 'HTTP/1\.[01]' -Quiet)) {
    throw 'guest did not receive an HTTP response'
}
python tests/verify_syn.py tests/rede.pcap
if ($LASTEXITCODE) { throw 'captured SYN invalid' }
Write-Output 'QEMU TCP integration passed'
