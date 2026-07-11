# AI 草稿生成接入知识库检索方案

日期：2026-06-22

## 1. 背景

当前聚合会话里的“生成 AI 草稿”主链路已经在 C++ 客户端内形成闭环：

```text
AggregateChatForm::onGenerateAiDraftClicked()
  -> AiChatAppService::buildAggregateReplyRequest()
  -> 检查 API Key / Base URL / model
  -> 从 MessageDao 取最近入站快照和最近聊天记录
  -> 组装 system prompt / history / 最新客户消息 / 可选图片
  -> createSession()
  -> start()
  -> 流式回写输入框
```

这条链路目前缺少一层业务知识增强。对于商品规则、售后政策、发货时效、价格口径、活动说明等问题，仅依赖大模型和最近聊天上下文，容易出现不够准确或编造承诺的问题。

本方案讨论在生成 AI 草稿前接入外部知识库检索服务：先根据客户最新问题和会话上下文检索相关原文片段，再把片段注入提示词，最后交给现有大模型链路生成回复。

## 2. 设计判断

建议保留当前 C++ 大模型生成主流程，不在 Python 侧新增另一套 AI 生成回复。

原因：

- C++ 侧已经承担 UI 触发、模型配置、API Key 校验、流式会话、增量写入输入框、失败提示和请求审计。
- 如果 Python 侧也生成最终回复，会出现两套模型配置、两套流式解析、两套 prompt 逻辑和两套错误处理。
- 当前 Python sidecar 的主职责更偏向 RPA、平台事件、事实库和命令桥，不适合在没有明确迁移目标前承载完整 AI 编排层。
- 知识库不是本项目实现，只需要按稳定接口调用检索服务，因此更适合被建模为“生成前上下文增强步骤”。

因此推荐架构是：

```text
C++ 聚合会话
  -> 取当前会话上下文
  -> 调统一知识库检索接口
  -> 拿到 topK 原文片段 / 来源 / 分数 / 意图 / 风险标记
  -> 注入 prompt
  -> 复用当前 createSession() / start() 流式生成
```

Python 侧可以保留为 RPA/sidecar 服务。只有在知识库接口需要内网代理、鉴权隔离、Python SDK 或统一后端编排时，才考虑让 Python 暴露一个轻量 `/api/knowledge/search` 代理接口；即便如此，也不建议 Python 同时负责最终 AI 生成。

## 3. 知识库服务现状理解

对方知识库目前有三条管线：

1. 普通 RAG：使用重排模型或 BGE 模型。
2. grep + glob：基于精确关键词和文件匹配的离线查询。
3. 意图识别 + RAG：先识别问题意图，再选择或增强 RAG 检索。

本项目不应关心对方内部使用什么语言，也不应直接感知三条管线的实现细节。客户端应该只对接一个稳定的检索 API，由知识库服务内部决定走 RAG、精确查询、意图识别，或融合排序。

三条管线的建议职责：

- 普通 RAG 作为默认主路径，处理自然语言问题和模糊表达。
- grep + glob 作为确定性补充，适合 SKU、型号、活动名、规则编号、文件名等精确查询。
- 意图识别 + RAG 适合后续自动回复风控，例如退款、投诉、发票、隐私、价格争议等意图可以决定是否允许自动发送或是否强制人工确认。

## 4. 推荐接口契约

建议对方提供统一接口：

```http
POST /knowledge/search
```

请求示例：

```json
{
  "query": "客户最新问题",
  "conversation_history": [
    {"role": "customer", "content": "你好，这个可以退吗"},
    {"role": "agent", "content": "请问您收到货了吗"}
  ],
  "platform": "qianniu",
  "shop_id": "shop-001",
  "scene": "reply_draft",
  "top_k": 5,
  "mode": "auto"
}
```

返回示例：

```json
{
  "status": "success",
  "intent": "refund_policy",
  "risk_flags": ["refund"],
  "results": [
    {
      "source_id": "doc-123",
      "source_title": "售后规则",
      "snippet": "7 天内未使用且不影响二次销售，可申请退货；特殊定制商品除外。",
      "score": 0.87,
      "match_type": "rag"
    }
  ]
}
```

字段建议：

- `query`：客户最新入站消息，优先使用文本；如果只有图片/OCR，则使用 OCR 文本或图片上下文描述。
- `conversation_history`：最近 N 条会话，区分 customer / agent。
- `platform`：`wechat` / `qianniu` 等平台名。
- `shop_id`：店铺或账号维度，后续用于隔离知识库。
- `scene`：调用场景，当前为 `reply_draft`。
- `top_k`：返回片段数量，建议 3 到 5。
- `mode`：建议默认 `auto`，由知识库服务内部选择管线。
- `intent`：可选，但建议返回，便于后续风控和审计。
- `risk_flags`：可选，标记退款、投诉、发票、隐私、金额等风险。
- `results[].snippet`：必须是可直接引用的原文片段，不要只返回摘要。
- `results[].source_title/source_id`：用于审计和调试。
- `results[].score`：用于过滤低置信结果。
- `results[].match_type`：`rag` / `grep` / `intent_rag` / `hybrid` 等。

## 5. 后台知识库管理与服务联动

AI 草稿生成要想稳定使用知识库，后台管理界面必须提供让用户维护知识的入口。客户端界面的职责不是实现知识库本身，而是作为知识库服务的管理面：用户通过客户端上传、替换、删除、启停、分类和绑定知识库，客户端再调用外部知识库服务完成文件存储、解析、切片、索引和检索。

推荐边界：

```text
后台管理界面
  -> 上传 / 替换 / 删除 / 启停 / 分类 / 绑定店铺或机器人
  -> 调知识库服务管理 API
  -> 展示文档状态、索引状态、失败原因

AI 草稿生成
  -> 根据当前平台、店铺、机器人、知识库范围
  -> 调知识库 search API
  -> 拿原文片段注入 prompt
```

客户端不要自己维护向量库、重排模型、grep 索引或文档切片。否则会和知识库服务形成两套事实源，也会导致后续删除、替换、重建索引和检索结果不一致。

### 5.1 管理面 API

除 `/knowledge/search` 外，知识库服务还需要提供管理面接口。建议先按“知识库集合”和“文档”两层建模。

知识库集合：

```http
POST   /knowledge/bases
GET    /knowledge/bases
GET    /knowledge/bases/{base_id}
PATCH  /knowledge/bases/{base_id}
DELETE /knowledge/bases/{base_id}
```

文档管理：

```http
POST   /knowledge/documents
GET    /knowledge/documents?base_id=...
GET    /knowledge/documents/{doc_id}
PATCH  /knowledge/documents/{doc_id}
DELETE /knowledge/documents/{doc_id}
POST   /knowledge/documents/{doc_id}/replace
POST   /knowledge/documents/{doc_id}/reindex
```

文档上传后，知识库服务应异步完成解析、切片、embedding、重排索引、grep/glob 索引等处理。客户端不能假设“上传成功”等于“可检索”，必须展示索引状态。

建议文档状态：

```text
uploaded
parsing
indexing
ready
failed
disabled
deleted
```

其中：

- `uploaded`：文件已接收，但尚未开始处理。
- `parsing`：正在解析文档正文、表格、图片 OCR 等。
- `indexing`：正在切片、向量化、重排索引或精确检索索引构建。
- `ready`：可被 `/knowledge/search` 命中。
- `failed`：解析或索引失败，需要展示失败原因。
- `disabled`：用户主动停用，不参与检索。
- `deleted`：软删除状态，不参与检索，物理清理由服务端决定。

### 5.2 后台 UI 首版范围

后台管理界面首版建议做成“可用的知识库控制台”，而不是复杂文档系统。

建议首版能力：

- 知识库列表：名称、说明、启用状态、绑定平台/店铺/机器人、文档数量、最近更新时间。
- 文档列表：文件名、类型、大小、上传时间、状态、失败原因、当前版本。
- 文档导入：支持 PDF、Word、Excel、Markdown、TXT，后续再扩展图片 OCR、网页抓取等。
- 文档替换：上传新版本，旧版本在新版本 `ready` 前继续作为 active 版本参与检索。
- 文档删除：优先软删除，让检索立即不再命中，物理删除由知识库服务异步处理。
- 启用/停用：比删除更常用，便于临时下线错误知识。
- 重建索引：解析失败、切片策略变更或检索效果异常时可手动触发。
- 绑定范围：控制知识库适用于哪个平台、店铺、机器人或客服场景。

文档“修改”不建议首版做在线编辑。更稳妥的路径是：

```text
查看/下载原文件
  -> 用户本地修改
  -> 上传替换
  -> 新版本解析和索引
  -> ready 后切换为 active
```

这样可以避免客户端演变成文档编辑器，也能避免“替换后索引失败导致原知识不可用”的问题。

### 5.3 检索范围绑定

后台管理的知识必须能影响 AI 草稿生成时的检索范围。建议 `/knowledge/search` 请求增加范围字段，例如：

```json
{
  "query": "客户最新问题",
  "platform": "qianniu",
  "shop_id": "shop-001",
  "robot_id": "robot-after-sale",
  "scene": "reply_draft",
  "base_ids": ["kb-after-sale", "kb-product"],
  "top_k": 5,
  "mode": "auto"
}
```

其中：

- `base_ids`：由客户端根据当前店铺、平台、机器人配置计算出来。
- `shop_id`：用于多店铺隔离。
- `robot_id`：用于后续机器人配置差异化。
- `scene`：用于区分 AI 草稿、自动回复、客户画像、质检等不同场景。

如果后台允许用户把某个知识库绑定到多个店铺或机器人，客户端生成草稿时只需要传当前上下文，知识库服务也可以在服务端二次校验可用范围，避免客户端错误传入越权知识库。

### 5.4 客户端与知识库服务职责

建议职责划分如下：

| 模块 | 职责 |
|---|---|
| 客户端后台管理界面 | 上传、替换、删除、启停、绑定、状态展示、失败原因展示 |
| 客户端 AI 草稿生成链路 | 根据当前会话上下文选择 `base_ids/shop_id/scene`，调用检索，注入 prompt |
| 知识库服务 | 文件存储、解析、OCR、切片、embedding、重排、grep/glob 索引、意图识别、检索融合、版本管理 |
| 客户端本地数据库 | 可缓存知识库和文档列表用于快速展示，但不能作为事实源 |

关键约定是：知识库服务必须保证文档进入 `ready` 后，`/knowledge/search` 可以在对应 `base_id/shop_id/scene` 范围内检索到它；文档被停用、删除或替换后，检索结果也必须按状态实时或准实时生效。

## 6. C++ 侧接入点

当前 `AiChatAppService::buildAggregateReplyRequest()` 是同步构造请求。如果知识库检索走 HTTP，就不适合在这个函数内阻塞等待。

推荐把“生成 AI 草稿”拆成两段：

```text
onGenerateAiDraftClicked()
  -> buildAggregateKnowledgeSearchRequest(conversationId)
  -> KnowledgeRetrievalClient::search()
  -> onKnowledgeSearchFinished()
      -> buildAggregateReplyRequest(conversationId, modelKey, knowledgeContext)
      -> createSession()
      -> start()
```

建议新增能力：

- `KnowledgeRetrievalClient`：负责 HTTP 调用知识库服务、超时、解析和错误归一。
- `KnowledgeSearchRequest`：承载 query、history、platform、shop/account、scene、topK。
- `KnowledgeSearchResult`：承载 intent、riskFlags、snippets。
- `AiChatAppService::buildAggregateReplyRequest(..., KnowledgeContext)`：在原有 prompt 上追加知识库上下文。

超时建议控制在 1.5 到 3 秒。知识库失败、超时或返回空结果时，不应阻断 AI 草稿生成，应降级为当前无知识库的生成流程，并在审计或状态提示中记录 `knowledge_unavailable`。

## 7. Prompt 注入方式

知识库片段应作为独立上下文拼入 system prompt 或最新 user turn 中。建议使用明确边界，避免模型把检索元数据误当作回复正文。

示例：

```text
【知识库检索结果】
1. 来源：售后规则，相关度：0.87，匹配方式：rag
原文：7 天内未使用且不影响二次销售，可申请退货；特殊定制商品除外。

2. 来源：发货时效说明，相关度：0.81，匹配方式：grep
原文：现货商品通常 48 小时内发出，预售商品以页面标注时间为准。

要求：
- 涉及商品、售后、价格、时效、活动、发票等事实性问题时，优先依据知识库原文。
- 知识库没有覆盖的信息，不要编造承诺。
- 如果信息不足，回复中说明需要进一步核实或引导客户补充信息。
- 回复给客户时不要暴露 source_id、score、match_type 等内部字段。
```

建议只注入 top 3 到 top 5，且限制总字符数，避免 prompt 过长。低于置信阈值的片段可以不注入，或作为弱参考注入并提醒模型不得据此做确定承诺。

## 8. 失败降级与风控

知识库检索不应成为生成草稿的单点故障。

建议策略：

- 检索成功且有高置信结果：注入知识库片段生成。
- 检索成功但无结果：继续生成，但 prompt 中不加入知识库。
- 检索超时：继续生成，并记录 `knowledge_timeout`。
- 检索失败：继续生成，并记录 `knowledge_error`。
- 返回高风险意图或风险标记：仍可生成草稿，但后续自动回复应强制人工确认。

当前阶段主要是“AI 草稿”，不是全自动发送，因此风险标记可以先用于 UI 提示和审计。未来如果开启自动回复，风险标记必须进入自动回复决策。

## 9. 与 Python 侧的边界

默认推荐 C++ 直接调用知识库检索服务，原因是生成草稿的入口、状态和流式输出都在 C++。

可以考虑 Python 代理的场景：

- 知识库服务只提供 Python SDK。
- 知识库接口需要同机内网凭证，不希望暴露给 C++ 客户端。
- 未来希望把 AI 编排整体迁到服务端。
- 需要复用 Python 服务端已有的事实库、平台事件或账号上下文。

如果走 Python 代理，也建议只代理检索：

```text
C++ -> Python /api/knowledge/search -> 知识库服务
C++ -> 当前 C++ AI session -> 大模型生成
```

不建议变成：

```text
C++ -> Python /api/ai/generate_reply -> Python 调知识库 + Python 调大模型
```

除非明确决定重构为 Python AI 编排层，否则这会让当前 C++ AI 体系和 Python AI 体系长期并存。

## 10. 推荐落地顺序

1. 定义知识库统一检索 API 和管理面 API 契约，先固定请求、响应、状态字段。
2. 后台管理界面占位升级为知识库控制台，支持知识库列表、文档列表、上传、替换、删除、启停和状态展示。
3. 在 C++ 增加 `KnowledgeRetrievalClient`，只负责调用和解析检索接口。
4. 在 `onGenerateAiDraftClicked()` 后加入异步检索状态。
5. 扩展 `AiChatAppService::buildAggregateReplyRequest()`，支持传入知识库片段。
6. 将知识库片段注入 prompt，并保留无知识库降级路径。
7. 增加请求审计字段：是否检索、耗时、命中数量、intent、risk_flags、失败原因、命中的 `base_ids/doc_ids`。
8. 后续再把 `intent/risk_flags` 接入自动回复风控。

## 11. 当前结论

本项目侧应该把知识库视为外部检索能力，而不是把三条管线的选择逻辑搬进客户端。

短期最优方案：

- 保留 C++ 现有 AI 草稿生成链路。
- 在生成前增加知识库检索步骤。
- 对接一个统一 `mode=auto` 的知识库搜索接口。
- 后台管理界面通过知识库管理 API 维护知识库集合和文档，客户端不实现索引和检索内核。
- 成功时注入原文片段，失败时降级为当前流程。
- Python 侧暂不新增最终 AI 回复生成能力。

这样能最大程度复用当前已跑通的大模型流式体验，同时把业务知识准确性补上，改动范围也更可控。
