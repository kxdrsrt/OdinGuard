@echo off
rem Removes OdinGuard from the client folder given as argument (default: current folder).
setlocal
set "DST=%~1"
if "%DST%"=="" set "DST=%CD%"
if not exist "%DST%\ijl15r.dll" (echo OdinGuard is not installed in "%DST%". & exit /b 1)
del /q "%DST%\odinguard.dll" "%DST%\odinguard.ini" 2>nul
move /y "%DST%\ijl15r.dll" "%DST%\ijl15.dll" >nul
echo OdinGuard removed from "%DST%".
