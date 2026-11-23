@echo off
rem Installs OdinGuard into the client folder given as argument (default: current folder).
rem The existing ijl15.dll (ROExt) is kept as ijl15r.dll; the OdinGuard loader takes its name.
setlocal
set "SRC=%~dp0"
set "DST=%~1"
if "%DST%"=="" set "DST=%CD%"
if not exist "%DST%\alpha.exe" if not exist "%DST%\ijl15x.dll" (echo No RO client in "%DST%". & exit /b 1)
if not exist "%DST%\ijl15r.dll" move /y "%DST%\ijl15.dll" "%DST%\ijl15r.dll" >nul
copy /y "%SRC%ijl15.dll" "%DST%\ijl15.dll" >nul
copy /y "%SRC%odinguard.dll" "%DST%\odinguard.dll" >nul
if not exist "%DST%\odinguard.ini" copy /y "%SRC%odinguard.ini" "%DST%\odinguard.ini" >nul
echo OdinGuard installed in "%DST%".
