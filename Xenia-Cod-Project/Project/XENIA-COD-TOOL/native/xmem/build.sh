#!/bin/sh
# Builds the LZX helper the BO2 fastfile reader loads. Run from this folder.
#   Windows (from Linux): x86_64-w64-mingw32-gcc ...  -> ../../t5menu/bin/xmem.dll
#   Linux:                gcc ...                     -> ../../t5menu/bin/libxmem.so
set -e
out=../../t5menu/bin
mkdir -p "$out"
x86_64-w64-mingw32-gcc -O2 -shared -I. -o "$out/xmem.dll" xmem.c lzxd.c -static-libgcc
gcc -O2 -shared -fPIC -I. -o "$out/libxmem.so" xmem.c lzxd.c
