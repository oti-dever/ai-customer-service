# Python Service

Python 独立服务端入口，负责 RPA 命令、事件和健康检查。

## 启动

在 `python/` 目录下执行：

```powershell
python -m service.server --host 127.0.0.1 --port 8765 --mode debug
```

正式模式：

```powershell
python -m service.server --host 127.0.0.1 --port 8765 --mode formal
```

也可以使用 `python/service/run_service.bat` 快速启动正式模式。

## 模式

- `debug`：允许开发者命令执行，包括读取微信和发送消息。
- `formal`：允许读取和采集，拒绝 `send_message`、`prepare_reply_draft`。

## 接口

- `GET /api/health`
- `GET /api/rpa/health`
- `GET /api/rpa/events`
- `POST /api/rpa/command`

## 说明

- 服务端可独立运行，不依赖 C++ 客户端进程存在。
- 客户端是否连接，不影响 Python 持续采集和写库。
- Python 仍然使用数据库作为事实源。
