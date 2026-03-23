@echo off
REM Run WeChat RPA (reader + writer)
REM 在启动 Python 前设置环境变量，禁用 oneDNN 避免 Paddle 3.3+ 崩溃
set FLAGS_use_mkldnn=0
set FLAGS_use_dnnl=0
REM 跳过模型服务器连通性检查，避免卡住（模型已本地缓存）
set PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK=True
cd /d "%~dp0..\.."
python python/rpa/main.py --platform wechat
pause
