from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service.knowledge_store import KnowledgeStore


DEFAULT_QUESTIONS = [
    "K87 Pro 和 K98 Max 有什么区别，办公用哪个更合适？",
    "PBT 热升华键帽会不会打油？",
    "键盘收到后有一个轴不触发，可以换吗？",
    "当天几点前下单可以当天发货？",
    "键盘进水了还能保修吗？",
]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Import local docs into the lightweight knowledge store.")
    parser.add_argument("--directory", type=Path, default=REPO_ROOT / "docs" / "knowledge-base")
    parser.add_argument("--base-name", default="键盘键帽客服知识库PoC")
    parser.add_argument("--shop-id", default="keyboard-demo-shop")
    parser.add_argument("--scene", default="reply_draft")
    parser.add_argument("--skip-search", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    store = KnowledgeStore()
    base = store.get_or_create_base(
        args.base_name,
        description="本地小型知识库 PoC 测试数据集",
        shop_id=args.shop_id,
        scene=args.scene,
    )
    imported = store.import_directory_with_assets(str(base["id"]), args.directory)
    docs = imported["documents"]
    images = imported["images"]
    print(
        json.dumps(
            {
                "status": "success",
                "base": base,
                "document_count": len(docs),
                "image_count": len(images),
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    for doc in docs:
        print(f"- {doc['title']} status={doc['status']} id={doc['id']}")
    for image in images:
        print(
            f"- [image] {image['title']} status={image['analysis_status']} "
            f"type={image['asset_type']} id={image['id']}"
        )

    if args.skip_search:
        return 0

    for question in DEFAULT_QUESTIONS:
        result = store.search(
            question,
            base_ids=[str(base["id"])],
            shop_id=args.shop_id,
            scene=args.scene,
            top_k=3,
        )
        print(f"\nQ: {question}")
        for index, item in enumerate(result["results"], start=1):
            snippet = item["snippet"].replace("\n", " ")[:160]
            print(f"  {index}. {item['source_title']} score={item['score']} {snippet}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
