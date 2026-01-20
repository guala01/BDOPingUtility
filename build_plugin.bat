@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

msbuild "AfterburnerPingPlugin\Ping.vcxproj" /p:Configuration=Release /p:Platform=Win32 /m
if errorlevel 1 exit /b %errorlevel%

if exist "AfterburnerPingPlugin\Release\BDOPing.dll" (
  copy /Y "AfterburnerPingPlugin\Release\BDOPing.dll" "C:\Program Files (x86)\MSI Afterburner\Plugins\Monitoring\BDOPing.dll"
  if errorlevel 1 (
    echo.
    echo Copy failed (access denied). Please copy manually:
    echo   AfterburnerPingPlugin\Release\BDOPing.dll
    echo to:
    echo   C:\Program Files ^(x86^)\MSI Afterburner\Plugins\Monitoring\BDOPing.dll
  )
)

endlocal
