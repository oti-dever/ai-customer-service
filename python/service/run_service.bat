@echo off
setlocal
cd /d "%~dp0\.."
python -m service.server --host 127.0.0.1 --port 8765 --mode formal

