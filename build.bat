@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "SOURCE=%~dp0src\runtime\d4tune.c"
set "OUTPUT=%~dp0d4tune.bin"
set "TMP=%~dp0d4tune.bin.tmp"

if not exist "%ROOT%\runtime\cpu.h" goto bad_root
if not exist "%ROOT%\runtime\guest.h" goto bad_root
if not exist "%SOURCE%" (
  echo D4Tune: missing "%SOURCE%"
  exit /b 1
)

set "CLANG="
if defined WASI if exist "%WASI%\bin\clang.exe" set "CLANG=%WASI%\bin\clang.exe"
if not defined CLANG (
  for /d %%I in ("%ROOT%\deps\wasi-sdk-*") do (
    if exist "%%~fI\bin\clang.exe" set "CLANG=%%~fI\bin\clang.exe"
  )
)
if not defined CLANG (
  echo D4Tune: Windows wasi-sdk 34 was not found.
  echo Unpack it under "%ROOT%\deps" or set WASI to its directory.
  exit /b 1
)

if exist "%TMP%" del /q "%TMP%"
"%CLANG%" --target=wasm32-wasip1-threads -pthread -O3 ^
  -msimd128 -mbulk-memory -mnontrapping-fptoint -mtail-call -mexception-handling ^
  -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false ^
  -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS ^
  -fno-delete-null-pointer-checks ^
  -include "%ROOT%\runtime\web\compat\wasm_plat.h" ^
  -I"%ROOT%\runtime\web\compat" -I"%ROOT%\runtime" ^
  -c -o "%TMP%" "%SOURCE%"
if errorlevel 1 (
  if exist "%TMP%" del /q "%TMP%"
  exit /b 1
)

move /y "%TMP%" "%OUTPUT%" >nul
if errorlevel 1 (
  if exist "%TMP%" del /q "%TMP%"
  echo D4Tune: could not replace "%OUTPUT%".
  exit /b 1
)
echo D4Tune: built relocatable WebAssembly object "%OUTPUT%"
echo D4Tune: link this object into the target project; it is not a live-process plugin
where certutil.exe >nul 2>nul
if not errorlevel 1 certutil.exe -hashfile "%OUTPUT%" SHA256
exit /b 0

:bad_root
echo D4Tune: put this D4Tune folder directly inside an nfsu2-web source checkout.
exit /b 1
