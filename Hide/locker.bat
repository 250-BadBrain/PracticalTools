@echo off
set "PASSWORD=1234"
set "LOCKER=Locker"
set "LOCKED=Control Panel.{21EC2020-3AEA-1069-A2DD-08002B30309D}"

if exist "%LOCKED%" goto unlock
if not exist "%LOCKER%" goto create
goto lock

:create
mkdir "%LOCKER%"
echo Locker created.
goto end

:lock
choice /c YN /n /m "Hide Locker? [Y/N]: "
if errorlevel 2 goto end
ren "%LOCKER%" "%LOCKED%"
attrib +h +s "%LOCKED%"
echo Locker hidden.
goto end

:unlock
set /p "INPUT=Password: "
if not "%INPUT%"=="%PASSWORD%" (
  echo Wrong password.
  goto end
)
attrib -h -s "%LOCKED%"
ren "%LOCKED%" "%LOCKER%"
echo Locker restored.
goto end

:end
