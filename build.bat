@echo off
setlocal

:: ---------------------------------------------------------------
:: build.bat — Build wfweb (wfserver) on Windows
::
:: Usage:
::   build            Incremental release build
::   build clean      Delete all build artifacts, then build
::   build cleanonly   Delete all build artifacts without building
::
:: All output is written to build.log (viewable while build runs).
:: Exit code is written to the last line as EXIT:n
:: ---------------------------------------------------------------

set SRCDIR=C:\Users\alain\Devel\wfweb
set QTDIR=C:\Qt\5.15.2\msvc2019_64
set VCPKG=C:/vcpkg/installed/x64-windows
set LOG=%SRCDIR%\build.log

:: --- Visual Studio environment ---
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" > nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed > %LOG%
    echo EXIT:1 >> %LOG%
    exit /b 1
)
set PATH=%QTDIR%\bin;%PATH%
cd /d %SRCDIR%

:: Clear log
echo build.bat started %date% %time% > %LOG%

:: --- Handle "clean" / "cleanonly" ---
if /i "%1"=="clean" goto :doclean
if /i "%1"=="cleanonly" goto :docleanonly
goto :build

:doclean
echo Cleaning build artifacts... >> %LOG%
if exist Makefile nmake clean > nul 2>&1
del /q Makefile Makefile.Debug Makefile.Release .qmake.stash 2>nul
rd /s /q release 2>nul
rd /s /q debug 2>nul
rd /s /q wfweb 2>nul
rd /s /q wfserver-debug 2>nul
echo Clean done. >> %LOG%
goto :build

:docleanonly
echo Cleaning build artifacts... >> %LOG%
if exist Makefile nmake clean > nul 2>&1
del /q Makefile Makefile.Debug Makefile.Release .qmake.stash 2>nul
rd /s /q release 2>nul
rd /s /q debug 2>nul
rd /s /q wfweb 2>nul
rd /s /q wfserver-debug 2>nul
echo Clean done. >> %LOG%
echo EXIT:0 >> %LOG%
exit /b 0

:build
echo === qmake === >> %LOG%
"%QTDIR%\bin\qmake.exe" wfserver.pro CONFIG+=release "VCPKG_DIR=%VCPKG%" >> %LOG% 2>&1
if errorlevel 1 (
    echo ERROR: qmake failed >> %LOG%
    echo EXIT:1 >> %LOG%
    exit /b 1
)

echo === nmake === >> %LOG%
nmake release >> %LOG% 2>&1
if errorlevel 1 (
    echo ERROR: nmake failed >> %LOG%
    echo EXIT:1 >> %LOG%
    exit /b 1
)

:: --- Deploy runtime DLLs ---
echo === Deploying runtime DLLs === >> %LOG%
set OUTDIR=%SRCDIR%\wfweb
set VCPKG_BIN=%VCPKG:/=\%\bin

:: vcpkg DLLs (portaudio, opus, hidapi, OpenSSL)
for %%F in (portaudio.dll opus.dll hidapi.dll libssl-3-x64.dll libcrypto-3-x64.dll libssl-1_1-x64.dll libcrypto-1_1-x64.dll) do (
    if exist "%VCPKG_BIN%\%%F" (
        copy /y "%VCPKG_BIN%\%%F" "%OUTDIR%\" >> %LOG% 2>&1
    )
)
:: Qt 5.15 looks for OpenSSL 1.1 DLL names at runtime.  OpenSSL 3.x is
:: ABI-compatible for the subset Qt uses, so provide aliases if 1.1 DLLs
:: were not found above.
if not exist "%OUTDIR%\libssl-1_1-x64.dll" (
    if exist "%OUTDIR%\libssl-3-x64.dll" (
        copy /y "%OUTDIR%\libssl-3-x64.dll" "%OUTDIR%\libssl-1_1-x64.dll" >> %LOG% 2>&1
        echo Aliased libssl-3 as libssl-1_1 for Qt 5 SSL >> %LOG%
    )
)
if not exist "%OUTDIR%\libcrypto-1_1-x64.dll" (
    if exist "%OUTDIR%\libcrypto-3-x64.dll" (
        copy /y "%OUTDIR%\libcrypto-3-x64.dll" "%OUTDIR%\libcrypto-1_1-x64.dll" >> %LOG% 2>&1
        echo Aliased libcrypto-3 as libcrypto-1_1 for Qt 5 SSL >> %LOG%
    )
)
echo DLL deployment done >> %LOG%

:: --- Deploy rig definition files ---
echo === Deploying rig files === >> %LOG%
if not exist "%OUTDIR%\rigs" mkdir "%OUTDIR%\rigs"
copy /y "%SRCDIR%\rigs\*.rig" "%OUTDIR%\rigs\" >> %LOG% 2>&1
echo Rig files deployed >> %LOG%

echo BUILD OK: wfweb\wfweb.exe >> %LOG%
echo EXIT:0 >> %LOG%
exit /b 0
