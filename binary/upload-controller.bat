@echo off
setlocal

set "PORT=%~1"
if "%PORT%"=="" (
  echo Usage: %~nx0 COM20
  exit /b 1
)

set "ESPTOOL=%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
set "IMAGE=%~dp0controller-tennis\controller-tennis-v1.0-esp32c3-merged.bin"

if not exist "%ESPTOOL%" (
  echo esptool.exe not found:
  echo %ESPTOOL%
  exit /b 1
)

if not exist "%IMAGE%" (
  echo Firmware image not found:
  echo %IMAGE%
  exit /b 1
)

"%ESPTOOL%" --chip esp32c3 --port "%PORT%" --baud 921600 write_flash -z 0x0 "%IMAGE%"
exit /b %ERRORLEVEL%
