## RPA 数据库共享协议（C++ / Python）

本协议约定 **Qt 客户端（C++）** 与 **Python RPA 组件** 通过同一个 SQLite 数据库进行数据交换的字段含义与读写规则，目标是：

- 保证双方对同一条记录有一致的理解；
- 便于在不引入独立后端服务的前提下，实现“外部平台 ↔ 聚合接待界面”的消息闭环。

数据库文件默认路径：项目根目录下的 `database/app.db`

---

## 0. 总体原则（强约束）

为避免 **Python 先写入 `messages`，Qt 再次写入导致重复**，本项目约定：

- **入站（平台 → 聚合界面）**：默认推荐 Python Reader **只写入“入站队列”表**（见第 3 节 `rpa_inbox_messages`），Qt 侧适配器消费后再交给 `MessageRouter` 统一落库到 `conversations/messages`。
- **出站（聚合界面 → 平台）**：Qt 侧 `MessageRouter` **写入 `messages(direction='out') + sync_status=10`**，Python Writer 负责执行发送并更新状态（见第 4.2 节）。

这样可以保证：`conversations/messages` 的最终一致性由 Qt 端单点维护，Python 仅做 I/O 与自动化。

### 0.1 方向B（千牛只读里程碑）特例：Python 直写 `messages`

方向B-只读里程碑为了减少一张队列表与中间转换，可采用“Python 直接写入 `messages(direction='in')`，Qt 侧轮询增量行并走 `MessageRouter` 的去重/会话更新管线”。

该模式的强约束是：

- **Python 只写入 `messages` 的入站行**：`direction='in'`、`sender='customer'`，且必须提供 **稳定唯一** 的 `platform_msg_id`。
- **Qt 侧消费必须幂等**：可重复读取同一批入站行，但不得造成 UI/DB 重复（依赖 `platform_msg_id` 去重）。
- **并发稳定性必须先做**：SQLite 必须启用 WAL，双方必须设置 busy timeout 并使用批量事务写入（见第 3.5 节）。

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
  - `qianniu_pc`：千牛客户端
  - `pdd_web`：拼多多网页
  - `douyin_web`：抖店网页
  -  `weixin_pc`：微信客户端
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
  - Python 端默认不创建/更新 conversations（推荐由 Qt 单点维护）；方向B 如确需直写 `messages`，Python 也不应并发写 `conversations`，由 Qt 侧在消费时 `ensureConversation()` 创建/更新。
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

### 2.1.1 方向B（千牛只读）对 `messages` 的写入规范（Python Reader 必须满足）

当采用“Python 直写 `messages` 入站行”的模式时，Python 写入每条入站消息必须满足：

- **direction**：固定为 `"in"`
- **sender**：固定为 `"customer"`（如后续需要系统消息可用 `"system"`，但里程碑1先不引入）
- **platform_msg_id（强约束）**：必须稳定唯一，用于跨进程去重。建议格式：
  - `qianniu:<shopId>:<platformConversationId>:<ts_ms>:<sha1(content)>`
- **created_at**：尽量写真实时间；若取不到平台时间，可用本地时间字符串，但会影响排序精度
- **platform / platform_conversation_id / customer_name**：当前 `messages` 表未包含这些字段，方向B 下要求 Qt 消费侧能够从 `platform_msg_id` 或其他旁路信息恢复出：
  - `platform='qianniu'`
  - `platformConversationId`
  - `customer_name`

说明：若现有实现需要 `messages` 直接携带平台维度字段（platform、platform_conversation_id 等），应通过数据库迁移扩展字段并在本协议中补齐（不要靠解析 `platform_msg_id` 的字符串来“偷字段”作为长期方案）。

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

## 3. 入站队列表（Python → Qt）`rpa_inbox_messages`

### 3.1 表用途

`rpa_inbox_messages` 是 **Python Reader 写入、Qt 侧平台适配器消费** 的“入站消息队列”。\n
Qt 消费到队列消息后，会将其转换为 `PlatformMessage` 并 `emit incomingMessage()`，随后由 `MessageRouter` 统一写入 `conversations/messages`（并负责 `platform_msg_id` 去重）。

> 备注：本表是“默认推荐方案”。方向B-只读里程碑可以不使用本表，改为 Python 直写 `messages`（见第 0.1 与第 2.1.1）。

### 3.2 建表约定（迁移将由 Qt 端创建）

```sql
CREATE TABLE IF NOT EXISTS rpa_inbox_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  platform TEXT NOT NULL,
  platform_conversation_id TEXT NOT NULL,
  customer_name TEXT NOT NULL,
  content TEXT NOT NULL,
  created_at DATETIME,
  platform_msg_id TEXT NOT NULL,
  consume_status INTEGER NOT NULL DEFAULT 0,
  error_reason TEXT DEFAULT ''
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_inbox_platform_msg_id
  ON rpa_inbox_messages(platform, platform_msg_id);

CREATE INDEX IF NOT EXISTS idx_inbox_consume_status
  ON rpa_inbox_messages(platform, consume_status, id);
```

### 3.3 字段含义与约束

- **platform**：平台标识，推荐直接复用 `conversations.platform`，例如 `qianniu`。\n
  - 注意：方向B里程碑1只接入 `qianniu`。
- **platform_conversation_id**：平台会话标识。\n
  - 里程碑1允许先用“可稳定复现”的拼接键（例如 `buyerNick` 或 `buyerNick|shopName`），后续再替换为更可靠的 id。
- **customer_name**：买家昵称（用于聚合界面会话列表显示）。\n
- **content**：入站消息文本。\n
- **created_at**：消息时间。\n
  - 推荐写入 Python 侧识别到的时间；若暂时拿不到，可写本地时间字符串。
- **platform_msg_id（强约束）**：**必须稳定唯一**，用来彻底避免重复消费与重复入库。\n
  - 推荐格式：`qianniu:<shopId>:<platformConversationId>:<ts_ms>:<sha1(content)>`\n
  - 若短期拿不到 `shopId`，可用占位 `unknownShop`，但 `platformConversationId` + `ts_ms` + `hash` 必须可靠。
- **consume_status**：消费状态（仅用于队列自身，不影响 `messages.sync_status`）：\n
  - `0 = new`：Python 写入完成，等待 Qt 消费\n
  - `1 = consumed`：Qt 已消费并成功交给 `MessageRouter`\n
  - `2 = failed`：Qt 消费/解析失败（`error_reason` 记录原因，可重试）\n

### 3.4 Python Reader 写入规则（必须遵守）

- 只允许 `INSERT` 新行或对 `consume_status=2` 的失败行进行修复性重写；\n
- 不允许修改 `platform_msg_id`（否则会破坏去重）；\n
- 写入使用事务批量提交，避免锁竞争；\n
- 必须做“幂等写入”：建议 `INSERT OR IGNORE`（由 `(platform, platform_msg_id)` 唯一索引保证幂等）。

### 3.5 SQLite 并发与稳定性约定（Python / Qt 都必须配置）

多进程（Python 写、Qt 读/写）共享 SQLite 时，必须执行以下配置，否则极易出现 `database is locked`：

- **WAL 模式**：启用 `PRAGMA journal_mode=WAL;`
- **busy_timeout**：
  - Python：`sqlite3.connect(..., timeout=...)` 且设置 `PRAGMA busy_timeout = ...;`
  - Qt：设置 SQLite busy timeout（连接参数或 PRAGMA）
- **批量事务**：Python Reader 按“每次轮询一批消息 → 单事务提交”，减少锁竞争

---

## 4. C++ 端读写约定

### 4.1 读消息（聚合界面展示）

- C++ 端通过 `MessageDao` / `ConversationDao` 读取：
  - 按 `conversation_id` 查询消息列表时，应无条件包含 `direction in ('in','out')`，不必关心 `sync_status`；
  - UI 展示时，可以选择性根据 `sync_status` 渲染发送状态（后续扩展）。
- `MessageRouter` 收到来自某个适配器的 `PlatformMessage` 时：
  - 依据 `platform` 与 `platformConversationId` 查找/创建会话；
  - 写入一条 `direction = 'in'` 的记录：
    - `sender = 'customer'`；
    - 如有平台侧的消息 id，写入 `platform_msg_id`；
    - `sync_status` 通常直接写为 `1 (normal)`。

### 4.2 写消息（聚合界面发出）

- 聚合界面调用 `sendMessage()` 时，C++ 端应：
  - 在 messages 表中插入一条记录：
    - `direction = 'out'`
    - `sender = 'agent'`
    - `content` 为输入框文本
    - `sync_status = 10 (pending_send)`
  - 更新会话的 `last_message` / `last_time` 等汇总字段。
- C++ 端 **不直接操纵外部平台窗口**，而是只负责把“待发送消息”写入数据库，等待 Python 处理。

---

## 5. Python 端读写约定

### 5.1 Reader（从平台读入 → 写入 messages）

Reader 负责从千牛客户端 / 拼多多网页中抓取到新消息，并将其写入 SQLite。

- **默认推荐落点**：写入 `rpa_inbox_messages`（第 3 节），再由 Qt 消费并统一落库（更清晰、字段更齐）。
- **方向B-只读里程碑落点**：允许直接写入 `messages(direction='in')`（见第 2.1.1 的写入规范）。

两种落点的共同硬要求是：`platform_msg_id` 稳定唯一、幂等写入、批量事务、WAL+busy_timeout。

### 5.2 Writer（从 messages 读出 → 写回平台）

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

## 6. 时间与时区约定

- 所有 `DATETIME` 字段（`created_at`, `last_time` 等）目前均采用 **本地时间** 存储；
- C++ 与 Python 端应统一使用同一时区（通常为操作系统当前时区），不做额外的 UTC 转换；
- 未来如需要跨时区或服务器部署，再统一升级为 UTC 存储 + 本地显示换算。

---

## 7. 平台与扩展

- 目前平台枚举建议：
  - `qianniu`：千牛桌面客户端（RPA 主战场）；
  - `pdd_web`：拼多多网页；
  - `simulator`：现有模拟器平台（可继续使用，便于开发调试）。
- 新增平台时，应在：
  - 文档中补充平台标识字符串；
  - Python 端 Reader / Writer 中增加对应实现；
  - 如需特殊字段或行为，在本协议文件中追加说明。

---

## 8. 开发与调试建议

- 建议在开发阶段提供一个轻量级的 **“DB 浏览/诊断工具”**（例如 Python 脚本或简单 GUI），便于：
  - 查看当前会话与消息记录；
  - 手工修改 `sync_status` 做重试；
  - 排查去重是否生效。
- C++ 与 Python 侧如需调整字段或枚举值，应优先更新本协议文档，再修改代码，保持两边一致。

