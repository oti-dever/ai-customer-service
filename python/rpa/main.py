import hashlib
import sqlite3
import time
from datetime import datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DB_PATH = PROJECT_ROOT / "database" / "app.db"

POLL_INTERVAL_SEC = 3

def ensure_db_path():
    # 仅作为示例，真实路径应与 Qt 端保持一致，可通过配置文件传入
    print(f"[RPA-Reader] 使用数据库路径: {DB_PATH}")

def open_db():
    conn = sqlite3.connect(DB_PATH, timeout=5)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    conn.execute("PRAGMA busy_timeout=3000;")
    return conn

def make_platform_msg_id(platform: str, platform_conversation_id: str, content: str, created_at: str) -> str:
    # 强约束：稳定唯一（尽量不依赖自增 id）
    digest = hashlib.sha1(f"{platform_conversation_id}|{created_at}|{content}".encode("utf-8")).hexdigest()[:12]
    ts_ms = int(time.time() * 1000)
    return f"{platform}:unknownShop:{platform_conversation_id}:{ts_ms}:{digest}"


def write_inbox_messages(batch: list[dict]):
    if not batch:
        return

    conn = open_db()
    try:
        cur = conn.cursor()
        cur.execute("BEGIN")
        for item in batch:
            cur.execute(
                """
                INSERT OR IGNORE INTO rpa_inbox_messages
                (platform, platform_conversation_id, customer_name, content, created_at, platform_msg_id, consume_status, error_reason)
                VALUES (?, ?, ?, ?, ?, ?, 0, '')
                """,
                (
                    item["platform"],
                    item["platform_conversation_id"],
                    item["customer_name"],
                    item["content"],
                    item["created_at"],
                    item["platform_msg_id"],
                ),
            )
        conn.commit()
    finally:
        conn.close()


def build_demo_batch() -> list[dict]:
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    platform = "qianniu"
    platform_conversation_id = "demo_conv_1"
    customer_name = "演示买家"
    content = f"【Python RPA 假消息】{now}"
    platform_msg_id = make_platform_msg_id(platform, platform_conversation_id, content, now)
    return [
        {
            "platform": platform,
            "platform_conversation_id": platform_conversation_id,
            "customer_name": customer_name,
            "content": content,
            "created_at": now,
            "platform_msg_id": platform_msg_id,
        }
    ]


def main():
    ensure_db_path()
    while True:
        batch = build_demo_batch()
        write_inbox_messages(batch)
        print(f"[RPA-Reader] 已写入 {len(batch)} 条 inbox（示例）")
        time.sleep(POLL_INTERVAL_SEC)


if __name__ == "__main__":
    main()

