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
- `GET /api/platform/health`
- `GET /api/platform/events`
- `GET /api/platform/replay`：从 Python 服务端事实库重放已持久化的平台事件，支持 `platform`、`cursor`、`limit`
- `GET /api/cache/snapshot`：服务端缓存快照，支持 `platform`、`cursor`、`conversation_limit`、`message_limit`
- `GET /api/conversations/list`：服务端会话列表，支持 `platform`、`conversation_limit`
- `GET /api/conversations/messages`：指定会话消息，支持 `platform`、`conversation_key`、`message_limit`
- `POST /api/platform/command`
- 兼容路径：`/api/rpa/health`、`/api/rpa/events`、`/api/rpa/replay`、`/api/rpa/command`

## 说明

- 服务端可独立运行，不依赖 C++ 客户端进程存在。
- 客户端是否连接，不影响 Python 持续采集和写库。
- Python 仍然使用数据库作为事实源。
- 平台观测到的会话/消息事件会先写入 Python 服务端事实库，再进入内存事件队列并推送给 C++。
- `send_message` 命令会先在 Python 服务端事实库记录 pending 出站消息，再由发送结果事件更新为 sent/failed。
- 平台事件会写入 `rpa_events` 持久事件日志，断线或服务重启后可通过 `/api/platform/replay` 重放。
- 微信和千牛 sidecar 都会在 connect 后启动持续 observer；C++ 启动恢复时会先消费 replay，再拉 snapshot/backfill。
- snapshot 默认读取仓库 `database/app_data.db`，可用 `AI_CUSTOMER_SERVICE_APP_DB` 指定统一数据库；`AI_CUSTOMER_SERVICE_SERVER_DB` 仍作为兼容覆盖项。
