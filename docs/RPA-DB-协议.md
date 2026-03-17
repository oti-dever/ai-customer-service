## RPA 数据库共享协议（C++ / Python）

本协议约定 **Qt 客户端（C++）** 与 **Python RPA 组件** 通过同一个 SQLite 数据库进行数据交换的字段含义与读写规则，目标是：

- 保证双方对同一条记录有一致的理解；
- 便于在不引入独立后端服务的前提下，实现“外部平台 ↔ 聚合接待界面”的消息闭环。

数据库文件默认路径：项目根目录下的 `database/app.db`

---

## 1. conversations 表

现有建表 SQL（节选，自 `src/data/database.cpp`）：

```sql
CREATE TABLE IF NOT EXISTS conversations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  platform TEXT NOT NULL,
  platform_conversation_id TEXT,
  customer_name TEXT NOT NULL,
  last_message TEXT DEFAULT '',
  last_time DATETIME,
  unread_count INTEGER DEFAULT 0,
  status TEXT DEFAULT 'open',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(platform, platform_conversation_id)
);
```

### 1.1 字段含义

- **id**：本地会话自增主键。C++ / Python 在 messages 表中通过 `conversation_id` 引用。
- **platform**：平台标识，约定为小写字符串，例如：
  - `qianniu`：千牛客户端
  - `pdd_web`：拼多多网页
  - 后续可以扩展例如 `weixin_pc`、`douyin` 等
- **platform_conversation_id**：外部平台的会话标识，示例：
  - 千牛：平台自身的会话 id，或 `buyerId + shopId` 组合字符串；
  - 拼多多：聊天窗口 URL 中的会话 id、订单号等。
- **customer_name**：展示在聚合界面左侧会话列表的客户昵称。
- **last_message**：最近一条消息的简短内容预览（由 C++ 端自动维护）。
- **last_time**：最近一条消息的时间（由 C++ 端自动维护）。
- **unread_count**：未读计数（由 C++ 端自动维护）。
- **status**：会话状态，当前约定：
  - `"open"`：进行中的会话；
  - `"closed"`：已结束/归档的会话。
- **created_at**：本地会话创建时间。默认 `CURRENT_TIMESTAMP`。

### 1.2 协议约定

- **会话创建方**：
  - C++ 端在收到新消息且未找到对应 `(platform, platform_conversation_id)` 时，可以主动创建会话；
  - Python 端也可以在写入第一条消息前，先检测并创建对应会话（推荐）。
- **去重约定**：
  - `(platform, platform_conversation_id)` 组合必须唯一；
  - 如平台侧切换导致 id 变化，应由 Python 侧做好映射或迁移。

---

## 2. messages 表

现有建表 SQL（节选）：

```sql
CREATE TABLE IF NOT EXISTS messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  conversation_id INTEGER NOT NULL,
  direction TEXT NOT NULL,
  content TEXT NOT NULL,
  sender TEXT NOT NULL,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  platform_msg_id TEXT,
  FOREIGN KEY(conversation_id) REFERENCES conversations(id)
);
```

后续会在此表上按需扩展字段（例如 `sync_status`, `error_reason` 等），本协议中一并约定。

### 2.1 现有字段

- **id**：本地消息自增主键。
- **conversation_id**：外键，指向 `conversations.id`。
- **direction**：消息方向，字符串枚举：
  - `"in"`：客户 → 商家（进入聚合界面的“收到”消息）；
  - `"out"`：商家 → 客户（从聚合界面或外部平台发出的“发送”消息）。
- **content**：消息正文纯文本。后续如需支持富文本/表情，可另加字段。
- **sender**：发送方角色，当前约定：
  - `"customer"`：客户
  - `"agent"`：客服
  - `"system"`：系统生成的消息
- **created_at**：消息创建时间。默认 `CURRENT_TIMESTAMP`，推荐统一使用本地时间即可。
- **platform_msg_id**：外部平台的消息 id（可选，用于更精细的去重与状态追踪）。

### 2.2 扩展字段（计划）

为支持 C++ / Python 之间的同步状态，需要在 messages 表上新增以下字段（最终以实际迁移实现为准）：

- **sync_status INTEGER NOT NULL DEFAULT 1**
  - 建议枚举值：
    - `0 = pending_to_ui`：Python 写入后，尚未被 UI 消费（可选，暂不强制使用）；
    - `1 = normal`：正常状态（默认值）；  
    - `10 = pending_send`：聚合界面新建的“待发送到平台”的消息；
    - `11 = sent_ok`：Python 已成功发往外部平台；
    - `12 = sent_failed`：Python 发送失败，具体原因可见 `error_reason`。
- **error_reason TEXT DEFAULT ''**
  - 可选，记录 Python 发送失败时的错误原因，方便排查。

如需扩展更多字段（例如截图路径、附件信息），统一在此文档中补充。

---

## 3. C++ 端读写约定

### 3.1 读消息（聚合界面展示）

- C++ 端通过 `MessageDao` / `ConversationDao` 读取：
  - 按 `conversation_id` 查询消息列表时，应无条件包含 `direction in ('in','out')`，不必关心 `sync_status`；
  - UI 展示时，可以选择性根据 `sync_status` 渲染发送状态（后续扩展）。
- `MessageRouter` 收到来自某个适配器的 `PlatformMessage` 时：
  - 依据 `platform` 与 `platformConversationId` 查找/创建会话；
  - 写入一条 `direction = 'in'` 的记录：
    - `sender = 'customer'`；
    - 如有平台侧的消息 id，写入 `platform_msg_id`；
    - `sync_status` 通常直接写为 `1 (normal)`。

### 3.2 写消息（聚合界面发出）

- 聚合界面调用 `sendMessage()` 时，C++ 端应：
  - 在 messages 表中插入一条记录：
    - `direction = 'out'`
    - `sender = 'agent'`
    - `content` 为输入框文本
    - `sync_status = 10 (pending_send)`
  - 更新会话的 `last_message` / `last_time` 等汇总字段。
- C++ 端 **不直接操纵外部平台窗口**，而是只负责把“待发送消息”写入数据库，等待 Python 处理。

---

## 4. Python 端读写约定

### 4.1 Reader（从平台读入 → 写入 messages）

- Reader 负责从千牛客户端 / 拼多多网页中抓取到新消息，并将其写入 SQLite：
  - 必须设置：
    - `conversation_id`：根据 `(platform, platform_conversation_id)` 查询/创建会话后得到；
    - `direction = 'in'`；
    - `sender = 'customer'`（如是系统提示可改为 `system`）；
    - `content`：OCR 或 DOM 提取出来的文字；
    - `created_at`：建议使用平台时间或本地时间；
    - `platform_msg_id`：如平台暴露唯一 id，建议写入。
  - 建议写入时 `sync_status = 1 (normal)`，让 UI 立即视为正常消息。
- 去重策略建议：
  - 借助 `(platform, platform_msg_id)` 或 `(platform, platform_conversation_id, content, created_at)` 组合在 Python 端先做一次检查；
  - 再配合 C++ 端已有的简单指纹去重逻辑，避免重复气泡。

### 4.2 Writer（从 messages 读出 → 写回平台）

- Writer 负责读取“待发送消息”，并在外部平台中模拟输入与发送：
  - 定期查询：
    - `direction = 'out'`
    - `sync_status = 10 (pending_send)`
    - `platform` 为自己负责的平台（可由会话维度拿到）。
  - 对每条记录：
    - 通过 `conversation_id` 找到对应会话的 `platform` 与 `platform_conversation_id`；
    - 在外部 UI 中切换到对应聊天窗口；
    - 聚焦输入框 → 填入 `content` → 触发发送按钮；
    - 成功时：
      - 将该消息的 `sync_status` 更新为 `11 (sent_ok)`；
      - 如能拿到平台返回的消息 id，填入 `platform_msg_id`；
    - 失败时：
      - 将 `sync_status` 更新为 `12 (sent_failed)`；
      - 在 `error_reason` 中写明失败原因。

---

## 5. 时间与时区约定

- 所有 `DATETIME` 字段（`created_at`, `last_time` 等）目前均采用 **本地时间** 存储；
- C++ 与 Python 端应统一使用同一时区（通常为操作系统当前时区），不做额外的 UTC 转换；
- 未来如需要跨时区或服务器部署，再统一升级为 UTC 存储 + 本地显示换算。

---

## 6. 平台与扩展

- 目前平台枚举建议：
  - `qianniu`：千牛桌面客户端（RPA 主战场）；
  - `pdd_web`：拼多多网页；
  - `simulator`：现有模拟器平台（可继续使用，便于开发调试）。
- 新增平台时，应在：
  - 文档中补充平台标识字符串；
  - Python 端 Reader / Writer 中增加对应实现；
  - 如需特殊字段或行为，在本协议文件中追加说明。

---

## 7. 开发与调试建议

- 建议在开发阶段提供一个轻量级的 **“DB 浏览/诊断工具”**（例如 Python 脚本或简单 GUI），便于：
  - 查看当前会话与消息记录；
  - 手工修改 `sync_status` 做重试；
  - 排查去重是否生效。
- C++ 与 Python 侧如需调整字段或枚举值，应优先更新本协议文档，再修改代码，保持两边一致。

