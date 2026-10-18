@echo off
rem Builds tau++.exe (command line) and tauIDE.exe (Win32 IDE) with MinGW-w64 g++.
rem Run from this folder in a shell where g++ is on PATH (e.g. MSYS2 MinGW64).
if not exist build mkdir build
g++ -std=c++17 -O2 -static -o build\tau++.exe tauvm.cpp tauxx.cpp || goto :err
g++ -std=c++17 -O2 -static -mwindows -o build\tauIDE.exe ide\main.cpp tauvm.cpp -lcomctl32 -lcomdlg32 -lgdi32 -luser32 || goto :err
xcopy /E /I /Y ide\scripts build\scripts >nul
echo Built build\tau++.exe and build\tauIDE.exe (scripts copied next to the exe).
goto :eof
:err
echo Build failed.
exit /b 1
