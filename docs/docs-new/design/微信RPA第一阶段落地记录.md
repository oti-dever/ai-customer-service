# 微信 RPA 第一阶段落地记录

日期：2026-05-28

## 本阶段目标

先把新的微信 RPA 主链路接起来，让 Python 侧以平台适配器身份输出统一事件，C++ 侧消费事件并进入聚合会话界面。旧微信 OCR reader/writer 和 `rpa_inbox_messages` 暂时保留为兼容兜底。

## 已完成

- `python/service/server.py` 新增 RPA HTTP 接口：
  - `POST /api/rpa/command`
  - `GET /api/rpa/events?platform=wechat&cursor=...`
  - `GET /api/rpa/health?platform=wechat`
- 新增 `python/service/rpa_bridge.py`，提供 RPA HTTP bridge 和内存事件队列。
- 新增 `python/rpa/platforms/wechat/adapter.py`，承接微信平台命令、UIA 采集、草稿回填和事件封装。
- Python bridge 复用现有 `rpa.platforms.wechat.uia` 能力，先支持：
  - `connect`
  - `disconnect`
  - `health_check`
  - `fetch_visible_conversations`
  - `fetch_visible_messages`
  - `prepare_reply_draft`
  - `send_message`
- `src/ipc/ipcservice.*` 新增 RPA command / events / health 同步调用。
- `src/services/platforms/wechatrp_adapter.*` 改为优先消费 Python sidecar RPA event queue。
- `WechatRPAAdapter` 保留 `rpa_inbox_messages` 轮询作为 fallback，不影响旧链路。
- `message_observed` 事件已经能映射为 `PlatformMessage`，继续进入 `MessageRouter` 和聚合界面。

## 当前边界

- 事件通道暂时是 HTTP event queue 短轮询，不是最终 WebSocket/gRPC/管道推送。
- `service.rpa_bridge` 已收敛为 HTTP bridge、事件队列和平台分发入口；微信具体实现已拆到 `python/rpa/platforms/wechat/adapter.py`。
- 当前实现已建立 `detector / reader_v2 / sender_v2 / stability` 分层骨架，并新增 `uia_scoring.py` 承接候选控件打分兜底。
- `reader_v2.py` 已接入 `role_judgement.py`，优先保留消息方向、角色、判断方法和置信度，事件 metadata 中同步输出。
- `sender_v2.py` 已接入 `click_strategy.py`，发送按钮点击从单一 UIA click 扩展为后台窗口消息、Invoke、UIA click、前台鼠标点击的多策略链路。
- 内部仍优先复用现有 UIA 工具函数，后续继续替换为更完整的 `D:/wechat` 自动化内核。
- 独立微信工作台未删除，但不再作为新链路承载点。
- `send_message` 仍要求 C++ 侧带 `manual_confirmed_by_agent` token，先保证不会绕过人工确认边界。

## 下一步

1. 继续对齐 `D:/wechat`，增强失败冷却策略和调试证据保存。
2. 为 `message_observed -> PlatformMessage` 增加 C++ 单元测试。
3. 逐步降低 `rpa_inbox_messages` 在微信链路中的实时职责，只保留补偿和审计用途。

---

### 2026-05-29（微信旧 OCR 默认入口下线）

本轮进一步收口微信 RPA 主路径：

- Python 微信自动化默认入口只保留 `python/service/rpa_bridge.py` -> `python/rpa/platforms/wechat/*` sidecar 路线。
- 删除旧微信 OCR reader/writer/session/capture/preview/calibration/workbench Python 文件后，`rpa.common` 不再重导出 `wechat_capture`、`wechat_session`、`wechat_ocr_ops` 等旧兼容包装。
- C++ `MainWindow` 不再提供微信 OCR 校准入口，也不再在首页暴露“微信 RPA 工作台”快捷入口。
- `WeChatWorkbenchService` 不再尝试启动已删除的 `python/rpa/tools/wechat_workbench_service.py`，旧工作台 Python 服务明确下线。
- 千牛 OCR 校准和拼多多 UI/窗口识别入口保留；本轮删除的是微信旧 OCR 自动化实现和默认入口。

验证：已通过微信 sidecar 相关 Python AST 检查，以及 `test_wechat_uia_scoring.py`、`test_wechat_role_and_send.py`、`test_wechat_session_v2.py`。