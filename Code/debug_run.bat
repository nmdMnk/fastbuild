@echo off
setlocal enabledelayedexpansion

@REM Set paths
set "cur_folder=%~dp0"
set "tmp_folder=%cur_folder%..\tmp"

@REM Convert tmp_folder to absolute path
for %%i in ("%tmp_folder%") do set "tmp_folder=%%~fi"

set output_type=x64-Debug
set "output_folder=%tmp_folder%\%output_type%\bin"

@REM Check if executables exist
if not exist "%output_folder%\FBuildCoordinator.exe" (
    echo Error: FBuildCoordinator.exe not found in %output_folder%
    echo Please build the project first.
    goto :end
)

if not exist "%output_folder%\FBuildWorker.exe" (
    echo Error: FBuildWorker.exe not found in %output_folder%
    echo Please build the project first.
    goto :end
)

@REM Run the executables
echo Starting FBuildCoordinator and FBuildWorker...
start %output_folder%\FBuildCoordinator.exe
start %output_folder%\FBuildWorker.exe -console -coordinator=127.0.0.1 -cpus=2

:end
endlocal
