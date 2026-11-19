#!/bin/sh
set -eu

if [ $# -lt 3 ]; then
    echo "usage: $0 <mode> <file.ptab> <out>" >&2
    echo "  mode: -Xl (xo) | -Ol (o) | -Cl (c)" >&2
    exit 1
fi

mode="$1"
ptab="$2"
out="$3"
src="${out}.x"

if [ ! -f "$ptab" ]; then
    echo "xmake: missing ${ptab}" >&2
    exit 1
fi

if [ ! -f "$src" ]; then
    echo "xmake: missing ${src}" >&2
    exit 1
fi

echo "[*] xmake: ${mode} ${ptab} + ${src} -> ${out}"

case "$mode" in
    -Cl|-c)
        ./xc "${mode}" "${ptab}" "${src}" "${out}.c"
        clang \
            -ffreestanding -nostdlib -static \
            -Wl,-e,_start -O2 \
            -fno-stack-protector -fno-builtin \
            -o "${out}" "${out}.c"
        ;;
    -Xl|-Ol)
        ./xc "${mode}" "${ptab}" "${src}" "${out}"
        ;;
    *)
        echo "xmake: unknown mode ${mode}" >&2
        exit 1
        ;;
esac

echo "[+] xmake: ./${out}"
