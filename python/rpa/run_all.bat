@echo off
REM Run all platform RPA (WeChat + 千牛)
set FLAGS_use_mkldnn=0
set FLAGS_use_dnnl=0
set PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK=True
cd /d "%~dp0..\.."
python python/rpa/main.py --platform all
pause
