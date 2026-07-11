from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service.knowledge_store import KnowledgeStore


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Search local image assets in the lightweight knowledge store.")
    parser.add_argument("query", help="Customer question or image request, e.g. 发空格键实拍图")
    parser.add_argument("--base-name", default="键盘键帽客服知识库PoC")
    parser.add_argument("--shop-id", default="keyboard-demo-shop")
    parser.add_argument("--scene", default="reply_draft")
    parser.add_argument("--top-k", type=int, default=5)
    parser.add_argument("--include-risky", action="store_true")
    parser.add_argument("--mode", default="hybrid", choices=["keyword", "vector", "hybrid"])
    return parser


def main() -> int:
    os.environ.setdefault("AI_CUSTOMER_SERVICE_EMBEDDING_MODEL", "local-hash-ngram-v1")
    args = build_parser().parse_args()
    store = KnowledgeStore()
    base = store.get_or_create_base(
        args.base_name,
        description="本地小型知识库 PoC 测试数据集",
        shop_id=args.shop_id,
        scene=args.scene,
    )
    result = store.search_image_assets(
        args.query,
        base_ids=[str(base["id"])],
        shop_id=args.shop_id,
        scene=args.scene,
        top_k=args.top_k,
        mode=args.mode,
        include_risky=args.include_risky,
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
