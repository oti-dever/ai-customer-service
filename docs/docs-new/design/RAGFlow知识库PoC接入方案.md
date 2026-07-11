# RAGFlow 知识库 PoC 接入方案

日期：2026-06-29

## 1. 背景

当前聚合会话的 AI 回复链路主要依赖聊天上下文和系统提示词。现有 `AiChatAppService::buildAggregateReplyRequest()` 已经能完成模型配置校验、历史消息组装、系统提示词拼接、图片输入和流式生成，但系统提示词中的“店铺知识”仍是 MVP 占位文本。

这会带来一个核心问题：当客户询问商品规格、发货时效、售后政策、活动规则、发票、价格口径等事实型问题时，大模型只能根据通用知识和最近对话推断，容易生成不准确或无法兑现的内容。

本 PoC 决定优先引入 RAGFlow 作为独立知识库服务，先验证以下闭环：

```text
导入文档
  -> RAGFlow 解析、切片、索引
  -> AI 生成前检索相关原文片段
  -> 原文片段注入 prompt
  -> 复用当前大模型流式生成链路
  -> 客服人工确认后发送
```

本方案只讨论 PoC 设计，不进入代码实现。

## 2. PoC 目标

### 2.1 要验证的问题

1. RAGFlow 是否能稳定管理店铺知识文档，包括上传、删除、启停、解析状态查看。
2. RAGFlow 对常见客服文档的解析和检索效果是否足够可用。
3. 当前 AI 草稿生成链路在注入检索片段后，回复准确性是否明显提升。
4. 知识库失败、超时、无结果时，现有 AI 草稿生成是否能平稳降级。
5. 知识库和会话上下文拼接后的 prompt 长度、延迟和生成质量是否可控。

### 2.2 首版范围

PoC 首版只覆盖“AI 草稿生成前检索知识库”，不做全自动回复。

纳入范围：

- 部署一套 RAGFlow 服务。
- 建立 1 到 2 个测试知识库。
- 导入售后规则、发货说明、商品 FAQ、活动说明等测试文档。
- 使用 RAGFlow API 检索 topK 原文片段。
- 设计本项目内部的知识库检索适配层接口。
- 设计 prompt 注入格式、降级策略和验收标准。

暂不纳入范围：

- 不自研向量库、切片器、embedding 管线。
- 不在客户端本地维护第二套知识库索引。
- 不做复杂在线文档编辑器。
- 不做无人值守自动发送。
- 不做完整多租户 SaaS 权限体系。
- 不把最终 AI 生成迁移到 RAGFlow。

## 3. 选型判断

RAGFlow 更适合作为本项目知识库 PoC 的原因：

- 它本身是面向 RAG 的开源知识库服务，不只是向量库。
- 支持数据集、文档、chunk、解析状态和检索 API。
- 文档解析能力覆盖 PDF、Word、Excel、PPT、TXT、网页等常见资料形态。
- API 支持元数据过滤，便于后续按平台、店铺、机器人、场景隔离知识范围。
- 可自托管，PoC 阶段不需要购买商业知识库服务。

需要注意：

- RAGFlow 是独立服务，部署复杂度高于简单 SDK。
- 检索质量依赖文档质量、切片策略、embedding 模型和 rerank 配置。
- RAGFlow 的 API 是外部系统契约，本项目不应把它的字段直接散落到 UI 和 AI 生成代码里，应通过适配层收敛。

## 4. 总体架构

推荐架构：

```text
后台管理界面
  -> 知识库管理适配层
  -> RAGFlow HTTP API
  -> 数据集 / 文档 / chunk / 元数据

聚合会话 AI 草稿生成
  -> 取最近会话上下文和客户最新入站消息
  -> 知识库检索适配层
  -> RAGFlow /api/v1/retrieval
  -> 标准化 KnowledgeContext
  -> 注入 AiRequest.systemPrompt
  -> 现有 AiServiceFacade / IAiStreamingSession
```

关键边界：

- RAGFlow 负责文档存储、解析、切片、索引、检索。
- 本项目负责知识库配置、会话范围选择、检索结果裁剪、prompt 注入和 AI 回复生成。
- C++ 客户端仍是 AI 草稿生成的主编排方。
- Python sidecar 仅在需要内网代理、鉴权隔离或统一服务端出口时，才作为轻量代理，不负责最终生成。

## 5. RAGFlow 概念映射

| RAGFlow 概念 | 本项目概念 | PoC 用法 |
|---|---|---|
| dataset | 知识库 | 一个店铺、一个机器人或一个业务域可对应一个 dataset |
| document | 知识文档 | 售后规则、商品说明、发货政策、FAQ 文件 |
| chunk | 原文片段 | 检索后注入 prompt 的最小事实依据 |
| meta_fields / metadata | 文档元数据 | 平台、店铺、机器人、分类、版本、适用场景 |
| retrieval | 知识库检索 | 根据客户问题返回相关 chunk |
| run/progress | 文档解析状态 | 控制文档是否可用于 AI 检索 |

PoC 阶段建议先按“业务域”建 dataset，而不是按每个小文件建 dataset。

示例：

```text
dataset: 售后知识库
  document: 退换货规则.pdf
  document: 发票规则.docx
  document: 投诉处理口径.md

dataset: 商品知识库
  document: 商品规格表.xlsx
  document: 常见问题FAQ.md
```

## 6. 知识库管理方案

### 6.1 管理对象

本项目后台管理面建议抽象出两层对象：

```text
KnowledgeBase
  id
  name
  description
  enabled
  ragflow_dataset_id
  platform_scope
  shop_scope
  robot_scope
  scene_scope
  created_at
  updated_at

KnowledgeDocument
  id
  base_id
  ragflow_document_id
  name
  file_type
  file_size
  status
  progress
  enabled
  version
  error_message
  created_at
  updated_at
```

PoC 可以先不落库，只在设计上明确这些字段。后续实现时，如果后台 UI 要展示知识库列表和文档状态，再考虑本地缓存这些映射关系。

### 6.2 文档导入流程

```text
用户选择知识库并上传文件
  -> 调用 RAGFlow POST /api/v1/datasets/{dataset_id}/documents
  -> 获得 ragflow_document_id
  -> 设置文档元数据
  -> 调用 RAGFlow POST /api/v1/datasets/{dataset_id}/chunks 触发解析
  -> 轮询文档列表或详情
  -> run = DONE 后标记为可检索
```

RAGFlow 上传文档后返回的 `run` 初始可能是 `UNSTART`，需要显式触发解析。文档处理状态可按 RAGFlow 的 `run` 字段映射：

| RAGFlow run | 本项目状态 | 含义 |
|---|---|---|
| `UNSTART` | `uploaded` | 已上传，未开始解析 |
| `RUNNING` | `indexing` | 正在解析、切片、embedding、索引 |
| `DONE` | `ready` | 可参与检索 |
| `FAIL` | `failed` | 解析或索引失败 |
| `CANCEL` | `cancelled` | 解析被取消 |

### 6.3 文档删除和停用

PoC 建议区分“删除”和“停用”：

- 停用：优先通过文档可用状态或元数据过滤实现，让文档暂时不参与检索。
- 删除：调用 RAGFlow 删除文档接口，按文档 ID 删除。
- 替换：先上传新文档并解析，待新版本 `DONE` 后再停用或删除旧版本。

不建议首版做在线编辑。更稳定的方式是下载原文件、本地修改、重新上传替换。

### 6.4 元数据设计

建议每个文档至少写入以下元数据，便于检索时过滤：

```json
{
  "platform": "qianniu",
  "shop_id": "shop-001",
  "robot_id": "robot-after-sale",
  "scene": "reply_draft",
  "category": "after_sale",
  "version": "2026-06-29",
  "enabled": "true"
}
```

PoC 阶段可以只使用 `shop_id`、`scene`、`enabled` 三个字段。后续多平台、多店铺、多机器人时再扩展。

## 7. 检索方案

### 7.1 本项目内部接口

不建议业务代码直接调用 RAGFlow `/api/v1/retrieval`。建议先定义一层项目内部语义接口：

```http
POST /knowledge/search
```

请求示例：

```json
{
  "query": "客户说这件衣服收到后尺码不合适，可以退吗？",
  "conversation_history": [
    {"role": "customer", "content": "我昨天刚收到"},
    {"role": "agent", "content": "请问吊牌还在吗？"}
  ],
  "platform": "qianniu",
  "shop_id": "shop-001",
  "robot_id": "robot-after-sale",
  "scene": "reply_draft",
  "base_ids": ["kb-after-sale"],
  "top_k": 5
}
```

返回示例：

```json
{
  "status": "success",
  "results": [
    {
      "source_id": "ragflow-doc-id",
      "chunk_id": "ragflow-chunk-id",
      "source_title": "退换货规则.pdf",
      "snippet": "服装类商品在签收后 7 天内，吊牌完整且不影响二次销售，可申请退换货。",
      "score": 0.86,
      "metadata": {
        "category": "after_sale"
      }
    }
  ],
  "metadata": {
    "provider": "ragflow",
    "latency_ms": 420
  }
}
```

这层接口可以先是 C++ 内部类，也可以后续由 Python 服务或后端服务暴露。核心是把 RAGFlow 返回结构转换成稳定的 `KnowledgeContext`，避免后续替换知识库时影响 AI 生成链路。

### 7.2 RAGFlow 检索调用

RAGFlow 的检索接口为：

```http
POST /api/v1/retrieval
```

PoC 推荐请求参数：

```json
{
  "question": "客户说这件衣服收到后尺码不合适，可以退吗？",
  "dataset_ids": ["ragflow-dataset-id"],
  "page": 1,
  "page_size": 5,
  "similarity_threshold": 0.25,
  "vector_similarity_weight": 0.3,
  "top_k": 128,
  "keyword": true,
  "highlight": false,
  "metadata_condition": {
    "logic": "and",
    "conditions": [
      {"name": "shop_id", "comparison_operator": "is", "value": "shop-001"},
      {"name": "scene", "comparison_operator": "is", "value": "reply_draft"},
      {"name": "enabled", "comparison_operator": "is", "value": "true"}
    ]
  }
}
```

参数建议：

- `page_size` 控制最终返回给本项目的候选片段数量，PoC 用 5。
- `top_k` 是参与召回排序的候选范围，不等同于最终注入 prompt 的片段数。
- `keyword=true` 可增强 SKU、型号、规则编号、活动名称这类精确匹配。
- `metadata_condition` 用来隔离店铺、场景和启停状态。
- `similarity_threshold` 先用较低阈值验证召回，再根据测试集调高。

### 7.3 检索查询构造

检索 `question` 不应只用客户最后一句话。建议组合：

```text
客户最新入站消息
+ 最近 2 到 4 条关键上下文
+ 平台/店铺/场景提示
```

示例：

```text
场景：电商客服售后回复草稿。
客户最新问题：尺码不合适可以退吗？
最近上下文：客户昨天收到衣服，客服询问吊牌是否完整。
```

这样能降低客户追问、代词、省略表达导致的检索偏差。

## 8. Prompt 注入方案

现有 system prompt 中的“店铺知识·MVP 占位”应在接入后替换为真实检索结果。

建议格式：

```text
【知识库检索结果】
以下内容是从店铺知识库检索到的原文片段。涉及商品、售后、物流、价格、活动、发票等事实问题时，必须优先依据这些片段回答。没有覆盖的信息不要编造承诺。

1. 来源：退换货规则.pdf
原文：服装类商品在签收后 7 天内，吊牌完整且不影响二次销售，可申请退换货。

2. 来源：售后处理SOP.md
原文：客户申请退换货前，需先确认商品状态、吊牌、包装和是否影响二次销售。

要求：
- 回复客户时不要暴露 chunk_id、score、dataset_id 等内部字段。
- 如果知识库片段不足以确认结论，请引导客户补充信息或说明需要进一步核实。
- 不要输出“根据知识库”这类生硬表述，回复应像正常客服话术。
```

注入策略：

- 最多注入 top 3 到 top 5 个片段。
- 每个片段先限制 300 到 500 字。
- 总知识库上下文建议控制在 1500 到 2500 字以内。
- 低于置信阈值的片段不注入，或作为弱参考注入并要求模型不得据此做确定承诺。
- 如果多个片段互相冲突，优先选择更新时间更新、权重更高或业务分类更具体的文档。

## 9. 失败降级和风险控制

知识库检索不能成为 AI 草稿生成的单点故障。

建议策略：

| 场景 | 行为 |
|---|---|
| 检索成功且有高置信片段 | 注入知识库片段后生成 |
| 检索成功但无结果 | 不注入知识库，沿用现有生成链路 |
| RAGFlow 超时 | 记录 `knowledge_timeout`，沿用现有生成链路 |
| RAGFlow 返回错误 | 记录 `knowledge_error`，沿用现有生成链路 |
| 文档正在解析 | 不参与检索，后台显示 `indexing` |
| 文档解析失败 | 不参与检索，后台显示失败原因 |
| 检索结果互相冲突 | 降低确定性表达，引导人工核实 |

超时建议：

- PoC 阶段检索超时设为 3 秒。
- 正式体验中可优化到 1.5 到 2 秒。
- 超时不阻断生成，只影响知识增强。

## 10. PoC 部署建议

### 10.1 部署形态

建议先采用独立部署：

```text
开发机或测试服务器
  -> Docker / Linux 环境运行 RAGFlow
  -> RAGFlow 使用本地或内网模型服务
  -> 本项目通过 HTTP API 访问
```

不建议把 RAGFlow 作为本项目进程内依赖，也不建议把它塞进 Windows 客户端安装包。RAGFlow 的依赖、存储和模型服务都更适合作为独立服务维护。

### 10.2 模型和成本

PoC 阶段尽量不付费，可以优先考虑：

- embedding：本地或自托管中文 embedding 模型，例如 BGE 系列。
- rerank：先不开或使用本地 rerank，待检索质量不足时再加。
- chat model：继续复用当前项目已有大模型配置。

关键原则：RAGFlow 只负责“找资料”，最终回复仍由当前 AI 配置生成。这样不会引入第二套回复模型成本和配置复杂度。

## 11. 验收标准

PoC 建议准备 30 到 50 个客服问题作为测试集，覆盖：

- 售后退换货。
- 发货时效。
- 商品规格。
- 活动优惠。
- 发票规则。
- 投诉和高风险问题。
- 知识库没有覆盖的问题。

验收指标：

| 指标 | 目标 |
|---|---|
| 文档导入成功率 | 常见 PDF/Word/Excel/Markdown/TXT 能成功导入和解析 |
| 检索命中率 | 测试问题 top 5 中能命中相关片段的比例达到可接受水平 |
| 回复事实准确率 | 注入知识库后，回复不再编造关键规则 |
| 延迟 | 检索增加的平均耗时可接受，超时可降级 |
| 引用可追溯 | 每条注入片段能追溯到文档和 chunk |
| 降级可用 | RAGFlow 停止或超时时，AI 草稿仍能生成 |

主观验收方式：

```text
同一批问题分别跑：
1. 当前无知识库链路
2. 接入 RAGFlow 检索链路

由业务人员对回复进行准确性、可发送性、风险性评分。
```

## 12. 分阶段推进

### P0：环境和资料准备

- 部署 RAGFlow。
- 创建测试 dataset。
- 导入 5 到 10 份真实或脱敏客服知识文档。
- 确认文档状态能进入 `DONE`。
- 手动调用 `/api/v1/retrieval` 验证能返回 chunk。

### P1：接口和 prompt 设计验证

- 确定内部 `/knowledge/search` 请求和返回结构。
- 确定 dataset、document、chunk 到项目内模型的映射。
- 确定 prompt 注入模板。
- 准备测试问题集。
- 先用手动脚本或 API 调试工具验证效果。

### P2：产品流程设计

- 设计后台知识库列表。
- 设计文档列表和上传入口。
- 设计文档状态、失败原因、启停、删除、重建索引入口。
- 设计 AI 草稿生成时的知识库启用开关和状态提示。

### P3：代码接入

本阶段暂不执行。后续如果进入实现，建议优先接入检索链路，再做管理 UI。

## 13. 主要风险

1. 文档质量差导致检索差。扫描件、图片、复杂表格和过时文档会显著影响结果。
2. 切片策略不合适。太短会丢上下文，太长会带入噪声。
3. 多店铺知识混用。没有元数据过滤时，容易拿错店铺规则。
4. 大模型仍可能不遵循知识库。prompt 必须明确“没有依据不要承诺”。
5. 延迟影响客服体验。检索要有超时和降级。
6. RAGFlow API 版本变化。项目内需要适配层隔离外部字段。

## 14. 参考资料

- RAGFlow HTTP API：<https://ragflow.com.cn/docs/http_api_reference>
- RAGFlow 数据集配置说明：<https://ragflow.com.cn/docs/configure_knowledge_base>
- 相关设计文档：`docs/docs-new/design/AI草稿生成接入知识库检索方案.md`

