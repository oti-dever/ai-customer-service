import sqlite3
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DB_PATH = PROJECT_ROOT / "database" / "app.db"


def open_db():
    conn = sqlite3.connect(DB_PATH, timeout=5)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")
    conn.execute("PRAGMA busy_timeout=3000;")
    return conn


def main():
    print(f"[DB-Inspect] DB={DB_PATH}")
    conn = open_db()
    try:
        cur = conn.cursor()

        def scalar(sql: str):
            cur.execute(sql)
            row = cur.fetchone()
            return row[0] if row else None

        print("[DB-Inspect] counts:")
        for table in ["conversations", "messages", "rpa_inbox_messages"]:
            try:
                print(f"  - {table}: {scalar(f'SELECT COUNT(1) FROM {table}')}")
            except sqlite3.OperationalError as e:
                print(f"  - {table}: N/A ({e})")

        print("[DB-Inspect] last 10 inbox rows (consume_status=0 first):")
        try:
            cur.execute(
                """
                SELECT id, platform, platform_conversation_id, customer_name, substr(content, 1, 30), created_at, platform_msg_id, consume_status
                FROM rpa_inbox_messages
                ORDER BY consume_status ASC, id DESC
                LIMIT 10
                """
            )
            for r in cur.fetchall():
                print("  ", r)
        except sqlite3.OperationalError as e:
            print("  N/A", e)
    finally:
        conn.close()


if __name__ == "__main__":
    main()

