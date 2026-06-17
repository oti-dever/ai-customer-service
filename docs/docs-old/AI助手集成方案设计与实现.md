# 内置 AI 助手集成方案设计与实现

本文档基于前期讨论整理，描述在**不依赖各平台消息 API、不强制与 RPA 自动回复联动**的前提下，将大模型能力以「软件内置帮助助手」形式接入主程序的**第一版**设计与后续演进方向；并**记录截至文档更新时的实现进度**，便于评审与迭代对齐。

文件名建议与本文标题一致：`AI助手集成方案设计与实现.md`。

---

## 1. 背景与目标

### 1.1 背景

- 项目名称含「AI 客服」，业务主线当前为**多平台会话聚合 + RPA 读写**；各平台侧通常**无**可用消息类 API。主程序已通过 **「机器人管理」内置 AI 助手**提供 OpenAI 兼容对话（本机 Key），与 RPA **解耦**。
- 微信 / 千牛 / 拼多多等平台**无可用或难以申请**官方消息类 API，故采用 RPA；**AI 能力与 RPA 解耦**，可独立迭代。
- 用户规模预期较小（约十至数十人），无需首版即建设团队级 API 网关。

### 1.2 第一版目标

在主窗口 **「机器人管理」** 入口对应的中心区域，提供：

1. **与 AI 的对话界面**：用户可就**本软件的使用方式、功能说明**等提问，模型生成回答。
2. **可配置的 HTTP API**：默认对接 **DeepSeek**（OpenAI 兼容协议），便于后续更换为其他兼容端点。
3. **流式输出**：首字尽快可见，助手回复以「打字机」效果逐段展示（SSE 流式）。
4. **本机 API Key**：由**每位用户自行**在 DeepSeek 开放平台创建 Key，在本机配置中填写与保存；**不与 RPA、聚合会话中的客户聊天自动联动**。

### 1.3 设计原则

- **由浅入深**：先做好「产品内帮助助手」，再考虑会话摘要、回复建议、与聚合会话结合等。
- **边界清晰**：助手对话内容发往模型服务商；与微信 / 千牛等窗口内的客户消息**相互独立**，首版**不**自动向客户发送模型生成内容。
- **与官方安全提示一致**：Key **不写入源码、不进入版本库、不暴露在可被公开爬取的客户端代码中**；用户自备 Key、本机保管，与 DeepSeek 控制台「勿分享、勿暴露在浏览器/客户端代码」的表述一致（指勿公开传播与硬编码，而非禁止桌面应用在本地保存用户自填的密钥）。

---

## 2. 范围与非目标

### 2.1 范围内（第一版）

| 项 | 说明 |
|----|------|
| 入口 | 主窗口侧栏「机器人管理」（`platformId: robot`）→ **`RobotAssistantWidget`**（已实现，不再使用占位页）。 |
| 布局 | **Tab**：一页为**对话区**，一页为**设置区**（`QTabWidget` 或等价实现）。 |
| 对话 | 多轮对话 UI（历史展示、输入、发送）；助手侧支持流式追加文本；**对话已落库**（`ai_assistant_sessions` / `ai_assistant_messages`，按 `user_id` + **预设 `model_key`** 分会话线），重启可恢复；请求侧 **§10.2 十轮帽** 已实现。**豆包** 支持 **图片 + 文字** 多模态发送与展示（见 **§5.6**）。 |
| 配置 | **按模型预设分栏**：对话页与设置页均有 **模型下拉**（项同步），每个预设独立保存 **Base URL、模型名、API Key**（`QSettings` 分组 `ai/presets/<槽位>`；旧版全局 `ai/baseUrl` 等首次启动时迁移至 DeepSeek 预设）。「保存设置」「测试连接」针对**当前所选预设**的表单值。 |
| 协议 | OpenAI 兼容 `chat/completions`，`stream: true` 时使用 SSE 解析。**豆包（方舟）** 纯文本同协议；**看图/多模态** 须 `content` 多段，详见 **§5.5**。 |
| 系统提示词 | 约束助手角色为「本软件内置帮助」；首版可先使用 **§8.1 简明文案**，再随《软件功能清单》精修。 |
| 流式交互 | **不提供「停止生成」**；请求失败或流中断时 **保留已输出的半段正文**，并另附简短错误说明（见 §7）。 |
| 主题 | **随主窗口主题切换**；列表、输入区、气泡等**样式参考聚合会话界面**（`AggregateChatForm` + `ApplyStyle::MainWindowTheme`）。 |

### 2.2 非目标（第一版不做或明确延后）

- 不向各平台 RPA **自动投递**模型生成的话术。
- 不在首版实现 RAG / 向量库；产品知识以 **system prompt 内嵌摘要** 或资源内文本为主即可。
- 不建设公司统一 API 代理、不按工号集中计费与审计（用户量较小时由各人自行管理 DeepSeek 账号与用量）。
- 不强制 Markdown 富文本渲染流式过程（首版助手气泡内 **纯文本** 流式即可，降低复杂度）。
- 首版**不做**「停止生成」按钮（后续按需增加）。**DeepSeek、豆包（火山方舟）** 已可配置并对话；**通义千问** 仍为占位项。**按厂商完全分栏的 Key 存储**（与仅按预设分组的当前实现）若需统一，见 **§13.4**；完整定案仍见 **§10**。

---

## 3. 总体架构

```
┌─────────────────────────────────────────────────────────┐
│  MainWindow（Qt）                                        │
│  侧栏「机器人管理」 → RobotAssistantWidget（新）           │
│    └─ Tab：「对话」|「设置」                               │
│         · 对话：标题行模型下拉 + 历史（SQLite）+ 流式气泡 + 发送/清空 │
│         · 设置：API Key、Base URL、模型、保存、测试连接等    │
└───────────────────────────┬─────────────────────────────┘
                            │ HTTPS（JSON / SSE）
                            ▼
              DeepSeek / 火山方舟（豆包）等 OpenAI 兼容 API
```

- **网络层**：建议使用 `QNetworkAccessManager` 发起 POST；流式响应在 `readyRead` 中增量读取，**UI 更新限定在主线程**（信号槽 `Qt::QueuedConnection` 或 `QMetaObject::invokeMethod`）。
- **数据**：助手对话**已写入 SQLite**（与聚合 `messages` 表分离）；**会话键**由标题栏**模型预设**决定（见 **§10.8**）。API 配置（Base URL / 模型名 / Key）按**当前所选预设**从 **`QSettings`** 分组读取；切换预设时表单与对话线同步切换（见 **§10.5**）。
- **密钥**：写入 `QSettings`（及可选加密，与现有 `cryptoutil` 能力对齐）；**禁止**将 Key 写入日志、崩溃上报或源码。

---

## 4. 与 RPA、聚合会话的关系

| 模块 | 关系 |
|------|------|
| RPA（微信 / 千牛 / 拼多多等） | **无直接依赖**。助手仅服务「用户 ↔ 本软件」的问答。 |
| 聚合会话 | 首版**不读取**当前选中会话上下文发往模型；后续若做「回复建议」，再单独立项。 |
| MessageRouter / IPlatformAdapter | 首版**不修改**消息路由；避免与平台消息混流。 |

---

## 5. API 与配置项建议

### 5.1 默认与可选项

- **Base URL / 模型**：DeepSeek 侧默认值见下文 **§5.4**；**豆包（火山方舟）** 见 **§5.5**；实现中仍应允许用户覆盖（其它 OpenAI 兼容端点）。
- **Authorization**：`Bearer <API Key>`，与 OpenAI 兼容格式一致。

### 5.2 请求体要点

- `messages`：至少包含一条 `system`（角色与知识边界） + 多轮 `user` / `assistant`。
- **`user` 的 `content`**：可为 **字符串**（纯文本）；多模态时为 **JSON 数组**，元素含 `type: "image_url"` + `image_url.url`（如 `data:image/…;base64,…`）及可选 `type: "text"` + `text`（与 OpenAI / 方舟 Chat Completions 兼容写法一致）。
- `stream: true`：第一版即采用流式；需处理 SSE 行协议、`data: [DONE]` 结束、单行 JSON 增量中的 `choices[].delta.content`。

### 5.3 错误与中断

- 非 2xx：展示可读错误（与 **§5.4 错误码** 映射）；**不**清空本轮已流式输出内容（见 §7）。
- 流中途断连 / 解析失败：**保留助手气泡内已生成的半段文字**；在同一气泡末尾追加简短说明，或使用状态栏 / 独立一行提示「后续内容未能完成」，避免用户误以为整段作废。
- 首版**不提供「停止生成」**；若后续增加，再对 `QNetworkReply::abort()` 与 UI 状态单独立项。

### 5.4 DeepSeek 官方摘要（实现时与文档同步核对）

以下摘录自 [DeepSeek API 文档（中文）](https://api-docs.deepseek.com/zh-cn/) 及站内子页，便于开发与联调；**价格与模型细节以官网最新页为准**。

| 项 | 说明 |
|----|------|
| 协议 | 与 **OpenAI API 兼容**；`POST` 对话接口示例见 [首次调用 API](https://api-docs.deepseek.com/zh-cn/)。 |
| `base_url` | 文档表格式默认 **`https://api.deepseek.com`**；亦可设为 **`https://api.deepseek.com/v1`**（**`v1` 与模型版本无关**，仅为兼容习惯）。拼接路径时注意勿重复 `/v1`。 |
| 对话路径 | 文档示例为 **`/chat/completions`**（完整 URL 形如 `https://api.deepseek.com/chat/completions`）。 |
| 认证 | Header：`Authorization: Bearer <API_KEY>`；密钥在 [API keys](https://platform.deepseek.com/api_keys) 创建。 |
| 流式 | 请求体中 **`"stream": true`**；响应为 SSE。自研解析时除 `data: {...}`、`data: [DONE]` 外，须处理下文 **限速与心跳**。 |

**推荐默认模型（内置帮助场景）**

- **`deepseek-chat`**：DeepSeek-V3.2 **非思考模式**，**128K** 上下文（与 APP/WEB 版不同）；适合一般说明与操作问答。详见 [模型 & 价格](https://api-docs.deepseek.com/zh-cn/quick_start/pricing)。
- **`deepseek-reasoner`**：同版本 **思考模式**；输出上下文更长，成本与延迟通常更高，按需选用。

**HTTP 错误码（产品文案可映射）** — 摘自 [错误码](https://api-docs.deepseek.com/zh-cn/quick_start/error_codes)

| 状态码 | 含义（简述） |
|--------|----------------|
| 400 | 请求体格式错误 |
| 401 | API Key 错误 / 认证失败 |
| 402 | 余额不足 |
| 422 | 请求参数错误 |
| 429 | TPM 或 RPM 达到速率上限 |
| 500 | 服务器内部故障 |
| 503 | 服务器繁忙 |

**限速与流式心跳** — 摘自 [限速](https://api-docs.deepseek.com/zh-cn/quick_start/rate_limit)

- 官方**不限制用户并发**；高负载时请求可能排队，连接会保持。
- **非流式**：可能持续收到**空行**。
- **流式**：可能持续收到 SSE 注释行 **`: keep-alive`**。
- 自解析 HTTP/SSE 时：**应忽略**上述空行与 `: keep-alive`，仅处理有效 `data:` 行。
- 若 **10 分钟**仍未开始推理，服务器将**关闭连接**；客户端需超时提示与可重试策略。

### 5.5 豆包（火山方舟）接入要点

以下与 [火山引擎方舟](https://www.volcengine.com/docs/82379) 公开文档及《豆包大模型 1.8》《图片理解》《多模态理解》等说明对齐，供将内置助手下拉 **「豆包」** 从占位改为可用时实现；**计费、限流、模型列表以控制台与官网最新页为准**。

| 项 | 说明 |
|----|------|
| 产品形态 | **火山方舟**提供对话能力；调用方式为 **HTTPS + API Key**，与 DeepSeek 一样可走 **OpenAI 兼容** 习惯，但**多模态请求体**与「纯字符串 `content`」不同，需在客户端显式支持（见下）。 |
| Base URL | 文档示例多为 **`https://ark.<region>.volces.com/api/v3`**（如 **`https://ark.cn-beijing.volces.com/api/v3`**）。拼接 **`/chat/completions`** 即 Chat Completions；**勿**与 DeepSeek 的 `api.deepseek.com` 混用。 |
| 鉴权 | Header：**`Authorization: Bearer <ARK_API_KEY>`**；Key 在方舟控制台创建，**本机保存策略与 §6 一致**（不入库、不打印日志）。 |
| `model` | 填控制台 **推理接入点 ID**（常见形如 **`ep-…`**），或文档给出的 **`doubao-seed-…`** 等 **Model ID**（如 **`doubao-seed-1-8-251228`**）；**以创建接入点时的可选模型列表为准**。 |
| 纯文本对话 | 与现有 **`OpenAiCompatClient`** 一致：`messages` 中 `content` 为**字符串**即可（`stream: true` 时仍按 SSE 解析，具体字段以方舟返回为准）。 |
| 图片理解 / 图文问答 | **必须**使用**多模态** `content`：**数组**，内含 **`image_url`（或方舟文档中的等价结构）** 与 **`text`**。方舟文档另提供 **Responses API**（`/api/v3/responses`），请求体为 **`input`** 多段（如 **`input_image` + `input_text`**）；若首版只接 Chat Completions，则采用 **OpenAI 兼容多段 `content`** 与官方示例对齐。 |
| 图片数据来源 | **公网 URL**、**Base64**（`data:<mime>;base64,…`，注意单张与请求体大小限制）、或 **Files API 上传后使用 `file_id`** / SDK 支持的 **`file://` 本地路径**（依文档与 SDK）；内置助手 UI 需 **选图或粘贴路径** 再组装进请求。 |
| 多轮与图 | **Chat Completions 无状态**：若多轮对话都针对**同一张图**，文档要求**每次请求都带上图片信息**（或改用平台推荐的上下文方式，以最新文档为准）。 |
| 与当前工程（已实现） | **`OpenAiCompatClient`** 仍发送 **JSON `messages`**，其中 `content` 可为 **字符串或数组**（由 `RobotAssistantWidget::buildMessagesForRequest` 组装）。**落库**：多模态用户轮次使用 **单行 `content` 紧凑 JSON**（字段含本地路径与可选文字），界面拆成 **两条用户气泡**（先图后文）；请求时再拼为 **一段 `user` + 多段 `content`**，**十轮**仍按 **user/assistant 各一条计一轮**（不因双气泡加倍）。 |
| 能力边界 | 豆包大模型能力表含 **图片理解**；多模态文档中的 **视觉问答、图像描述** 即 **根据图生成文字回复** 或 **图 + 问题联合回答**，与产品目标一致。深度思考、工具调用、Responses 专属字段等**可选**，首版助手可只做 **流式文本输出** 与多模态输入。 |

### 5.6 内置助手：豆包多模态图片（已实现）

| 项 | 说明 |
|----|------|
| 入口 | 对话页输入框 **上方** 工具栏：**图片**（`:/picture_icon.svg`）、**文件**（`:/file_icon.svg`，占位提示后续扩展）。 |
| 适用范围 | **仅当** 模型为 **豆包（火山方舟）**（`model_key`：`doubao:ark`）时可点选图片；切换至其他预设时清除待发送图片。 |
| 待发送 | 选图后显示 **缩略图 + 文件名 +「移除」**；可与输入框文字 **组合发送**（仅图、仅字、或图+字均可；豆包下 **无文字仅有图** 亦可发送）。 |
| 单张限制 | 实现侧约 **5MB** 上限；图片以 **`data:<mime>;base64,…`** 填入 `image_url.url`。 |
| 展示 | 发送后 **两条己方气泡**：先 **图片**，再 **文字**（无文字则仅图片气泡）。 |
| 资源 | `resources/picture_icon.svg`、`resources/file_icon.svg`、`resources/doubao_icon.png` 等已入 **`app_resources.qrc`**。 |

**关联文档**：《千牛RPA优化方案设计与实现.md》（聊天区截图路径 + 聚合/助手发图场景）、《AI辅助回复方案设计与实现.md》（聚合侧 AI 辅助）。

---

## 6. 密钥与安全

### 6.1 策略定案

- **每人使用自己的 API Key**，在本机「机器人管理 / 助手设置」中填写。
- Key **仅本机持久化**（加密存储为推荐项），**不**通过团队共享配置文件、聊天、邮件明文分发。

### 6.2 工程约束

- `.gitignore` 覆盖一切可能落 Key 的本地配置路径（若未来有独立配置文件）。
- 日志与 `qDebug`：**禁止**打印完整 Key 或 `Authorization` 头。
- 产品内简短提示：请勿将 API Key 告知他人或提交到公开代码库；与开放平台说明一致。

### 6.3 后续若规模扩大

当需要「公司统一 Key、员工不可见、集中限流与审计」时，再引入**自建后端代理**（客户端不再持有主 Key），本文档第一版不要求实现。

---

## 7. 用户体验要点

### 7.0 设计与交互定案

| 项 | 定案 |
|----|------|
| 布局 | **对话区 + 设置区** 使用 **Tab** 切换，同处「机器人管理」中心页内。 |
| 对话持久化 | **已实现落库**与按 **`model_key`** 分会话线；清空为物理删子表消息、保留 session 行（**§10.6**）。标题栏与 **设置页顶部**均有 **模型下拉**（选项同步）：**DeepSeek**、**通义千问（即将支持）**、**豆包（火山方舟）**（**§10.4**）。**各预设独立 URL / 模型 / Key**（**§10.5**）。 |
| 流式 | **无「停止生成」**；失败或中断时 **保留已输出半段**，并给出简短错误提示（勿整段替换为空）。 |
| 主题与样式 | **随主窗口主题变化**；具体控件配色、列表与输入区风格 **对齐聚合会话**（`aggregatechatform.cpp` / `ApplyStyle::mainWindowStyle` 等现有实现），保证同窗口内观感一致。 |

### 7.1 对话

- 用户问题与助手回复分区展示；助手回复**流式逐字/逐段**显示。
- **助手身份展示**：按当前会话 **`model_key`** 显示助手 **昵称**（如 DeepSeek / **doubao**）与 **头像**（`deepseek_icon.png` / **`doubao_icon.png`** 等），与下拉所选厂商一致。
- **豆包多模态**：输入区上方 **图片 / 文件** 按钮；选图后见 **§5.6**。发送后用户侧可呈现 **图片气泡 + 文字气泡** 两条。
- **清空会话**：清空内存与界面，并对当前 `session_id` **物理删除**子表消息（**§10.6**）；**仅影响当前下拉所选槽位**；同时清除**待发送图片**。单条重试等放后续。

### 7.2 设置

- **模型**下拉与对话页一致；切换预设时加载该预设的 **Base URL、模型名称、API Key**（切换前将当前编辑写回上一预设）。
- API Key 输入框使用密码样式（`Password` 模式）。
- 「测试连接」：对**当前表单**中的 URL / model / Key 发最小请求；成功 / 失败提示与 §5.4 / 方舟侧错误文案一致。

### 7.3 隐私告知

在**设置 Tab** 内用简短标签说明（无需单独弹窗强制首次）：

- 为获得回答，**用户输入、所选图片（经编码后）与部分上下文会发送至所配置的 API 服务商**（如 DeepSeek、火山方舟等，取决于所选模型）；
- 该通道**与**微信、千牛等平台内的**客户聊天无关**，也**不会**自动将助手内容发给客户。

---

## 8. 系统提示词与知识来源（第一版）

### 8.1 首版简明文案（可先直接使用，发版前再按《软件功能清单》扩充）

**实现侧说明（与下述示例的关系）**：代码中 **`systemPromptForRequest()`** 使用 **中文** 内置文案（软件功能摘要 + 角色风格），并按所选预设 **追加**「【关于你的身份与底层模型】」段，使 **DeepSeek** 与 **豆包** 在被问及模型身份时与界面一致；**不以**下方英文示例为运行时唯一来源。

以下为**占位级**英文 system 示例（历史文档保留；若与代码不一致，以 **C++ 内嵌 `systemPromptForRequest` 文案** 为准）；**功能摘要**可先写短段落，避免首版阻塞在长篇文档提炼上。

**System 角色（示例）**

```text
你是桌面应用「AI 客服」的内置帮助助手。你只回答与本软件相关的使用问题：例如如何登录、如何添加/管理各平台窗口、聚合会话、RPA 启动与控制台、主题与设置等。若用户问题与软件无关，礼貌说明无法回答并建议查看软件内帮助。回答简洁、分步说明操作路径；不要编造不存在的菜单或按钮名称。
```

**功能摘要（示例，请替换为真实能力列表）**

```text
【软件功能摘要 - 请随版本更新】
- 支持多平台客服窗口聚合管理（如微信、千牛、拼多多等，以实际已接入为准）。
- 提供聚合会话视图、RPA 启动/停止与管理、控制台日志查看。
- 支持主题切换与个人账户相关设置。
- 内置本助手：仅用于解答本软件用法，不会自动向各平台客户发送消息。
```

将上述两段**合并为一条** `role: system` 的 `content`（或拆成一条角色 + 一条知识，若所用 API 对多条 system 有限制则必须合并为一条）。

### 8.2 后续增强

- 从《软件功能清单》全文提炼更长、可维护的摘要，放入 `:/` 资源或独立 txt。
- RAG、Dify/FastGPT 等与《技术选型》中长期规划对齐。

---

## 9. 与现有文档的衔接

- 《技术选型》中已提及 OpenAI 兼容 API 与 JSON 交互，本方案与之**一致**，首版落地在「机器人管理」场景。
- 《软件功能清单》中「简易 AI / AI 配置」等条目，可在本功能上线后**回填实现状态**与截图说明。

---

## 10. 对话持久化与多模型（方案定案与实现进展）

本节为产品与技术**定案**，并标注**截至文档更新时**在仓库中的**落地情况**；细节与文件索引以 **§13** 为准。

**已实现**：库表、`AiAssistantDao`、`RobotAssistantWidget` 读写库、请求侧十轮截断、**对话/设置双处模型下拉同步**、**按预设分组的 QSettings**（含旧配置迁移）、**DeepSeek + 豆包（火山方舟）** 可配置可对话、**豆包多模态图片**（§5.6）、**助手气泡昵称/头像随预设**、`PRAGMA foreign_keys=ON`。

**未实现或仍占位**：通义千问真实接入；**按厂商完全分栏的 Key**（与当前「按预设分组」二选一即可）；「自定义 OpenAI 兼容端点」独立项；`updated_at`；QSettings 记住上次所选模型下拉；**文件**附件（仅 UI 占位）等（见 **§13.4**）。

### 10.1 三层区分（避免混淆）

| 层级 | 含义 |
|------|------|
| **界面聊天记录** | 用户看到的列表 / 气泡；可全量落库便于回看。 |
| **API 上下文** | 单次请求里 `messages` 数组实际带给模型的内容；**仅这一部分消耗上下文长度与计费相关 token**。 |
| **数据库存储** | 持久化载体；**不会自动占用上下文**，除非实现里把库中记录**无截断地**全部拼进每次请求。 |

**结论**：持久化后，**下次启动是否「带着上次聊天当上下文」**，完全由客户端**从库中取多少条参与 `buildMessages()`** 决定，而非「只要入库就占满窗口」。

### 10.2 请求侧：最多保留 10 轮

- **「10 轮」定义（建议写死）**：最近 **10 个 user↔assistant 来回**（即最多 10 条 `user` + 10 条 `assistant`，成对截断；不含 `system`）。
- **库中**：同一逻辑会话可**全量保存**（展示与审计）；**每次请求**只取该会话时间序下的**尾部 10 轮**拼入 `messages`，再加固定 `system`。
- **后续优化**：若单轮极长，可再叠加 **token / 字符二级上限**；首版可只做「轮数帽」。

### 10.3 数据模型（逻辑分会话，物理单表即可）

- **不必**按会话做「物理分表」（多张同结构表）。与现有聚合侧表名区分：聚合使用 `conversations` / `messages`，内置助手使用下文两张表。
- **用户维度**：按 **`user_id`**（`INTEGER`，外键引用 **`users(id)`**）隔离；**不**使用 `username` 文本作会话归属键。
- **会话线语义**：每个 **`(user_id, model_key)`** 对应**至多一行** `ai_assistant_sessions`；切换 UI 下拉模型 = 换到该用户下另一 `model_key` 对应的会话行并加载其子消息。
- **清空不可恢复**：**不做**软删、回收站或历史版本表。清空 = **物理删除**该 `session_id` 下 `ai_assistant_messages` 全部行，**保留** `ai_assistant_sessions` 同一行（同一 `id`），便于后续继续对话而无需重建会话元数据。

#### 10.3.1 表 `ai_assistant_sessions`

| 列名 | 类型 | 说明 |
|------|------|------|
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | 本地会话主键；子表引用此列。 |
| `user_id` | INTEGER NOT NULL | 外键 → `users(id)`。 |
| `model_key` | TEXT NOT NULL | 稳定槽位键，与 **§10.5** 一致，如 `deepseek:deepseek-chat`、`custom:别名`。 |
| `created_at` | DATETIME DEFAULT CURRENT_TIMESTAMP | 与库内其它表风格一致。 |
| `updated_at` | DATETIME | **可选**：最后一条消息时间；若实现中一律用子查询 `MAX(created_at)` 维护展示顺序，可省略本列。 |

**约束**：`UNIQUE(user_id, model_key)`。

**说明**：不在此表冗余 `provider` / `base_url` / 展示标题等，避免与设置项双源；需要时由 `model_key` + `QSettings`（或后续配置表）解析。

#### 10.3.2 表 `ai_assistant_messages`

| 列名 | 类型 | 说明 |
|------|------|------|
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | |
| `session_id` | INTEGER NOT NULL | 外键 → `ai_assistant_sessions(id)`，**ON DELETE CASCADE**（若选择「删会话行」路径时可一并删消息；**清空会话**路径为仅删子行、保留会话行，见上）。 |
| `role` | TEXT NOT NULL | 仅 `user` / `assistant`。**`system` 不落库**，请求前由客户端拼接。 |
| `content` | TEXT NOT NULL | 纯文本用户句 **或** 多模态轮次的 **紧凑 JSON**（见 **§5.6**）；助手侧仍为纯文本。 |
| `created_at` | DATETIME DEFAULT CURRENT_TIMESTAMP | 会话内排序；首版可与 `id` 增序一致，无需额外 `seq`。 |

**索引**：`CREATE INDEX … ON ai_assistant_messages(session_id, id)`（或 `(session_id, created_at)`），便于按会话拉全量及取时间序尾部 **§10.2** 十轮。

**与聚合 `messages` 表**：字段语义不同（聚合用 `direction` / `conversation_id` 等）；助手侧保持 **`role` + `session_id`**，避免混用命名。

### 10.4 多模型：切换即换「对话线」

- **原则**：**切换模型 ⇔ 强制进入另一条会话线**（语义上等同聊天软件里「和不同的人聊天」——此处 **人 = 预设模型槽位**）。
- **UI（已落地）**：对话区标题行 **「内置 AI 助手」右侧** 为 **`QComboBox`**（`robotAssistantModelCombo`），与标题同一行；**默认第一项 DeepSeek**。
- **行为（已落地）**：
  - 切换下拉 → **不删除**其它 `model_key` 已持久化的记录；**`rebindSessionFromCurrentConfig()`** 从 DB 加载当前项对应会话并**重绘气泡**。
  - **不点「清空会话」**：当前槽位对话 **跨重启保留**（请求仍受 **§10.2 十轮帽**）。
  - **「清空会话」**：仅 **DELETE** 当前 `session_id` 下 `ai_assistant_messages`（**§10.6**）。
- **通义千问（即将支持）**：`model_key` 为 **`qwen:placeholder`**，`available=false`，不发起请求。
- **豆包（火山方舟）（已接通）**：`model_key` 为 **`doubao:ark`**，`available=true`；需填写方舟 **Base URL**、**接入点 / Model ID**、**API Key**；支持 **图片多模态**，见 **§5.5、§5.6**。

### 10.5 模型列表与配置（分期）

**当前代码（已落地）**

- 下拉三条：**DeepSeek**（`deepseek:deepseek-chat`，`available=true`）、**通义千问**（`qwen:placeholder`，占位）、**豆包（火山方舟）**（`doubao:ark`，`available=true`）。
- **会话键**：DeepSeek **`deepseek:deepseek-chat`**；豆包 **`doubao:ark`**；与早期的 `compat:<host>:<model>` 推导键若并存则为**不同会话线**（不做自动迁移）。
- **HTTP 请求**：读取**当前预设**在设置页中的 **Base URL、模型名、API Key**（`OpenAiCompatClient` + `QSettings` 分组 **`ai/presets/<槽位>`**）。首次启动将旧版全局 **`ai/baseUrl` / `ai/model` / `ai/apiKey`** 迁移入 DeepSeek 预设（若预设键尚未写入）。
- **切换预设**：先 **保存当前三个编辑框** 到上一 `sessionModelKey`，再加载新预设的默认值或已存值；对话页与设置页 **下拉索引联动**。

**下一版（待实现，与 §13.4 对齐）**

- **通义千问**：接入各厂商 **OpenAI 兼容** Base URL 与默认 model；`available=true`，稳定 `model_key`。
- **API Key**：可选 **按厂商分栏**（与「按预设分组」并存策略产品定夺）。
- **自定义端点**：用户在设置中填写 URL/model，数据层映射为稳定 **`modelKey`**（如 `custom:<别名>`），下拉增加一项或独立入口。

**实现层提示**

- 每个可选模型对应稳定 **`modelKey`**，用于 **`ai_assistant_sessions.model_key`**；协议非兼容时再引入适配器，与 session 存储解耦。

### 10.6 「清空会话」与上下文

- 用户预期：**清空 = 与当前模型这段对话重新开始**。
- 实现上应同时满足：**UI 清空**、**内存中的当前会话历史清空**、**持久化层中对当前 `session_id` 执行 `DELETE FROM ai_assistant_messages WHERE session_id = ?`（物理删除，不可恢复）**；**保留** `ai_assistant_sessions` 对应行。之后下一次请求 **不得再带上**已清空内容。

### 10.7 边界（首版可简化）

- **同一预设模型、不同 API Key**：首版可视为 **同一条对话线**（不按 Key 拆分）；若以后要按账号隔离，再引入 `modelKey + keyId` 等扩展。
- **切换模型时输入框未发送内容**：首版可 **共用同一输入框**（全局一份草稿）；若需「每个模型独立草稿」，后续再加。

### 10.8 实现进展摘要（与代码同步）

| 项 | 状态 |
|----|------|
| `ai_assistant_sessions` / `ai_assistant_messages` + 迁移 + `FOREIGN KEY` + WAL 同库 | 已落地（`database.cpp`） |
| `AiAssistantDao`：`ensureSession` / `listMessages` / `appendMessage` / `clearMessages` | 已落地 |
| 请求组装：尾部最多 **10 轮** user/assistant + `system` | 已落地（`RobotAssistantWidget::buildMessagesForRequest`） |
| 标题栏 + 设置页 **模型下拉同步**；DeepSeek 默认；千问占位、**豆包可对话** | 已落地 |
| 会话 `model_key`：`deepseek:deepseek-chat` / `qwen:placeholder` / **`doubao:ark`** | 已落地 |
| **按预设分组** `QSettings`、旧全局键迁移 | 已落地 |
| **豆包多模态**：工具栏选图、`content` JSON 落库、请求内 `image_url`+`text`、双气泡展示 | 已落地（**§5.6**） |
| 助手气泡 **昵称 + 头像** 随预设（含 **`doubao_icon`**） | 已落地 |
| **system** 按预设追加身份说明（`systemPromptForRequest`） | 已落地 |
| 预设自动写回设置框默认值、**厂商分栏 Key**、自定义 preset、`updated_at`、记住所选模型 | 未落地或部分未落地 |
| **文件**附件（仅按钮占位） | 未落地 |

---

## 11. 后续演进（路线图参考，非承诺排期）

1. **多模型接通与配置产品化**（接续 **§10.5、§13.4**）：**豆包与按预设配置已落地**（§5.5、§5.6、§10.5）；**通义千问** OpenAI 兼容参数仍待接；可选同步设置默认值、**厂商分栏 Key** 或自定义 `modelKey`。
2. **流式「停止生成」**、单条重试、更细的错误恢复。
3. **帮助中心 / 其它入口**复用同一套 API 客户端与配置。
4. **聚合会话**：仅「生成回复草稿」、人工确认后发送（仍不默认自动发 RPA）；**产品与技术定案**见 **`AI辅助回复方案设计与实现.md`**。**千牛聊天区截图入站 + 聚合多模态** 与 **`千牛RPA优化方案设计与实现.md`** 衔接（复用本文 **§5.5～§5.6** 豆包请求体）。
5. **会话摘要、标签、待办**等基于本地 `MessageRecord` 的离线/异步调用。
6. **团队代理与统一计费**（按需）。

---

## 12. 可实施具体步骤

以下按**推荐开发顺序**排列，便于拆 issue / 迭代；每一步完成后应可编译运行并通过对应自测。

### 阶段 A：工程与模块骨架

| 步骤 | 内容 | 说明 |
|------|------|------|
| A1 | 增加 Qt Network 依赖 | 在根目录 `CMakeLists.txt` 的 `find_package(Qt6 …)` 与 `target_link_libraries` 中加入 **Qt6::Network**（`QNetworkAccessManager`、`QNetworkReply` 等在此模块）。 |
| A2 | 新增源码文件并纳入构建 | 建议路径示例：`src/ui/robotassistantwidget.h/.cpp`（助手整页 UI）；可选 `src/services/ai/openaicompatclient.h/.cpp`（纯网络与 SSE 解析，无 Qt Widgets 依赖，便于单测）。将路径加入 `SOURCES` / `HEADERS`。 |
| A3 | 定义配置键与默认值 | 使用 `QSettings`，统一组织名/应用名与现有项目一致；键名示例：`ai/baseUrl`、`ai/model`、`ai/apiKey`（Key 存盘前可走 `cryptoutil` 加密，见阶段 D）。默认值写入 DeepSeek 官方兼容 Base URL 与推荐模型 id（以当时文档为准，**勿**在代码里写死真实 Key）。 |

### 阶段 B：主窗口接入「机器人管理」

| 步骤 | 内容 | 说明 |
|------|------|------|
| B1 | 拆分 `robot` 与 `manage` 分支 | 在 `mainwindow.cpp` 的侧栏切换逻辑中，**仅当** `platformId == "robot"` 时切换到助手页；`manage` 可继续保持占位或其它页面。 |
| B2 | 中心区域挂载助手控件 | 与 `m_centerStack`、`AggregateChatForm` 类似：在 `MainWindow` 构造或初始化流程中 `addWidget` 助手页，索引固定，避免与 index 0/1 冲突；选中 `robot` 时 `setCurrentWidget`。 |
| B3 | 主题与样式 | 助手页实现 `applyTheme(MainWindowTheme)`（或与 `AggregateChatForm` 相同模式），在 `MainWindow::applyMainWindowTheme` 中**一并调用**；对话区列表/输入框/气泡颜色与圆角等**参考聚合会话**现有样式，必要时在 `applystyle.cpp` 增加带 objectName 选择器的片段，避免硬编码与主窗口脱节。 |

### 阶段 C：设置区 UI 与持久化

| 步骤 | 内容 | 说明 |
|------|------|------|
| C1 | 设置控件 | Base URL（`QLineEdit`）、模型名（`QLineEdit`）、API Key（`QLineEdit` + `Password`）、保存按钮；只读说明标签（隐私与「勿泄露 Key」短文案）。 |
| C2 | 加载 / 保存 | 打开页面时从 `QSettings` 读取；保存时写回；API Key 若加密存储，与 `cryptoutil` 加解密约定好版本字段，避免升级后无法解密。 |
| C3 | 「测试连接」 | 发送一次 **非流式**或**短流式**请求（最小 `messages`，如 `user: "ping"`），成功则 `QMessageBox` 或状态栏提示成功；失败展示 HTTP 状态与响应体摘要（**脱敏**，不打印 Key）。 |

### 阶段 D：对话区 UI 与多轮状态

| 步骤 | 内容 | 说明 |
|------|------|------|
| D1 | Tab 与对话布局 | 根控件用 **`QTabWidget`**：Tab1「对话」— 上方可滚动区域展示历史（用户 / 助手气泡或列表项），下方多行输入 + 发送 + 「清空会话」；Tab2「设置」— 对应阶段 C 表单。 |
| D2 | 内存模型 + 落库 | UI 层 `QList<RobotChatTurn>` 与 DB 同步；请求体 = 固定 `system` + **尾部十轮**历史（**§10.2**）。API 配置仍 `QSettings`。落库见 **`AiAssistantDao`**（阶段 J 已落实主体）。 |
| D3 | 发送中状态 | 发送后禁用发送或显示「正在回复…」；流式**正常结束**或**失败**后恢复。**首版不做「停止生成」**；失败时见阶段 E4。 |

### 阶段 E：HTTP 客户端与流式解析

| 步骤 | 内容 | 说明 |
|------|------|------|
| E1 | 构建 POST | URL = `baseUrl` + `/chat/completions`（若厂商路径不同则配置可覆盖完整 path 或文档约定）；Header：`Content-Type: application/json`、`Authorization: Bearer <key>`；Body：`model`、`messages`、`stream: true`。 |
| E2 | 流式读取 | 连接 `QNetworkReply::readyRead`，累积字节缓冲；按行切分 SSE（`\n`），处理 `data: ` 前缀行；遇到 `data: [DONE]` 结束。**忽略**空行与 DeepSeek 文档所述 **`: keep-alive`**（见 §5.4）。 |
| E3 | 解析增量 JSON | 对每行 `data: {...}` 使用 `QJsonDocument` 解析；读取 `choices[0].delta.content`（可能为空）拼接到当前助手气泡文本；**单条回复一个 `QString` 累积**，通过信号发到 UI 线程更新控件。 |
| E4 | 错误处理 | `errorOccurred`、HTTP 非 2xx 时读取 `errorString`/body；展示友好中文说明。**流式中途失败**：**不得清空**已写入助手的文本，仅在末尾追加短提示（如「（后续内容未能生成）」）或状态栏说明，与 §5.3 / §7.0 一致。 |
| E5 | 线程安全 | 网络回调可能在非主线程时，**所有 QWidget 更新**经 `Qt::QueuedConnection` 或 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 派发到主线程。 |

### 阶段 F：系统提示词与产品知识

| 步骤 | 内容 | 说明 |
|------|------|------|
| F1 | 编写 `system` 文案 | 角色边界 + 回答风格；常量或 `QString` 资源。 |
| F2 | 嵌入功能摘要 | **首版**：直接使用 **§8.1** 短文或略作替换即可。后续再从《软件功能清单》扩展为更长摘要，放入 `:/` 资源 txt 或 C++ 内嵌字符串。 |
| F3 | 版本注释 | 摘要顶部注释版本日期，便于与发版同步更新。 |

### 阶段 G：安全与仓库卫生

| 步骤 | 内容 | 说明 |
|------|------|------|
| G1 | 全局检索 | 确认无 `sk-` 测试 Key、无完整 Authorization 写入仓库。 |
| G2 | `.gitignore` | 若将来有独立 ini/json 配置文件落盘，路径加入 ignore。 |
| G3 | 日志审计 | 全局确认 `qInfo`/`Logger` 不在 AI 请求路径打印 Key 与完整请求体（可打「已发起请求、长度」级元数据）。 |

### 阶段 H：自测清单（第一版验收）

- [ ] 未配置 Key 时点击发送：有明确提示，不崩溃。  
- [ ] 配置错误 Key：错误信息可读，无 Key 泄露到界面文本。  
- [ ] 配置正确：流式回复逐段出现，结束后可继续下一轮对话。  
- [ ] 「清空会话」后上下文不再带上上一轮。  
- [ ] 「测试连接」成功 / 失败路径均验证。  
- [ ] 切换侧栏到其它平台再切回「机器人管理」：API 设置仍正确；**助手对话仍从库恢复**（DeepSeek 槽位）。  
- [ ] 切换模型下拉：各 `model_key` 会话线独立；占位项不可发送。  
- [ ] **豆包**：选图（可仅图或图+文）发送成功，接口返回流式正文；历史可恢复为 **图片气泡 + 文字气泡**（本地路径仍存在时）。  
- [ ] 重启应用：DeepSeek 线历史保留；清空后子表无行、session 行仍在。  
- [ ] 断开网络或超时：有提示；**已流式输出部分仍保留在助手气泡内**。  
- [ ] 主窗口主题切换：助手页（含 Tab 内对话区）与**聚合会话**观感协调、无未样式化的刺眼控件。  
- [ ] Tab「对话」与「设置」切换正常，无焦点错乱。

### 阶段 I：文档与后续衔接（可选，发版前）

- 在《软件功能清单》对应条目标注「已实现」并附入口截图。  
- 若增加自动化测试：可对 SSE 解析函数做**纯字符串输入**的单元测试（不发起真实网络）。

### 阶段 J：对话持久化与多模型（与 §10 对齐）

| 步骤 | 内容 | 说明 |
|------|------|------|
| J1 | 库表 | **已完成**：**§10.3** 表结构；连接默认 **`PRAGMA foreign_keys=ON`**。 |
| J2 | 请求组装 | **已完成**：UI 全量来自库；API 仅 **尾部 10 轮** + `system`。 |
| J3 | 标题栏下拉 | **已完成**：对话 + 设置同步下拉、会话线切换、DeepSeek / 豆包 / 千问占位；气泡随模型变化（**§10.8**）。 |
| J4 | 清空 | **已完成**：仅当前 `session_id` 子表物理删除；清空时清除待发送图。 |
| J5 | 预设与 Key | **部分完成**：**按预设分组** `QSettings`、豆包默认 URL；**待**千问接通、厂商分栏 Key、自定义项（**§13.4**）。 |

---

## 13. 当前开发进度（实现侧）

> 本节描述**代码仓库中已落地**的能力；与 §12 阶段表对照时，未单独打勾的项仍建议按 §H 自测清单在发版前走一遍。

### 13.1 已完成

| 类别 | 内容 |
|------|------|
| 工程 | 根目录 `CMakeLists.txt`：**Qt6::Network**；源码含 **`openaicompatclient`**、**`robotassistantwidget`**、**`aiassistantdao`**。 |
| 网络层 | **OpenAiCompatClient**：OpenAI 兼容 `POST …/chat/completions`，Bearer；**流式** SSE（忽略 `: keep-alive`）；`buildCompletionsUrl`；测试连接**独立 client**。 |
| 主窗口 | **`openRobotAssistantPage()`** 懒加载；**`applyMainWindowTheme`** 同步 **`m_robotAssistantWidget->applyTheme`**。 |
| 助手 UI | **Tab**「对话」「设置」；对话区 **`aggregateCenterPanel` → `chatArea`**；标题行 **「内置 AI 助手」+ `QComboBox#robotAssistantModelCombo`**；**`messageScroll`、输入区**；气泡样式 **`bubbleIn` / `bubbleOut`** 等。 |
| 模型下拉 | **DeepSeek**；**通义千问** 占位；**豆包（火山方舟）** 可用；对话与设置 **下拉同步**；**按预设** `QSettings`。 |
| 数据与 DAO | **`database.cpp`**：`ai_assistant_sessions` / `ai_assistant_messages` 迁移、**`PRAGMA foreign_keys=ON`**；**`AiAssistantDao`**：`ensureSession`、`listMessages`、`appendMessage`、`clearMessages`；**`modelKeyFromBaseUrlAndModel`**（保留供兼容/后续，当前会话键以下拉 **UserRole** 为准）。 |
| 对话逻辑 | **SQLite 持久化**（按登录用户 **`user_id`** + **`model_key`**）；**`rebindSessionFromCurrentConfig()`**（切 Tab / 保存设置 / 切换下拉 / 发送前）；**尾部 10 轮**参与 API；流式与失败半段保留；**无停止生成**。 |
| 配置 | **`QSettings`** 分组 **`ai/presets/...`**（按预设存 URL/model/apiKey）；旧全局键迁移至 DeepSeek 预设；请求读**当前预设**表单。 |
| 多模态 | 豆包：**选图工具栏**、待选区预览、**Base64 data URL** 请求、库内 **JSON** 存路径+文字、**双用户气泡**；见 **§5.6**。 |
| 资源 | **`deepseek_icon.png`、`doubao_icon.png`、`picture_icon.svg`、`file_icon.svg`** 等入 qrc。 |
| 主题与样式 | **`robotAssistantExtraStyle`**（含标题行 **QComboBox** 三主题）；**`robotAssistantQss`**。 |
| 按钮 | 「发送」「清空会话」→ **`sendButton`**；占位项下发送键禁用（**`applySendButtonPolicy`**）。 |
| 个人信息 | 己方气泡 **`UserDao`**；**`refreshLocalUserProfile()`**；**`EditProfileDialog`** 保存后刷新。 |
| 弹窗 | **`QMessageBox`** 浅色对比样式。 |
| 工程修复 | **`btnRow`** 重名拆分；**`SP_MessageBoxInformation`** 替代不可用图标。 |

### 13.2 待办 / 可选增强（与 §11、§13.4 对照）

- 《**软件功能清单**》等对外文档**回填已实现**及截图（§12 阶段 I）。
- §12 **阶段 G**、**阶段 H**：发版前检索 Key / 日志；真实 DeepSeek 环境跑自测（含**持久化与下拉**相关条）。
- **system 摘要**随版本维护（§8.1）；后续 **RAG** 等。
- **停止生成**、聚合会话「草稿建议」、帮助中心复用客户端：仍属 **§11** 中长期项。
- **多模型接通与配置产品化**：DeepSeek / 豆包与按预设配置**已落地**；千问、厂商分栏 Key 等仍见 **§13.4**。

### 13.3 主要源文件索引

| 路径 | 说明 |
|------|------|
| `src/services/ai/openaicompatclient.{h,cpp}` | HTTP + SSE |
| `src/ui/robotassistantwidget.{h,cpp}` | 助手 UI、会话重绑、持久化与下拉 |
| `src/data/aiassistantdao.{h,cpp}` | 助手会话与消息 CRUD、`model_key` 工具 |
| `src/data/database.cpp` | 迁移、`PRAGMA foreign_keys` |
| `src/ui/mainwindow.{h,cpp}` | 入口、主题、个人信息刷新 |
| `src/utils/applystyle.{h,cpp}` | 助手补充 QSS（含模型下拉） |
| `resources/app_resources.qrc`（含 `deepseek_icon.png`、`doubao_icon.png`、`picture_icon.svg`、`file_icon.svg` 等） | 资源 |

### 13.4 接下来实现清单（建议优先级）

以下为**建议顺序**，便于拆 issue；细节以 **§10.5、§10.8** 为准。

| 优先级 | 项 | 说明 |
|--------|----|------|
| P1 | **通义千问 OpenAI 兼容接入** | 据官方文档填写默认 `base_url`、默认 `model`；下拉项改为 `available=true`，`model_key` 改为稳定值（如 `qwen:…`）；选中时可选择是否**写回设置框**默认值。 |
| P1 | **豆包（火山方舟）** | **已落地**：`model_key` 为 **`doubao:ark`**，`§5.5` 配置与 **`messages[].content` 多模态**（`image_url` + `text`）、**§5.6** 助手 UI 与 JSON 落库；可选后续：**Files API / 公网 URL** 传图、**Responses API**。 |
| P2 | **设置页 Key / URL 分栏或按厂商隔离** | 与 **§10.5** 一致；当前为 **按预设分组**；若需 **按厂商** 再拆 `QSettings` 键并迁移。 |
| P2 | **助手气泡侧栏「模型名 / 图标」随预设变化** | **已落地**（随 `model_key` 切换 DeepSeek / doubao / 千问占位）。 |
| P3 | **自定义 OpenAI 兼容端点** | 下拉增加「自定义」或设置内绑定 `custom:<别名>` 与独立会话线。 |
| P3 | **`QSettings` 记住上次所选模型下拉索引** | 重启恢复 UI；与 `model_key` 一致即可。 |
| P3 | **`ai_assistant_sessions.updated_at`** | 在 `appendMessage` 或事务末尾更新；可选。 |
| P3 | **（可选）旧 `compat:…` 数据迁移** | 若需把历史并入 `deepseek:deepseek-chat`，单独写迁移脚本或一次性 SQL；默认**可不做**。 |
| P4 | **停止生成、单条重试、SSE 单测** | 见 **§11**；与模型扩展解耦。 |

**说明**：P1 依赖各厂商 **API 文档定稿**（Base URL、路径是否标准 `/v1/chat/completions`、模型 id、鉴权头）；若某家非 OpenAI 兼容，需在客户端加 **适配层** 再挂到同一下拉。

---

## 14. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-04-10 | 对齐已实现：**§5.6** 豆包多模态助手（工具栏、data URL、双气泡、落库 JSON）；**按预设 QSettings**、对话/设置下拉同步、**`doubao:ark`**；助手 **昵称/头像** 与 **`systemPromptForRequest` 身份段**；更新 **§2、§3、§7、§8、§10、§11、§13.1/§13.3/§13.4**。 |
| 2026-04-07 | 初稿；增补 **§12**「可实施具体步骤」（分阶段任务、CMake、主窗口接入、UI、流式、安全与验收）。 |
| 2026-04-07 | 第 5 节增补 §5.4：DeepSeek 官方 base_url、路径、模型、错误码、限速/SSE keep-alive 及文档链接。 |
| 2026-04-07 | 产品定案：Tab 布局、对话不持久化、无停止生成、失败保留半段、主题对齐聚合界面；§8.1 简明 system/摘要示例；§7.0 / §12 同步修订。 |
| 2026-04-07 | 文档更名并与实现对齐：标题改为「设计与实现」；新增 **§13 当前开发进度**、原修订记录顺延为 **§14**。 |
| 2026-04-07 | 新增 **§10**「对话持久化与多模型」定案；原 §10–§13 顺延为 **§11–§14**；增补 **§12 阶段 J**；§2、§3、§7 与 **§13.2** 交叉引用 **§10**。 |
| 2026-04-07 | **§10.3** 增补表字段草案：`user_id` + `model_key` 唯一会话线；`ai_assistant_messages(role, content, …)`；清空为物理删子表、保留会话行、**不可恢复**；**§10.6**、**阶段 J1** 同步。 |
| 2026-04-07 | 对齐代码：**§2、§3、§7、§10** 改为「持久化 + 下拉占位已落地」；新增 **§10.8**；**§11** 首条改写；**阶段 J、H** 与 **§13.1–§13.3** 重写；新增 **§13.4 接下来实现清单**（千问/豆包 P1，Key 分栏与自定义等）。 |
| 2026-04-08 | **§11** 第 4 条补充指向独立文档 **`AI辅助回复方案设计与实现.md`**（聚合 AI 辅助回复定案）。 |
| 2026-04-08 | 新增 **§5.5 豆包（火山方舟）接入要点**（Base URL、Bearer、`model`、纯文本 vs 多模态、`content` 数组、传图方式、多轮、与 `OpenAiCompatClient`/落库差异）；**§2.1、§10.4、§13.4 P1** 交叉引用。 |
| 2026-04-13 | **§11** 第 4 条：补充与 **`千牛RPA优化方案设计与实现.md`**、聚合多模态衔接说明（复用豆包请求体）。 |
