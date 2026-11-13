@echo off
REM Activate ESP-IDF before running this script.
if not defined IDF_PATH (
    echo Activate an ESP-IDF environment first.
    exit /b 1
)
pushd "%~dp0"
idf.py build
set "result=%errorlevel%"
popd
exit /b %result%
