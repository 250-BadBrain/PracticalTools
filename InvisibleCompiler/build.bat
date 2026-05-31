@echo off
setlocal EnableExtensions

if not exist dist mkdir dist

set CGO_ENABLED=0
set GOOS=windows
set GOARCH=amd64
go build -ldflags "-s -w -H=windowsgui" -o dist\invisiblecompiler-windows-amd64.exe .
if errorlevel 1 exit /b 1

set GOOS=linux
set GOARCH=amd64
go build -ldflags "-s -w" -o dist\invisiblecompiler-linux-amd64 .
if errorlevel 1 exit /b 1

set GOOS=linux
set GOARCH=arm64
go build -ldflags "-s -w" -o dist\invisiblecompiler-linux-arm64 .
if errorlevel 1 exit /b 1

echo Build finished.
endlocal
