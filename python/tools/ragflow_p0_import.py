from __future__ import annotations

import argparse
import json
import mimetypes
import os
import sys
import time
import uuid
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_KNOWLEDGE_DIR = ROOT / "docs" / "knowledge-base"
DEFAULT_DOCS = [
    "01_键盘商品规格与选购指南.txt",
    "02_键帽材质工艺与兼容性FAQ.txt",
    "03_机械键盘售后与保修政策.docx",
    "04_发货包装与退换货规则.pdf",
]
DEFAULT_TEST_QUESTIONS = [
    "K87 Pro 和 K98 Max 有什么区别，办公用哪个更合适？",
    "静音轴和茶轴哪个声音小，适合办公室吗？",
    "我买的键帽能不能装到 68 键键盘上？",
    "PBT 热升华键帽会不会打油？",
    "键盘收到后有一个轴不触发，可以换吗？",
    "自己拆轴之后还保修吗？",
    "收到键盘发现外箱压坏了怎么办？",
    "当天几点前下单可以当天发货？",
    "键帽色差能不能退？",
    "键盘进水了还能保修吗？",
]


class RagflowError(RuntimeError):
    pass


class RagflowClient:
    def __init__(self, base_url: str, api_key: str, timeout: int = 30) -> None:
        self.base_url = base_url.rstrip("/")
        self.api_key = api_key
        self.timeout = timeout

    def request_json(
        self,
        method: str,
        path: str,
        body: dict[str, Any] | None = None,
        query: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        url = f"{self.base_url}{path}"
        if query:
            url = f"{url}?{urlencode({k: v for k, v in query.items() if v is not None})}"

        data = None
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Accept": "application/json",
        }
        if body is not None:
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json"

        return self._send(Request(url, data=data, headers=headers, method=method))

    def upload_documents(self, dataset_id: str, files: list[Path]) -> dict[str, Any]:
        boundary = f"----ragflow-p0-{uuid.uuid4().hex}"
        chunks: list[bytes] = []

        for file_path in files:
            mime_type = mimetypes.guess_type(file_path.name)[0] or "application/octet-stream"
            chunks.append(f"--{boundary}\r\n".encode("utf-8"))
            chunks.append(
                (
                    'Content-Disposition: form-data; name="file"; '
                    f'filename="{file_path.name}"\r\n'
                    f"Content-Type: {mime_type}\r\n\r\n"
                ).encode("utf-8")
            )
            chunks.append(file_path.read_bytes())
            chunks.append(b"\r\n")

        chunks.append(f"--{boundary}--\r\n".encode("utf-8"))
        data = b"".join(chunks)
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Accept": "application/json",
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Content-Length": str(len(data)),
        }
        req = Request(
            f"{self.base_url}/api/v1/datasets/{dataset_id}/documents",
            data=data,
            headers=headers,
            method="POST",
        )
        return self._send(req)

    def _send(self, req: Request) -> dict[str, Any]:
        try:
            with urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read().decode("utf-8")
        except HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RagflowError(f"HTTP {exc.code}: {detail}") from exc
        except URLError as exc:
            raise RagflowError(f"request failed: {exc.reason}") from exc

        try:
            payload = json.loads(raw) if raw else {}
        except json.JSONDecodeError as exc:
            raise RagflowError(f"non-json response: {raw[:500]}") from exc

        code = payload.get("code", 0)
        if code not in (0, "0", None):
            raise RagflowError(f"RAGFlow code={code}: {payload.get('message') or payload}")
        return payload


def env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def list_datasets(client: RagflowClient, name: str) -> list[dict[str, Any]]:
    payload = client.request_json(
        "GET",
        "/api/v1/datasets",
        query={"page": 1, "page_size": 100, "name": name},
    )
    data = payload.get("data") or []
    if isinstance(data, dict):
        data = data.get("datasets") or data.get("items") or []
    return [item for item in data if isinstance(item, dict)]


def ensure_dataset(client: RagflowClient, name: str, dry_run: bool) -> str:
    if dry_run:
        body = {
            "name": name,
            "description": "键盘/键帽客服知识库 PoC 测试数据集",
            "permission": "me",
            "chunk_method": "naive",
        }
        print("dry-run create or reuse dataset:")
        print(json.dumps(body, ensure_ascii=False, indent=2))
        return "<dry-run-dataset-id>"

    existing = [item for item in list_datasets(client, name) if item.get("name") == name]
    if existing:
        dataset_id = str(existing[0]["id"])
        print(f"dataset exists: {name} -> {dataset_id}")
        return dataset_id

    body = {
        "name": name,
        "description": "键盘/键帽客服知识库 PoC 测试数据集",
        "permission": "me",
        "chunk_method": "naive",
    }
    payload = client.request_json("POST", "/api/v1/datasets", body=body)
    data = payload.get("data") or {}
    dataset_id = str(data.get("id") or data.get("dataset_id") or "")
    if not dataset_id:
        raise RagflowError(f"cannot find dataset id in response: {payload}")
    print(f"dataset created: {name} -> {dataset_id}")
    return dataset_id


def upload_documents(client: RagflowClient, dataset_id: str, files: list[Path], dry_run: bool) -> list[str]:
    if dry_run:
        print("dry-run upload files:")
        for file_path in files:
            print(f"  - {file_path}")
        return [f"<dry-run-doc-{index}>" for index, _ in enumerate(files, start=1)]

    payload = client.upload_documents(dataset_id, files)
    data = payload.get("data") or []
    if isinstance(data, dict):
        data = data.get("documents") or data.get("items") or [data]
    doc_ids = [str(item.get("id")) for item in data if isinstance(item, dict) and item.get("id")]
    print(f"uploaded documents: {len(doc_ids)}")
    for item in data if isinstance(data, list) else []:
        if isinstance(item, dict):
            print(f"  - {item.get('name') or item.get('filename')}: {item.get('id')}")
    return doc_ids


def list_documents(client: RagflowClient, dataset_id: str) -> list[dict[str, Any]]:
    payload = client.request_json(
        "GET",
        f"/api/v1/datasets/{dataset_id}/documents",
        query={"page": 1, "page_size": 100},
    )
    data = payload.get("data") or []
    if isinstance(data, dict):
        data = data.get("docs") or data.get("documents") or data.get("items") or []
    return [item for item in data if isinstance(item, dict)]


def parse_documents(client: RagflowClient, dataset_id: str, document_ids: list[str], dry_run: bool) -> None:
    if not document_ids:
        print("no newly uploaded document ids; skip parse trigger")
        return
    body = {"document_ids": document_ids}
    if dry_run:
        print("dry-run parse documents:")
        print(json.dumps(body, ensure_ascii=False, indent=2))
        return
    client.request_json("POST", f"/api/v1/datasets/{dataset_id}/chunks", body=body)
    print(f"parse triggered: {len(document_ids)} documents")


def wait_until_done(client: RagflowClient, dataset_id: str, timeout_seconds: int) -> None:
    deadline = time.time() + timeout_seconds
    while True:
        docs = list_documents(client, dataset_id)
        states = [(doc.get("name") or doc.get("filename"), doc.get("run"), doc.get("progress")) for doc in docs]
        print("document states:")
        for name, run, progress in states:
            print(f"  - {name}: run={run}, progress={progress}")

        unfinished = [state for state in states if state[1] not in ("DONE", "FAIL", "CANCEL")]
        if not unfinished:
            return
        if time.time() >= deadline:
            raise RagflowError(f"timeout waiting documents to finish: {unfinished}")
        time.sleep(10)


def run_retrieval(client: RagflowClient, dataset_id: str, questions: list[str], dry_run: bool) -> None:
    for question in questions:
        body = {
            "question": question,
            "dataset_ids": [dataset_id],
            "page": 1,
            "page_size": 5,
            "similarity_threshold": 0.25,
            "vector_similarity_weight": 0.3,
            "top_k": 128,
            "keyword": True,
            "highlight": False,
        }
        if dry_run:
            print("dry-run retrieval:")
            print(json.dumps(body, ensure_ascii=False, indent=2))
            continue

        payload = client.request_json("POST", "/api/v1/retrieval", body=body)
        chunks = ((payload.get("data") or {}).get("chunks") or []) if isinstance(payload.get("data"), dict) else []
        print(f"\nQ: {question}")
        if not chunks:
            print("  no chunks")
            continue
        for index, chunk in enumerate(chunks[:3], start=1):
            doc_name = chunk.get("document_name") or chunk.get("docnm_kwd") or chunk.get("source_title") or ""
            score = chunk.get("similarity") or chunk.get("score") or chunk.get("vector_similarity") or ""
            content = str(chunk.get("content") or chunk.get("content_with_weight") or "").replace("\n", " ")
            print(f"  {index}. {doc_name} score={score} {content[:180]}")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="RAGFlow P0 import and retrieval smoke test.")
    parser.add_argument("--base-url", default=env("RAGFLOW_BASE_URL", "http://127.0.0.1:9380"))
    parser.add_argument("--api-key", default=env("RAGFLOW_API_KEY"))
    parser.add_argument("--dataset-name", default=env("RAGFLOW_DATASET_NAME", "键盘键帽客服知识库PoC"))
    parser.add_argument("--knowledge-dir", type=Path, default=DEFAULT_KNOWLEDGE_DIR)
    parser.add_argument("--wait", action="store_true", help="Poll document run/progress until DONE/FAIL/CANCEL.")
    parser.add_argument("--wait-timeout", type=int, default=900)
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--skip-parse", action="store_true")
    parser.add_argument("--skip-retrieval", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    if not args.api_key and not args.dry_run:
        print("RAGFLOW_API_KEY is required. Set it in the environment or pass --api-key.", file=sys.stderr)
        return 2

    files = [args.knowledge_dir / name for name in DEFAULT_DOCS]
    missing = [path for path in files if not path.is_file()]
    if missing:
        print(f"missing knowledge files: {missing}", file=sys.stderr)
        return 2

    client = RagflowClient(args.base_url, args.api_key)
    dataset_id = ensure_dataset(client, args.dataset_name, args.dry_run)

    uploaded_doc_ids: list[str] = []
    if not args.skip_upload:
        uploaded_doc_ids = upload_documents(client, dataset_id, files, args.dry_run)
    if not args.skip_parse:
        parse_documents(client, dataset_id, uploaded_doc_ids, args.dry_run)
    if args.wait and not args.dry_run:
        wait_until_done(client, dataset_id, args.wait_timeout)
    if not args.skip_retrieval:
        run_retrieval(client, dataset_id, DEFAULT_TEST_QUESTIONS, args.dry_run)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
