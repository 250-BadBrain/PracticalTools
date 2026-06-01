@echo off
setlocal EnableExtensions

if not exist dist mkdir dist
set CGO_ENABLED=0
set "LDFLAGS=-s -w"

if not "%~1"=="" (
  if /i "%~1"=="all" goto build_all
  if /i "%~1"=="release" goto build_release
  set "TARGET_OS=%~1"
  set "TARGET_ARCH=%~2"
  goto build_one
)

:choose_os
echo.
echo Select operating system:
echo 1. Windows
echo 2. Linux
echo 3. All
echo.
choice /c 123 /n /m "Choose [1-3]: "
if errorlevel 3 goto build_all
if errorlevel 2 goto select_linux
if errorlevel 1 goto select_windows

:select_windows
set "TARGET_OS=windows"
goto choose_arch

:select_linux
set "TARGET_OS=linux"
goto choose_arch

:choose_arch
echo.
echo Select architecture:
echo 1. amd64
echo 2. 386
echo 3. arm64
if /i "%TARGET_OS%"=="linux" echo 4. armv7
echo.
if /i "%TARGET_OS%"=="linux" (
  choice /c 1234 /n /m "Choose [1-4]: "
  if errorlevel 4 goto select_armv7
  if errorlevel 3 goto select_arm64
  if errorlevel 2 goto select_386
  if errorlevel 1 goto select_amd64
) else (
  choice /c 123 /n /m "Choose [1-3]: "
  if errorlevel 3 goto select_arm64
  if errorlevel 2 goto select_386
  if errorlevel 1 goto select_amd64
)

:select_amd64
set "TARGET_ARCH=amd64"
goto build_one

:select_386
set "TARGET_ARCH=386"
goto build_one

:select_arm64
set "TARGET_ARCH=arm64"
goto build_one

:select_armv7
set "TARGET_ARCH=armv7"
goto build_one

:build_one
if "%TARGET_OS%"=="" goto usage
if "%TARGET_ARCH%"=="" goto usage

if /i "%TARGET_OS%"=="windows" goto build_windows
if /i "%TARGET_OS%"=="linux" goto build_linux
goto usage

:build_windows
if /i "%TARGET_ARCH%"=="amd64" goto build_windows_normal
if /i "%TARGET_ARCH%"=="386" goto build_windows_normal
if /i "%TARGET_ARCH%"=="arm64" goto build_windows_normal
echo Unsupported Windows architecture: %TARGET_ARCH%
exit /b 1

:build_windows_normal
set GOOS=windows
set GOARCH=%TARGET_ARCH%
set GOARM=
set "OUTPUT=dist\invisiblecompiler-windows-%TARGET_ARCH%.exe"
go build -ldflags "%LDFLAGS% -H=windowsgui" -o "%OUTPUT%" .
if errorlevel 1 exit /b 1
echo Built %OUTPUT%
exit /b 0

:build_linux
if /i "%TARGET_ARCH%"=="armv7" goto build_linux_armv7
if /i "%TARGET_ARCH%"=="amd64" goto build_linux_normal
if /i "%TARGET_ARCH%"=="386" goto build_linux_normal
if /i "%TARGET_ARCH%"=="arm64" goto build_linux_normal
echo Unsupported Linux architecture: %TARGET_ARCH%
exit /b 1

:build_linux_normal
set GOOS=linux
set GOARCH=%TARGET_ARCH%
set GOARM=
set "OUTPUT=dist\invisiblecompiler-linux-%TARGET_ARCH%"
go build -ldflags "%LDFLAGS%" -o "%OUTPUT%" .
if errorlevel 1 exit /b 1
echo Built %OUTPUT%
exit /b 0

:build_linux_armv7
set GOOS=linux
set GOARCH=arm
set GOARM=7
set "OUTPUT=dist\invisiblecompiler-linux-armv7"
go build -ldflags "%LDFLAGS%" -o "%OUTPUT%" .
if errorlevel 1 exit /b 1
echo Built %OUTPUT%
exit /b 0

:build_all
call "%~f0" windows amd64 || exit /b 1
call "%~f0" windows 386 || exit /b 1
call "%~f0" windows arm64 || exit /b 1
call "%~f0" linux amd64 || exit /b 1
call "%~f0" linux 386 || exit /b 1
call "%~f0" linux arm64 || exit /b 1
call "%~f0" linux armv7 || exit /b 1
echo Build finished.
exit /b 0

:build_release
call "%~f0" windows amd64 || exit /b 1
call "%~f0" linux amd64 || exit /b 1
call "%~f0" linux arm64 || exit /b 1
echo Release build finished.
exit /b 0

:usage
echo Usage:
echo   build.bat
echo   build.bat all
echo   build.bat release
echo   build.bat windows amd64
echo   build.bat linux arm64
exit /b 1
