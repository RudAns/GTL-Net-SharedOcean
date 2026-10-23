@echo off
rem Host and client side by side on one screen, to compare what the two machines see.
rem Uses its own port (17790) so it never mixes with a window started by Run.bat, and closes earlier demo windows first.
rem The engine is found automatically (see Tools\find_ue.ps1); set UE_ROOT to override.
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\find_ue.ps1"`) do set UE_ROOT=%%P
if "%UE_ROOT%"=="" (
  echo Unreal Engine 5.8 not found. Set UE_ROOT to the engine folder and run again.
  pause
  exit /b 1
)
echo Closing earlier SharedOcean demo windows...
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='UnrealEditor.exe'\" | Where-Object { $_.CommandLine -like '*SharedOcean.uproject*' -and $_.CommandLine -like '* -game*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
set UE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe
set LITE=-ExecCmds="t.MaxFPS 60,r.ScreenPercentage 80,r.VolumetricCloud.ViewRaySampleMaxCount 128"
start "" "%UE%" "%~dp0SharedOcean.uproject" /Game/Maps/Ocean?listen -port=17790 -game -windowed -ResX=960 -ResY=600 -SOSide=L %LITE%
echo Waiting for the host to start...
ping -n 13 127.0.0.1 >nul
start "" "%UE%" "%~dp0SharedOcean.uproject" 127.0.0.1:17790 -SOJoin=127.0.0.1:17790 -game -windowed -ResX=960 -ResY=600 -SOSide=R %LITE%
