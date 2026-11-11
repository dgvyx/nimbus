#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
flags='-std=c99 -Wall -Wextra -Werror -ffreestanding -fno-builtin'
for test in test_html parser_edges layout_edges limits; do
    gcc $flags "tests/$test.c" kernel/html.c kernel/layout.c -o "tests/$test"
    "./tests/$test"
done
gcc $flags -DHTTP_PARSER_ONLY "tests/test_http.c" kernel/http.c -o "tests/test_http"
"./tests/test_http"
for opt in 0 2; do
    for unit in html layout; do
        gcc $flags -m32 -O"$opt" -fno-stack-protector -fno-pie -c "kernel/$unit.c" -o "tests/${unit}32.o"
        nm -u "tests/${unit}32.o" > tests/undefined.txt
        if grep -Ev '(^$|[[:space:]]_?html_decode_text$)' tests/undefined.txt; then
            echo 'Unexpected external symbol' >&2
            exit 1
        fi
    done
done
for opt in 0 2; do
    gcc $flags -m32 -O"$opt" -fno-stack-protector -fno-pie -DHTTP_PARSER_ONLY -c kernel/http.c -o tests/http32.o
    nm -u tests/http32.o > tests/undefined.txt
    if grep -Ev '^$' tests/undefined.txt; then
        echo 'Unexpected external symbol in http' >&2
        exit 1
    fi
done
echo '32-bit freestanding checks passed (O0 and O2)'
