@echo off
rem ===========================================================================
rem Clean folder utility
rem                                               Copyright (c) 2006-2015 MAYO.
rem ===========================================================================

@echo on
pause clean each folder?

del *.aps /s /f /q
del *.suo /a:h /s /f /q
del *.ncb /s /f /q
del *.user  /s /f /q
for /f %%i in ('dir /a:d /b') do rmdir /s /q .\%%i\Release
for /f %%i in ('dir /a:d /b') do rmdir /s /q .\%%i\Debug
for /f %%i in ('dir /a:d /b') do rmdir /s /q .\%%i\Win32
for /f %%i in ('dir /a:d /b') do rmdir /s /q .\%%i\Win32
for /f %%i in ('dir /a:d /b') do rmdir /s /q .\%%i\x64
for /f %%i in ('dir /a:d /b') do rmdir /s /q .\%%i\x64

pause
