@echo off
rem Host and client side by side on one screen, to compare what the two machines see.
rem The engine is found automatically (see Tools\find_ue.ps1); set UE_ROOT to override.
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\find_ue.ps1"`) do set UE_ROOT=%%P
if "%UE_ROOT%"=="" (
  echo Unreal Engine 5.8 not found. Set UE_ROOT to the engine folder and run again.
  pause
  exit /b 1
)
set UE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe
start "" "%UE%" "%~dp0SharedOcean.uproject" /Game/Maps/Ocean?listen -game -windowed -ResX=960 -ResY=600 -WinX=0 -WinY=60
timeout /t 8 /nobreak >nul
start "" "%UE%" "%~dp0SharedOcean.uproject" 127.0.0.1 -game -windowed -ResX=960 -ResY=600 -WinX=960 -WinY=60
