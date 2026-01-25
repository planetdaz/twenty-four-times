@echo off
:: Reset COM Port - Runs PowerShell script with admin privileges
:: Usage: reset-com-port.bat [COM_PORT] [/k]
::   COM_PORT - e.g., COM18 (optional, lists ports if not specified)
::   /k       - also kill processes that might hold the port

set "SCRIPT_DIR=%~dp0"
set "COM_PORT=%~1"
set "KILL_FLAG="

if /i "%~2"=="/k" set "KILL_FLAG=-KillProcesses"
if /i "%~1"=="/k" (
    set "COM_PORT="
    set "KILL_FLAG=-KillProcesses"
)

:: Build the PowerShell command
if "%COM_PORT%"=="" (
    set "PS_ARGS="
) else (
    set "PS_ARGS=-ComPort %COM_PORT% %KILL_FLAG%"
)

:: Run as admin
powershell -Command "Start-Process powershell -Verb RunAs -ArgumentList '-ExecutionPolicy Bypass -File \"%SCRIPT_DIR%reset-com-port.ps1\" %PS_ARGS%'"

