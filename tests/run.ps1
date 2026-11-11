$ErrorActionPreference = 'Stop'
Set-Location "$PSScriptRoot/.."
$flags = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin')
foreach ($test in @('test_html', 'parser_edges', 'layout_edges', 'limits')) {
    & gcc @flags "tests/$test.c" kernel/html.c kernel/layout.c -o "tests/$test.exe"
    if ($LASTEXITCODE) { throw "Compilation failed: $test" }
    & "./tests/$test.exe"
    if ($LASTEXITCODE) { throw "Tests failed: $test" }
}
& gcc @flags '-DHTTP_PARSER_ONLY' 'tests/test_http.c' kernel/http.c -o 'tests/test_http.exe'
if ($LASTEXITCODE) { throw 'Compilation failed: test_http' }
& './tests/test_http.exe'
if ($LASTEXITCODE) { throw 'Tests failed: test_http' }
foreach ($opt in @('0', '2')) {
    foreach ($unit in @('html', 'layout')) {
        & gcc @flags -m32 "-O$opt" -fno-stack-protector -c "kernel/$unit.c" -o "tests/${unit}32.o"
        if ($LASTEXITCODE) { throw "32-bit compilation failed: $unit" }
        $symbols = & nm -u "tests/${unit}32.o"
        if ($LASTEXITCODE) { throw 'nm failed' }
        $unexpected = $symbols | Where-Object { $_ -notmatch '\b_?html_decode_text$' }
        if ($unexpected) { throw "Unexpected external symbols: $unexpected" }
    }
}
foreach ($opt in @('0', '2')) {
    & gcc @flags -m32 "-O$opt" -fno-stack-protector -DHTTP_PARSER_ONLY -c kernel/http.c -o tests/http32.o
    if ($LASTEXITCODE) { throw '32-bit compilation failed: http' }
    $symbols = & nm -u tests/http32.o
    if ($LASTEXITCODE) { throw 'nm failed' }
    if ($symbols) { throw "Unexpected external symbols: $symbols" }
}
Write-Output '32-bit freestanding checks passed (O0 and O2)'
