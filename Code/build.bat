@echo off
setlocal enabledelayedexpansion

REM Check if FBuild.exe exists in the current directory or PATH
where /q FBuild.exe
if %errorlevel% neq 0 (
    echo Error: FBuild.exe not found in the current directory or PATH.
    echo Please ensure FBuild.exe is available before running this script.
    goto :failedend
)

FBuild.exe All-x64-Release %*
if "%errorlevel%" neq "0" goto :failedend
FBuild.exe All-x64-Debug %*
if "%errorlevel%" neq "0" goto :failedend
FBuild.exe solution
if "%errorlevel%" neq "0" goto :failedend

REM Set folders with proper path handling
set "cur_folder=%~dp0"
set "tmp_folder=%cur_folder%..\tmp"
set outputs=x64-Release x64-Debug

REM Convert tmp_folder to absolute path
for %%i in ("%tmp_folder%") do set "tmp_folder=%%~fi"
echo Tmp folder: %tmp_folder%

@REM Get version from FBuildVersion.h
set "version_file=%cur_folder%Tools\FBuild\FBuildCore\FBuildVersion.h"
for /f "tokens=3 delims= " %%a in ('findstr /C:"#define FBUILD_VERSION_STRING" "%version_file%"') do set version=%%a
set version=%version:"=%
echo Version: %version%

@REM copy exe to tmp bin
echo Copying executables to tmp bin folders...
for %%f in (%outputs%) do (
    set "bin_dir=%tmp_folder%\%%f\bin"
    echo Creating directory: !bin_dir!
    mkdir "!bin_dir!" 1>NUL 2>NUL
    xcopy /y /q /s /k /d /h "%tmp_folder%\%%f\Tools\FBuild\FBuild\FBuild.exe" "!bin_dir!" > nul
    xcopy /y /q /s /k /d /h "%tmp_folder%\%%f\Tools\FBuild\FBuildCoordinator\FBuildCoordinator.exe" "!bin_dir!" > nul
    xcopy /y /q /s /k /d /h "%tmp_folder%\%%f\Tools\FBuild\FBuildWorker\FBuildWorker.exe" "!bin_dir!" > nul
)

@REM copy exe to outside bin with naming convention FASTBuild-Windows-x64-VERSION
set "bin_folder=%cur_folder%..\FASTBuild-Windows-x64-%version%"
REM Convert bin_folder to absolute path
for %%i in ("%bin_folder%") do set "bin_folder=%%~fi"
echo Creating output directory: %bin_folder%
mkdir "%bin_folder%" 1>NUL 2>NUL
xcopy /y /q /s /k /d /h "%tmp_folder%\x64-Release\bin\FBuild.exe" "%bin_folder%" > nul
xcopy /y /q /s /k /d /h "%tmp_folder%\x64-Release\bin\FBuildCoordinator.exe" "%bin_folder%" > nul
xcopy /y /q /s /k /d /h "%tmp_folder%\x64-Release\bin\FBuildWorker.exe" "%bin_folder%" > nul

echo;
echo Build Succeed !!!
echo Build ready in folder: %bin_folder%
goto :end

:failedend
echo;
echo Build Failed !!!

:end
endlocal
