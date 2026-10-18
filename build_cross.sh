#!/bin/sh
# Cross-compile the Windows binaries from Linux/macOS with mingw-w64.
set -e
CXX=${CXX:-x86_64-w64-mingw32-g++}
mkdir -p build
$CXX -std=c++17 -O2 -static -o build/tau++.exe tauvm.cpp tauxx.cpp
$CXX -std=c++17 -O2 -static -mwindows -o build/tauIDE.exe ide/main.cpp tauvm.cpp -lcomctl32 -lcomdlg32 -lgdi32 -luser32
rm -rf build/scripts && cp -r ide/scripts build/scripts
echo "Built build/tau++.exe and build/tauIDE.exe"
