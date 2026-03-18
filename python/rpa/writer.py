import sqlite3
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DB_PATH = PROJECT_ROOT / "database" / "app.db"

def open_db():
    conn = sqlite3.connect(DB_PATH, timeout=5)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    conn.execute("PRAGMA busy_timeout=3000;")
    return conn

def process_pending_send():
    conn = open_db()
    cur = conn.cursor()

    cur.execute(
        """
        SELECT m.id, m.conversation_id, m.content
        FROM messages m
        JOIN conversations c ON c.id = m.conversation_id
        WHERE m.direction = 'out'
          AND m.sync_status = 10
          AND c.platform = 'qianniu'
        ORDER BY m.id ASC
        """
    )
    rows = cur.fetchall()
    if not rows:
        conn.close()
        return

    for msg_id, conv_id, content in rows:
        print(f"[RPA-Writer] 模拟发送消息 id={msg_id}, conv={conv_id}, content={content[:20]}")
        # 这里暂时不真正操作千牛窗口，仅做状态更新
        cur.execute(
            "UPDATE messages SET sync_status = 11, error_reason = '' WHERE id = ?",
            (msg_id,),
        )

    conn.commit()
    conn.close()


def main():
    print(f"[RPA-Writer] 使用数据库路径: {DB_PATH}")
    while True:
        process_pending_send()
        time.sleep(5)


if __name__ == "__main__":
    main()

