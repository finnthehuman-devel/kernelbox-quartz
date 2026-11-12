#!/bin/bash
set -e
CC="clang"
CFLAGS="-O2 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib"
HVFLAGS="-O2"
SRC_DIR="src"
BUILD_DIR="build"
TARGET="kbox_system"

rm -rf "$BUILD_DIR" "$TARGET"
mkdir -p "$BUILD_DIR"

# Kernel objects: everything in src/ except the hypervisor.
echo "[*] Compiling kernel objects..."
KERNEL_OBJS=()
for f in "$SRC_DIR"/*.c; do
    base="$(basename "$f" .c)"
    if [ "$base" = "cvm_hv" ]; then continue; fi
    $CC $CFLAGS -I"$SRC_DIR" -c "$f" -o "$BUILD_DIR/$base.o"
    KERNEL_OBJS+=("$BUILD_DIR/$base.o")
done

echo "[*] Compiling Hypervisor..."
$CC $HVFLAGS -I"$SRC_DIR" -c "$SRC_DIR/cvm_hv.c" -o "$BUILD_DIR/cvm_hv.o"

echo "[*] Linking..."
$CC $HVFLAGS -o "$TARGET" \
    "$BUILD_DIR/cvm_hv.o" \
    "${KERNEL_OBJS[@]}"

echo "[+] Build complete: ./$TARGET"
echo "-----------------------------------------"
./"$TARGET"
echo "-----------------------------------------"
