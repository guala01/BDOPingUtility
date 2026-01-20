@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

cl /nologo /EHsc main.c /Fe:BDOPingUtility.exe ws2_32.lib iphlpapi.lib /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup

endlocal
