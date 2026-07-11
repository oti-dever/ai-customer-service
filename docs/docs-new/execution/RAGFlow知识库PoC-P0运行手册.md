# RAGFlow 知识库 PoC P0 运行手册

日期：2026-06-30

## 1. P0 目标

P0 的目标不是接入当前 AI 回复链路，而是先确认知识库服务本身可用：

```text
部署 RAGFlow
  -> 创建测试 dataset
  -> 导入键盘/键帽测试文档
  -> 触发解析和索引
  -> 确认文档进入 DONE
  -> 用测试问题检索到相关原文片段
```

当前仓库已准备测试资料：

- `docs/knowledge-base/01_键盘商品规格与选购指南.txt`
- `docs/knowledge-base/02_键帽材质工艺与兼容性FAQ.txt`
- `docs/knowledge-base/03_机械键盘售后与保修政策.docx`
- `docs/knowledge-base/04_发货包装与退换货规则.pdf`
- `docs/knowledge-base/RAGFlow导入测试问题.md`

## 2. 当前环境检查

在当前 Windows 开发机执行：

```powershell
docker --version
docker compose version
```

结果：当前机器未识别 `docker` 命令，因此本机暂时不能直接部署 RAGFlow。

P0 后续有两条路：

1. 在本机安装 Docker Desktop 后继续。
2. 在 Linux/内网测试服务器部署 RAGFlow，本机只通过 HTTP API 调用。

推荐第二种。RAGFlow 依赖较多，作为独立服务部署更符合后续架构边界。

## 3. RAGFlow 部署准备

建议按 RAGFlow 官方 Docker Compose 文档部署。部署完成后需要确认：

- Web 控制台可访问。
- 能创建 API Key。
- HTTP API 可访问，默认可按实际部署暴露为 `http://host:9380`。
- embedding 模型配置可用。
- 文档上传后能进入解析流程。

如果只是本地 PoC，建议先使用单实例测试配置，不要一开始处理高可用、备份和多租户。

参考：

- RAGFlow HTTP API：<https://ragflow.com.cn/docs/http_api_reference>
- RAGFlow 知识库配置：<https://ragflow.com.cn/docs/configure_knowledge_base>

## 4. API Key 和环境变量

拿到 RAGFlow API Key 后，在 PowerShell 中设置：

```powershell
$env:RAGFLOW_BASE_URL = "http://127.0.0.1:9380"
$env:RAGFLOW_API_KEY = "替换为RAGFlow控制台生成的APIKey"
$env:RAGFLOW_DATASET_NAME = "键盘键帽客服知识库PoC"
```

如果 RAGFlow 部署在服务器上，将 `RAGFLOW_BASE_URL` 改成服务器地址。

## 5. 导入脚本

仓库已新增辅助脚本：

```text
python/tools/ragflow_p0_import.py
```

脚本职责：

- 创建或复用 dataset。
- 上传 `docs/knowledge-base` 下的四份测试文档。
- 触发文档解析。
- 可选轮询文档状态。
- 使用测试问题调用 `/api/v1/retrieval`，打印 top chunks。

脚本不依赖 `requests`，只使用 Python 标准库。

### 5.1 干跑检查

先不调用 RAGFlow，只确认脚本参数和待上传文件：

```powershell
C:\Users\Wulihong0137\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe python\tools\ragflow_p0_import.py --dry-run
```

### 5.2 首次导入

RAGFlow 可访问后执行：

```powershell
C:\Users\Wulihong0137\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe python\tools\ragflow_p0_import.py --wait
```

预期结果：

- 输出 dataset id。
- 输出上传文档 id。
- 触发解析。
- 轮询文档 `run/progress`。
- 文档最终进入 `DONE`，或失败时显示 `FAIL`。
- 检索问题能返回相关 chunk。

### 5.3 只跑检索验证

如果文档已经导入并解析完成，可以跳过上传和解析：

```powershell
C:\Users\Wulihong0137\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe python\tools\ragflow_p0_import.py --skip-upload --skip-parse
```

## 6. 首轮测试问题

使用以下问题验证检索效果：

1. `K87 Pro 和 K98 Max 有什么区别，办公用哪个更合适？`
2. `静音轴和茶轴哪个声音小，适合办公室吗？`
3. `我买的键帽能不能装到 68 键键盘上？`
4. `PBT 热升华键帽会不会打油？`
5. `键盘收到后有一个轴不触发，可以换吗？`
6. `自己拆轴之后还保修吗？`
7. `收到键盘发现外箱压坏了怎么办？`
8. `当天几点前下单可以当天发货？`
9. `键帽色差能不能退？`
10. `键盘进水了还能保修吗？`

验收重点不是大模型回复，而是 RAGFlow 是否能返回对应的原文 chunk。

## 7. P0 通过标准

P0 可视为通过，需要满足：

- 至少一个 dataset 创建成功。
- 四份测试文档上传成功。
- 文档解析状态进入 `DONE`，或能明确定位失败文档和失败原因。
- 10 个测试问题中，大部分能在 top 5 chunk 命中对应文档片段。
- RAGFlow 停止或 API Key 错误时，脚本能清晰报错。

## 8. P0 未完成项

当前尚未完成：

- 本机未安装 Docker，RAGFlow 服务尚未部署。
- 尚未创建真实 RAGFlow dataset。
- 尚未实际上传测试文档。
- 尚未拿到真实检索结果。

下一步：准备一台可运行 Docker Compose 的机器，部署 RAGFlow，生成 API Key 后执行 `python/tools/ragflow_p0_import.py --wait`。

