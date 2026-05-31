@echo off
setlocal EnableExtensions

if not exist dist mkdir dist
set "BASE_LDFLAGS=-s -w"

if not "%~1"=="" (
  if /i "%~1"=="all" goto build_all
  if /i "%~1"=="cli" goto args_with_mode
  if /i "%~1"=="browser" goto args_with_mode
  if /i "%~1"=="native" goto args_with_mode
  set "BUILD_MODE=browser"
  set "TARGET_OS=%~1"
  set "TARGET_ARCH=%~2"
  goto build_one
)

:choose_mode
echo.
echo Select build mode:
echo 1. CLI
echo 2. Browser GUI
echo 3. Native GUI
echo.
choice /c 123 /n /m "Choose [1-3]: "
if errorlevel 3 goto select_mode_native
if errorlevel 2 goto select_mode_browser
if errorlevel 1 goto select_mode_cli

:select_mode_cli
set "BUILD_MODE=cli"
goto choose_os

:select_mode_browser
set "BUILD_MODE=browser"
goto choose_os

:select_mode_native
set "BUILD_MODE=native"
goto choose_os

:args_with_mode
set "BUILD_MODE=%~1"
set "TARGET_OS=%~2"
set "TARGET_ARCH=%~3"
goto build_one

:choose_os
echo.
echo Select operating system:
echo 1. Windows
echo 2. Linux
echo 3. All
echo.
choice /c 123 /n /m "Choose [1-3]: "
if errorlevel 3 goto build_mode_all
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
if "%BUILD_MODE%"=="" goto usage
if "%TARGET_OS%"=="" goto usage
if "%TARGET_ARCH%"=="" goto usage

set "BUILD_TAGS="
set "LDFLAGS=%BASE_LDFLAGS%"
set CGO_ENABLED=0
if /i "%BUILD_MODE%"=="cli" set "BUILD_TAGS=-tags cli"
if /i "%BUILD_MODE%"=="browser" (
  set "BUILD_TAGS="
  if /i "%TARGET_OS%"=="windows" set "LDFLAGS=%BASE_LDFLAGS% -H=windowsgui"
)
if /i "%BUILD_MODE%"=="native" (
  set "BUILD_TAGS=-tags native"
  set CGO_ENABLED=1
  if /i "%TARGET_OS%"=="windows" set "LDFLAGS=%BASE_LDFLAGS% -H=windowsgui"
)
if /i not "%BUILD_MODE%"=="cli" if /i not "%BUILD_MODE%"=="browser" if /i not "%BUILD_MODE%"=="native" goto usage

if /i "%BUILD_MODE%"=="native" if /i not "%TARGET_OS%"=="windows" goto native_cross_warning
if /i "%BUILD_MODE%"=="native" if /i not "%TARGET_ARCH%"=="amd64" goto native_arch_warning

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
set "OUTPUT=dist\portchecker-%BUILD_MODE%-windows-%TARGET_ARCH%.exe"
go build %BUILD_TAGS% -ldflags "%LDFLAGS%" -o "%OUTPUT%" .
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
set "OUTPUT=dist\portchecker-%BUILD_MODE%-linux-%TARGET_ARCH%"
go build %BUILD_TAGS% -ldflags "%LDFLAGS%" -o "%OUTPUT%" .
if errorlevel 1 exit /b 1
echo Built %OUTPUT%
exit /b 0

:build_linux_armv7
set GOOS=linux
set GOARCH=arm
set GOARM=7
set "OUTPUT=dist\portchecker-%BUILD_MODE%-linux-armv7"
go build %BUILD_TAGS% -ldflags "%LDFLAGS%" -o "%OUTPUT%" .
if errorlevel 1 exit /b 1
echo Built %OUTPUT%
exit /b 0

:build_mode_all
call "%~f0" %BUILD_MODE% windows amd64 || exit /b 1
call "%~f0" %BUILD_MODE% windows 386 || exit /b 1
call "%~f0" %BUILD_MODE% windows arm64 || exit /b 1
call "%~f0" %BUILD_MODE% linux amd64 || exit /b 1
call "%~f0" %BUILD_MODE% linux 386 || exit /b 1
call "%~f0" %BUILD_MODE% linux arm64 || exit /b 1
call "%~f0" %BUILD_MODE% linux armv7 || exit /b 1
echo Build finished.
exit /b 0

:build_all
call "%~f0" cli windows amd64 || exit /b 1
call "%~f0" browser windows amd64 || exit /b 1
call "%~f0" native windows amd64 || exit /b 1
call "%~f0" browser linux amd64 || exit /b 1
call "%~f0" browser linux arm64 || exit /b 1
echo Build finished.
exit /b 0

:usage
echo Usage:
echo   build.bat
echo   build.bat all
echo   build.bat cli windows amd64
echo   build.bat browser linux arm64
echo   build.bat native windows amd64
echo   build.bat windows amd64
exit /b 1

:native_cross_warning
echo Native GUI uses platform graphics libraries and is not cross-compiled by this Windows script.
echo Build Linux native GUI on Linux, or use browser/cli mode for cross-platform builds.
exit /b 1

:native_arch_warning
echo Native GUI cross-architecture builds need a matching C toolchain.
echo This script supports native windows amd64 on this machine by default.
exit /b 1
