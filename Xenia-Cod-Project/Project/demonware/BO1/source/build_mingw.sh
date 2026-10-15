#!/bin/sh
# Cross-compile bo1live.exe on Linux with MinGW-w64 (what the shipped exe was built with).
set -e
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres src/app.rc -O coff -o app.res.o
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -municode -mwindows -static -s -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 \
	src/net.cpp src/server.cpp src/main.cpp app.res.o -o bo1live.exe -lws2_32 -liphlpapi -lcomctl32 -lgdi32 -lshell32
# Linux console build (for a VPS): g++ -std=c++17 -O2 -pthread src/net.cpp src/server.cpp src/main.cpp -o bo1live
