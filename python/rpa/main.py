import sqlite3
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DB_PATH = PROJECT_ROOT / "database" / "app.db"


def ensure_db_path():
    # 仅作为示例，真实路径应与 Qt 端保持一致，可通过配置文件传入
    print(f"[RPA-Reader] 使用数据库路径: {DB_PATH}")


def insert_fake_message():
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()

    # 简单示例：找一条 qianniu 会话，没有就跳过
    cur.execute(
        "SELECT id, customer_name FROM conversations WHERE platform = ? ORDER BY id LIMIT 1",
        ("qianniu",),
    )
    row = cur.fetchone()
    if not row:
        print("[RPA-Reader] 未找到 qianniu 会话，暂不写入假消息")
        conn.close()
        return

    conv_id, customer_name = row
    content = f"【Python RPA 假消息】来自 {customer_name}"

    cur.execute(
        """
        INSERT INTO messages
        (conversation_id, direction, content, sender, platform_msg_id, sync_status, error_reason)
        VALUES (?, 'in', ?, 'customer', NULL, 1, '')
        """,
        (conv_id, content),
    )
    conn.commit()
    conn.close()
    print(f"[RPA-Reader] 已向会话 {conv_id} 写入一条假消息")


def main():
    ensure_db_path()
    while True:
        insert_fake_message()
        time.sleep(10)


if __name__ == "__main__":
    main()

