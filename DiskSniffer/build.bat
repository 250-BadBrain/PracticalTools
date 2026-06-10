@echo off
setlocal
pushd "%~dp0"

if not exist dist mkdir dist
del /q dist\disksniffer-windows-amd64.exe 2>nul

where cl >nul 2>nul
if %errorlevel%==0 (
  cl /nologo /O2 /EHsc /DUNICODE /D_UNICODE disksniffer.cpp /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /OUT:dist\disksniffer.exe Comctl32.lib Shell32.lib Ole32.lib Uuid.lib
  if errorlevel 1 (popd & exit /b 1)
  popd
  exit /b 0
)

where g++ >nul 2>nul
if %errorlevel%==0 (
  g++ -std=c++17 -O2 -ffunction-sections -fdata-sections -municode -mwindows disksniffer.cpp -Wl,--gc-sections -s -static -lcomctl32 -lshell32 -lole32 -luuid -o dist\disksniffer.exe
  if errorlevel 1 (popd & exit /b 1)
  popd
  exit /b 0
)

echo No supported C++ compiler found. Install MSVC or MinGW g++.
popd
exit /b 1
