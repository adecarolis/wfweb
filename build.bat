@echo off
setlocal enabledelayedexpansion

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
rd /s /q wfweb-release 2>nul
rd /s /q wfweb-debug 2>nul
echo Clean done. >> %LOG%
goto :build

:docleanonly
echo Cleaning build artifacts... >> %LOG%
if exist Makefile nmake clean > nul 2>&1
del /q Makefile Makefile.Debug Makefile.Release .qmake.stash 2>nul
rd /s /q release 2>nul
rd /s /q debug 2>nul
rd /s /q wfweb-release 2>nul
rd /s /q wfweb-debug 2>nul
echo Clean done. >> %LOG%
echo EXIT:0 >> %LOG%
exit /b 0

:build
:: --- Build RADE custom Opus (if radae_nopy submodule is present) ---
set RADAE_DIR=%SRCDIR%\radae_nopy
set RADAE_BUILD=%RADAE_DIR%\build
set OPUS_MSVC_LIB=%RADAE_BUILD%\opus_msvc_build\Release\opus.lib

if exist "%RADAE_DIR%\src\rade_api.h" (
    if not exist "%OPUS_MSVC_LIB%" (
        echo === RADE: building custom Opus === >> %LOG%

        :: Step 1: MSYS2 cmake to fetch/build Opus source + patch headers for MSVC
        if not exist "%RADAE_BUILD%\build_opus-prefix\src\build_opus\include\opus.h" (
            echo --- MSYS2 cmake + header patching --- >> %LOG%
            C:\msys64\usr\bin\bash.exe -l "%SRCDIR%\build-rade-opus.sh" "%RADAE_DIR%" >> %LOG% 2>&1
            if errorlevel 1 (
                echo ERROR: MSYS2 RADE build failed >> %LOG%
                echo EXIT:1 >> %LOG%
                exit /b 1
            )
        )

        :: Step 2: Build custom Opus with MSVC (static lib)
        echo --- MSVC Opus build --- >> %LOG%
        set OPUS_SRC=%RADAE_BUILD%\build_opus-prefix\src\build_opus
        cmake -S "!OPUS_SRC!" -B "%RADAE_BUILD%\opus_msvc_build" -DOPUS_DRED=ON -DOPUS_OSCE=ON -DBUILD_SHARED_LIBS=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF -A x64 >> %LOG% 2>&1
        if errorlevel 1 (
            echo ERROR: Opus MSVC cmake configure failed >> %LOG%
            echo EXIT:1 >> %LOG%
            exit /b 1
        )
        cmake --build "%RADAE_BUILD%\opus_msvc_build" --config Release >> %LOG% 2>&1
        if errorlevel 1 (
            echo ERROR: Opus MSVC cmake build failed >> %LOG%
            echo EXIT:1 >> %LOG%
            exit /b 1
        )
        echo RADE custom Opus built successfully >> %LOG%
    ) else (
        echo RADE custom Opus already built, skipping >> %LOG%
    )
) else (
    echo RADE: radae_nopy submodule not found, skipping >> %LOG%
)

:: --- Build codec2 (FreeDV) from source if not already present ---
set VCPKG_WIN=%VCPKG:/=\%
if not exist "%VCPKG_WIN%\lib\codec2.lib" (
    echo === codec2: building from source === >> %LOG%
    set CODEC2_SRC=%SRCDIR%\codec2

    if not exist "!CODEC2_SRC!\CMakeLists.txt" (
        echo --- Cloning codec2 --- >> %LOG%
        git clone --depth 1 https://github.com/drowe67/codec2.git "!CODEC2_SRC!" >> %LOG% 2>&1
        if errorlevel 1 (
            echo ERROR: codec2 clone failed >> %LOG%
            echo EXIT:1 >> %LOG%
            exit /b 1
        )
    )

    echo --- Building codec2 with MSYS2 MinGW --- >> %LOG%
    C:\msys64\usr\bin\bash.exe -l "%SRCDIR%\build-codec2.sh" "!CODEC2_SRC!" "%VCPKG_WIN%" >> %LOG% 2>&1
    if errorlevel 1 (
        echo ERROR: codec2 build failed >> %LOG%
        echo EXIT:1 >> %LOG%
        exit /b 1
    )

    :: Create MSVC import lib from .def (needs vcvars64 environment)
    echo --- Creating MSVC import lib --- >> %LOG%
    lib /def:"!CODEC2_SRC!\libcodec2.def" /out:"%VCPKG_WIN%\lib\codec2.lib" /machine:x64 >> %LOG% 2>&1
    if errorlevel 1 (
        echo ERROR: lib.exe import lib creation failed >> %LOG%
        echo EXIT:1 >> %LOG%
        exit /b 1
    )
    echo codec2 built and installed successfully >> %LOG%
) else (
    echo codec2 already installed, skipping >> %LOG%
)

echo === qmake === >> %LOG%
"%QTDIR%\bin\qmake.exe" wfweb.pro CONFIG+=release "VCPKG_DIR=%VCPKG%" "DEFINES-=FTDI_SUPPORT" >> %LOG% 2>&1
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

:: --- Deploy Qt runtime ---
echo === windeployqt === >> %LOG%
set OUTDIR=%SRCDIR%\wfweb-release
"%QTDIR%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw "%OUTDIR%\wfweb.exe" >> %LOG% 2>&1
if errorlevel 1 (
    echo ERROR: windeployqt failed >> %LOG%
    echo EXIT:1 >> %LOG%
    exit /b 1
)

:: --- Deploy runtime DLLs ---
echo === Deploying vcpkg DLLs === >> %LOG%
set VCPKG_BIN=%VCPKG:/=\%\bin

:: vcpkg DLLs (portaudio, hidapi, codec2, OpenSSL; opus is statically linked when RADE is enabled)
for %%F in (portaudio.dll hidapi.dll libcodec2.dll libssl-3-x64.dll libcrypto-3-x64.dll libssl-1_1-x64.dll libcrypto-1_1-x64.dll) do (
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

:: --- Deploy generated WSPR decoder asset (optional) ---
if exist "%SRCDIR%\resources\web-generated\wspr-decoder-wasm.js" (
    echo === Deploying WSPR decoder asset === >> %LOG%
    if not exist "%OUTDIR%\web-generated" mkdir "%OUTDIR%\web-generated"
    copy /y "%SRCDIR%\resources\web-generated\wspr-decoder-wasm.js" "%OUTDIR%\web-generated\" >> %LOG% 2>&1
)

echo BUILD OK: wfweb-release\wfweb.exe >> %LOG%
echo EXIT:0 >> %LOG%
exit /b 0
