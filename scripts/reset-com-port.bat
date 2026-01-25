@echo off
:: Reset COM Port - Runs PowerShell script with admin privileges
:: Usage: reset-com-port.bat [COM_PORT] [/k] [/n]
::   COM_PORT - e.g., COM18 (optional, lists ports if not specified)
::   /k       - also kill processes that might hold the port
::   /n       - NUCLEAR mode: restart USB driver, kill everything

set "SCRIPT_DIR=%~dp0"
set "COM_PORT="
set "KILL_FLAG="
set "NUCLEAR_FLAG="

:: Parse arguments
:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="/k" (
    set "KILL_FLAG=-KillProcesses"
    shift
    goto :parse_args
)
if /i "%~1"=="/n" (
    set "NUCLEAR_FLAG=-Nuclear"
    shift
    goto :parse_args
)
if "%COM_PORT%"=="" (
    set "COM_PORT=%~1"
)
shift
goto :parse_args

:done_parsing

:: Build the PowerShell command
if "%COM_PORT%"=="" (
    set "PS_ARGS=%KILL_FLAG% %NUCLEAR_FLAG%"
) else (
    set "PS_ARGS=-ComPort %COM_PORT% %KILL_FLAG% %NUCLEAR_FLAG%"
)

:: Run as admin
powershell -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-ExecutionPolicy Bypass -NoExit -File \"%SCRIPT_DIR%reset-com-port.ps1\" %PS_ARGS%'"

