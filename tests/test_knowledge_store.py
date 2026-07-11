import shutil
import sqlite3
import sys
import unittest
import uuid
import os
import base64
import json
import importlib.util
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = REPO_ROOT / "python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from service.knowledge_store import ArkDoubaoImageAssetAnalyzer, KnowledgeStore, extract_text


TINY_PNG = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAFgwJ/luzrWQAAAABJRU5ErkJggg=="
)


class FakeImageAnalyzer:
    def __init__(self, risk_tags=None):
        self.risk_tags = risk_tags or []

    def analyze(self, path: Path):
        return {
            "asset_type": "实拍图",
            "summary": "一张白色二次元主题空格键键帽实拍图，适合客户查看外观效果。",
            "ocr_text": "",
            "tags": ["键帽", "空格键", "实拍", "二次元"],
            "scenarios": ["外观咨询", "实拍图咨询"],
            "risk_tags": self.risk_tags,
            "suggested_reply": "亲，这张图可以参考下，是空格键键帽的实拍效果。",
            "analysis_model": "fake-vision-test",
            "width": 1,
            "height": 1,
            "raw_analysis": {"fixture": path.name},
        }


def make_test_store(image_analyzer=None):
    os.environ["AI_CUSTOMER_SERVICE_EMBEDDING_MODEL"] = "local-hash-ngram-v1"
    root = REPO_ROOT / ".codex_tmp" / "knowledge_unit_tests" / uuid.uuid4().hex
    root.mkdir(parents=True, exist_ok=True)

    class SharedConnection(sqlite3.Connection):
        def close(self):
            pass

        def real_close(self):
            super().close()

    conn = sqlite3.connect(":memory:", factory=SharedConnection)
    conn.row_factory = sqlite3.Row
    store = KnowledgeStore(files_dir=root / "files", connection_factory=lambda: conn, image_analyzer=image_analyzer)
    return store, conn, root


class KnowledgeStoreTests(unittest.TestCase):
    def test_base_management_fields_and_counts(self):
        store, conn, root = make_test_store(image_analyzer=FakeImageAnalyzer())
        try:
            source_dir = root / "kb-source"
            source_dir.mkdir()
            doc = source_dir / "policy.txt"
            doc.write_text("K87 orders paid before 16:00 can ship the same day.", encoding="utf-8")
            image = source_dir / "k87.png"
            image.write_bytes(TINY_PNG)

            base = store.create_base(
                "旗舰店知识库",
                source_directory=str(source_dir),
                applicable_shops="官方旗舰店",
            )
            store.import_directory_with_assets(base["id"], source_dir)
            updated = store.update_base(
                base["id"],
                name="旗舰店知识库 V2",
                applicable_shops="官方旗舰店、私域通用",
                enabled=False,
            )
            bases = store.list_bases()
            listed = next(item for item in bases if item["id"] == base["id"])

            self.assertEqual(updated["name"], "旗舰店知识库 V2")
            self.assertEqual(updated["enabled"], 0)
            self.assertEqual(listed["document_count"], 1)
            self.assertEqual(listed["image_count"], 1)
            self.assertEqual(listed["last_import_status"], "success")
            self.assertEqual(listed["last_import_document_count"], 1)
            self.assertEqual(listed["last_import_image_count"], 1)
            self.assertEqual(listed["applicable_shops"], "官方旗舰店、私域通用")
            self.assertEqual(listed["source_directory"], str(source_dir.resolve()))
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_platform_bindings_return_only_enabled_bases(self):
        store, conn, root = make_test_store()
        try:
            enabled_base = store.create_base("enabled-base")
            disabled_base = store.create_base("disabled-base", enabled=False)

            saved = store.set_platform_bindings("Qianniu", [enabled_base["id"], disabled_base["id"]])
            current = store.get_platform_bindings("qianniu")

            self.assertEqual(saved, [enabled_base["id"]])
            self.assertEqual(current, [enabled_base["id"]])

            store.set_base_enabled(enabled_base["id"], False)
            self.assertEqual(store.get_platform_bindings("qianniu"), [])

            store.set_platform_bindings("qianniu", [])
            self.assertEqual(store.get_platform_bindings("qianniu"), [])
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_ark_doubao_image_analyzer_parses_structured_json(self):
        def fake_transport(request, timeout):
            body = request.data.decode("utf-8")
            self.assertIn("image_url", body)
            self.assertGreaterEqual(timeout, 5)
            content = json.dumps(
                {
                    "asset_type": "实拍图",
                    "summary": "白色空格键键帽实拍图",
                    "ocr_text": "",
                    "tags": ["键帽", "空格键"],
                    "scenarios": ["外观咨询"],
                    "risk_tags": [],
                    "suggested_reply": "亲，这张图可以参考下",
                },
                ensure_ascii=False,
            )
            return json.dumps({"choices": [{"message": {"content": content}}]}, ensure_ascii=False).encode("utf-8")

        root = REPO_ROOT / ".codex_tmp" / "knowledge_unit_tests" / uuid.uuid4().hex
        root.mkdir(parents=True, exist_ok=True)
        try:
            image = root / "spacebar.png"
            image.write_bytes(TINY_PNG)
            analyzer = ArkDoubaoImageAssetAnalyzer(
                api_key="test-key",
                model="doubao-vision-test",
                transport=fake_transport,
            )
            analysis = analyzer.analyze(image)

            self.assertEqual(analysis["asset_type"], "实拍图")
            self.assertIn("键帽", analysis["tags"])
            self.assertEqual(analysis["analysis_model"], "ark:doubao-vision-test")
            self.assertEqual(analysis["risk_tags"], [])
        finally:
            shutil.rmtree(root, ignore_errors=True)

    def test_import_directory_and_search_keyboard_docs(self):
        store, conn, root = make_test_store()
        try:
            base = store.create_base(
                "键盘测试知识库",
                shop_id="keyboard-demo-shop",
                scene="reply_draft",
            )

            docs = store.import_directory(base["id"], REPO_ROOT / "docs" / "knowledge-base")

            self.assertGreaterEqual(len(docs), 4)
            self.assertTrue(all(doc["status"] == "ready" for doc in docs))

            result = store.search(
                "键盘收到后有一个轴不触发，可以换吗？",
                base_ids=[base["id"]],
                shop_id="keyboard-demo-shop",
                scene="reply_draft",
                top_k=3,
            )

            self.assertEqual(result["status"], "success")
            self.assertGreaterEqual(len(result["results"]), 1)
            joined = "\n".join(item["snippet"] for item in result["results"])
            self.assertIn("质量问题", joined)
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_search_filters_shop_scope(self):
        store, conn, root = make_test_store()
        try:
            base = store.create_base("键帽测试知识库", shop_id="shop-a", scene="reply_draft")
            store.import_file(base["id"], REPO_ROOT / "docs" / "knowledge-base" / "02_键帽材质工艺与兼容性FAQ.txt")

            hit = store.search("PBT 热升华键帽会不会打油？", shop_id="shop-a", scene="reply_draft")
            miss = store.search("PBT 热升华键帽会不会打油？", shop_id="shop-b", scene="reply_draft")

            self.assertGreaterEqual(len(hit["results"]), 1)
            self.assertEqual(miss["results"], [])
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_reimport_same_file_reuses_document_record(self):
        store, conn, root = make_test_store()
        try:
            base = store.create_base("dedupe-base", shop_id="shop-a", scene="reply_draft")
            source = root / "source.txt"
            source.write_text(
                "K87 Pro supports hot swap switches.\nWarranty follows shop policy.",
                encoding="utf-8",
            )

            first = store.import_file(base["id"], source)
            second = store.import_file(base["id"], source)
            docs = store.list_documents(base["id"])

            self.assertEqual(first["id"], second["id"])
            self.assertEqual(len(docs), 1)
            self.assertEqual(docs[0]["status"], "ready")
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    @unittest.skipUnless(importlib.util.find_spec("openpyxl"), "openpyxl is required for Excel import tests")
    def test_import_xlsx_product_list_as_structured_rows(self):
        from openpyxl import Workbook

        store, conn, root = make_test_store()
        try:
            base = store.create_base("excel-base", shop_id="shop-a", scene="reply_draft")
            source = root / "商品列表.xlsx"

            workbook = Workbook()
            sheet = workbook.active
            sheet.title = "商品列表"
            sheet.append(["键盘店铺商品列表"])
            sheet.append(["用于知识库 Excel 导入测试"])
            sheet.append([])
            sheet.append(["商品型号", "商品名称", "库存状态", "发货时效", "是否有实拍图", "客服备注"])
            sheet.append([
                "K87",
                "英菲克 K87 机械键盘",
                "现货",
                "当天 16:00 前下单可当天发货",
                "是",
                "客户问 K87 图片或实拍图时，优先推荐 K87 正面图。",
            ])
            workbook.save(source)

            extracted = extract_text(source)
            self.assertIn("【工作表：商品列表】", extracted)
            self.assertIn("说明行1：键盘店铺商品列表", extracted)
            self.assertIn("表头：商品型号 | 商品名称 | 库存状态 | 发货时效 | 是否有实拍图 | 客服备注", extracted)
            self.assertIn("商品型号=K87", extracted)
            self.assertIn("发货时效=当天 16:00 前下单可当天发货", extracted)

            doc = store.import_file(base["id"], source)
            self.assertEqual(doc["status"], "ready")

            chunk_row = conn.execute(
                "SELECT content FROM knowledge_chunks WHERE document_id = ? LIMIT 1",
                (doc["id"],),
            ).fetchone()
            self.assertIsNotNone(chunk_row)
            self.assertIn("是否有实拍图=是", chunk_row["content"])

            result = store.search("K87 实拍图 当天发货", base_ids=[base["id"]], mode="keyword", top_k=1)
            self.assertEqual(result["status"], "success")
            self.assertEqual(len(result["results"]), 1)
            self.assertIn("商品型号=K87", result["results"][0]["snippet"])
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_import_writes_embeddings_and_vector_search(self):
        store, conn, root = make_test_store()
        try:
            base = store.create_base("vector-base", shop_id="shop-a", scene="reply_draft")
            source = root / "vector.txt"
            source.write_text(
                "K87 Pro 机械键盘支持热插拔轴体，单个按键不触发时可先更换轴体排查。",
                encoding="utf-8",
            )

            doc = store.import_file(base["id"], source)
            self.assertEqual(doc["status"], "ready")

            chunk_row = conn.execute(
                """
                SELECT embedding, embedding_model, embedding_dim
                FROM knowledge_chunks
                WHERE document_id = ?
                LIMIT 1
                """,
                (doc["id"],),
            ).fetchone()
            self.assertIsNotNone(chunk_row)
            self.assertTrue(chunk_row["embedding"])
            self.assertEqual(chunk_row["embedding_model"], "local-hash-ngram-v1")
            self.assertEqual(chunk_row["embedding_dim"], 384)

            result = store.search(
                "热插拔键盘按键不触发怎么处理？",
                base_ids=[base["id"]],
                shop_id="shop-a",
                scene="reply_draft",
                top_k=3,
                mode="vector",
            )

            self.assertEqual(result["status"], "success")
            self.assertEqual(result["metadata"]["mode"], "vector")
            self.assertGreaterEqual(len(result["results"]), 1)
            self.assertEqual(result["results"][0]["match_type"], "vector")
            self.assertGreater(result["results"][0]["metadata"]["vector_score"], 0)
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_hybrid_search_uses_fts_and_precise_query_weights(self):
        store, conn, root = make_test_store()
        try:
            base = store.create_base("hybrid-rank-base", shop_id="shop-a", scene="reply_draft")
            k87 = root / "K87_shipping.txt"
            k98 = root / "K98_shipping.txt"
            k87.write_text("K87 keyboard ships the same day before 16:00.", encoding="utf-8")
            k98.write_text("K98 keyboard ships the same day before 16:00.", encoding="utf-8")
            store.import_file(base["id"], k87)
            store.import_file(base["id"], k98)

            result = store.search("K87 same day shipping", base_ids=[base["id"]], mode="hybrid", top_k=2)

            self.assertEqual(result["status"], "success")
            self.assertEqual(len(result["results"]), 2)
            self.assertEqual(result["results"][0]["source_title"], "K87_shipping")
            metadata = result["results"][0]["metadata"]
            self.assertGreater(metadata["fts_score"], 0)
            self.assertEqual(metadata["weight_reason"], "precise")
            self.assertGreater(metadata["keyword_weight"], metadata["vector_weight"])
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_search_merges_adjacent_chunks_into_snippet(self):
        store, conn, root = make_test_store()
        try:
            base = store.create_base("merge-chunk-base", shop_id="shop-a", scene="reply_draft")
            source = root / "shipping_policy.txt"
            before = ("Before context explains K87 keyboard stock and order handling. " * 8).strip()
            center = ("Needle answer: same day shipping is available before 16:00. " * 8).strip()
            after = ("After context explains tracking number updates after warehouse pickup. " * 8).strip()
            source.write_text(f"{before}\n\n{center}\n\n{after}", encoding="utf-8")
            store.import_file(base["id"], source)

            result = store.search("Needle answer same day shipping", base_ids=[base["id"]], mode="keyword", top_k=1)

            self.assertEqual(result["status"], "success")
            self.assertEqual(len(result["results"]), 1)
            top = result["results"][0]
            self.assertIn("Before context", top["snippet"])
            self.assertIn("Needle answer", top["snippet"])
            self.assertIn("After context", top["snippet"])
            self.assertEqual(top["metadata"]["merged_chunk_count"], 3)
            self.assertEqual(top["metadata"]["merged_chunk_indexes"], [0, 1, 2])
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_import_directory_with_image_assets_and_search_recommendation(self):
        store, conn, root = make_test_store(image_analyzer=FakeImageAnalyzer())
        try:
            base = store.create_base("image-base", shop_id="shop-a", scene="reply_draft")
            source_dir = root / "source"
            source_dir.mkdir()
            (source_dir / "spacebar-live.png").write_bytes(TINY_PNG)

            imported = store.import_directory_with_assets(base["id"], source_dir)
            images = store.list_image_assets(base["id"])

            self.assertEqual(imported["documents"], [])
            self.assertEqual(len(imported["images"]), 1)
            self.assertEqual(len(images), 1)
            self.assertEqual(images[0]["analysis_status"], "ready")
            self.assertEqual(images[0]["asset_type"], "实拍图")
            self.assertIn("键帽", images[0]["tags"])
            self.assertTrue(Path(images[0]["file_path"]).is_file())

            result = store.search_image_assets(
                "有空格键键帽实拍图吗？",
                base_ids=[base["id"]],
                shop_id="shop-a",
                scene="reply_draft",
                top_k=3,
            )

            self.assertEqual(result["status"], "success")
            self.assertEqual(len(result["results"]), 1)
            self.assertTrue(result["results"][0]["recommendation"]["should_attach"])
            self.assertTrue(result["results"][0]["recommendation"]["requires_human_confirm"])
            self.assertIn("实拍", result["results"][0]["summary"])
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_import_directory_syncs_removed_and_renamed_image_assets(self):
        store, conn, root = make_test_store(image_analyzer=FakeImageAnalyzer())
        try:
            base = store.create_base("image-sync-base", shop_id="shop-a", scene="reply_draft")
            source_dir = root / "source"
            source_dir.mkdir()
            current = source_dir / "current.png"
            stale = source_dir / "stale.png"
            current.write_bytes(TINY_PNG)
            stale.write_bytes(TINY_PNG + b"stale")

            store.import_directory_with_assets(base["id"], source_dir)
            first_images = store.list_image_assets(base["id"])
            self.assertEqual(len(first_images), 2)
            current_hash = next(
                image["content_hash"] for image in first_images if image["original_filename"] == "current.png"
            )

            next_source_dir = root / "next-source"
            next_source_dir.mkdir()
            renamed = next_source_dir / "renamed.png"
            renamed.write_bytes(TINY_PNG)

            store.import_directory_with_assets(base["id"], next_source_dir)
            images = store.list_image_assets(base["id"])

            self.assertEqual(len(images), 1)
            self.assertEqual(images[0]["original_filename"], "renamed.png")
            self.assertEqual(images[0]["content_hash"], current_hash)
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)

    def test_image_search_filters_risky_assets_by_default(self):
        store, conn, root = make_test_store(image_analyzer=FakeImageAnalyzer(risk_tags=["含活动信息"]))
        try:
            base = store.create_base("risky-image-base", shop_id="shop-a", scene="reply_draft")
            source = root / "discount.png"
            source.write_bytes(TINY_PNG)
            store.import_image_file(base["id"], source)

            safe_result = store.search_image_assets("空格键实拍图", base_ids=[base["id"]])
            risky_result = store.search_image_assets("空格键实拍图", base_ids=[base["id"]], include_risky=True)

            self.assertEqual(safe_result["results"], [])
            self.assertEqual(len(risky_result["results"]), 1)
            self.assertFalse(risky_result["results"][0]["recommendation"]["should_attach"])
            self.assertIn("含活动信息", risky_result["results"][0]["recommendation"]["risk_notice"])
        finally:
            conn.real_close()
            shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
