## 微信 RPA 读取方案（只读 / Windows OCR / 手工坐标）

本方案用于在 **没有电商平台商家账号** 的前提下，先以 Windows 桌面微信（`WeChat.exe`）为真实数据源，打通“外部平台消息 → `rpa_inbox_messages` → 聚合接待界面”的只读链路。

---

## 1. 目标与边界

### 1.1 目标（MVP）

- 从 Windows 微信桌面客户端读取当前聊天窗口中的**新增文本消息**。
- 将新增消息写入 SQLite 的 `rpa_inbox_messages`。
- Qt 端通过 inbox 消费器将消息显示在 `AggregateChatForm`（聚合接待）中。

### 1.2 不做（MVP 不包含）

- 不发送消息回微信（只读）。
- 不做 UIAutomation（Inspect 已验证难以读取内部文本）。
- 不做自动校准/智能定位消息区域（先手工写死坐标）。
- 不处理图片/语音/文件等非文本消息（先只做纯文本）。

---

## 2. 总体链路（与千牛 inbox 一致）

```mermaid
flowchart LR
  WeChat[WeChat.exe] --> Capture[ScreenCapture(消息区域)]
  Capture --> Ocr[WindowsOCR]
  Ocr --> Diff[IncrementalDetector(增量检测)]
  Diff --> Inbox[SQLite:rpa_inbox_messages]
  Inbox --> QtAdapter[Qt:InboxConsumerAdapter(platform=wechat_pc)]
  QtAdapter --> Router[MessageRouter]
  Router --> DB[(SQLite:conversations/messages)]
  DB --> UI[AggregateChatForm]
```

关键原则：**Python 只写 `rpa_inbox_messages`，Qt 单点维护 `conversations/messages`**。

---

## 3. 数据协议（写入 inbox）

写入表：`rpa_inbox_messages`

- `platform`: 固定 `wechat_pc`
- `platform_conversation_id`: MVP 先用固定值或昵称字符串（例如 `demo_wechat_conv_1`）
- `customer_name`: 展示名（MVP 可固定，例如 `演示微信联系人`）
- `content`: OCR 得到的新增文本行
- `created_at`: 本地时间（`YYYY-MM-DD HH:mm:ss`）
- `platform_msg_id`（强约束，稳定唯一）：
  - 推荐：`wechat_pc:<platformConversationId>:<ts_ms>:<sha1(content)>`
- `consume_status`: Python 写入时固定为 `0`
- 写入方式：`INSERT OR IGNORE`（依赖 `(platform, platform_msg_id)` 唯一索引保证幂等）

---

## 4. 手工坐标配置（MVP）

### 4.1 为什么要手工坐标

微信桌面端 UI 自绘，UIA 看不到内部消息文本；为了最快打通链路，先用“窗口定位 + 固定消息区域矩形”方式截屏 OCR。

### 4.2 推荐配置形态（示例）

建议新增 `python/rpa/wechat_config.json`（或 ini），先支持相对窗口坐标：

```json
{
  "poll_interval_sec": 3,
  "window_match": {
    "process_name": "WeChat.exe",
    "title_contains": "微信"
  },
  "chat_region": {
    "mode": "relative_to_window",
    "x": 260,
    "y": 90,
    "w": 720,
    "h": 760
  },
  "conversation": {
    "platform_conversation_id": "demo_wechat_conv_1",
    "customer_name": "演示微信联系人"
  }
}
```

> `relative_to_window`：消息区域矩形相对微信主窗口左上角，窗口移动不影响。

---

## 5. Windows OCR（Python 侧）

### 5.1 技术点

- 使用 WinRT OCR 能力（Windows 10/11），Python 侧可通过 `winsdk`/`winrt` 调用。
- MVP 输出只需要“整段文本”，后续可升级到行级/块级结果。

### 5.2 失败策略

- OCR 失败/空结果：仅打日志，不写入 inbox，不中断进程。

---

## 6. 增量检测（避免重复写入）

MVP 采用“尾部锚点 + 新增行”策略：

- 将 OCR 文本按行切分，做轻度清洗（去空行、去明显 UI 噪声）。
- 保存上次识别的尾部 \(N\) 行（例如 20 行）作为锚点。
- 本次识别后，从尾部向前寻找锚点，锚点之后的行视为新增。
- 对每条新增行写入 inbox（`INSERT OR IGNORE` + `platform_msg_id` 去重）。

状态保存建议：`python/rpa/state_wechat.json`（防止重启后大量重复入库）。

---

## 7. 已知风险与后续演进

### 7.1 风险

- OCR 抖动导致重复/错字：通过 `platform_msg_id` 去重 + 增量检测降低影响。
- 窗口遮挡/最小化导致截屏失败：MVP 允许漏读，后续可尝试 `PrintWindow` 或提示保持可见。
- 无法准确区分对方/自己消息：MVP 先不区分，后续可按气泡位置/颜色做图像特征分类（复杂度较高）。

### 7.2 后续演进

- 增加“区域校准工具”：用户框选消息区域并保存配置。
- 从单会话扩展到多会话：识别左侧会话列表并按会话分别维护增量状态。
- 从只读扩展到可写：实现 `Writer`（模拟输入/发送）并复用 `messages.sync_status` 状态机。

