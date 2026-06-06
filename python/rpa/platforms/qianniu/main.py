from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass

from .reader import MessageRecord, QianniuReader
from .sender import QianniuSender
from .sessions import QianniuSessionReader, SessionItem

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")


@dataclass(frozen=True)
class RunResult:
    ok: bool
    stage: str
    detail: str = ""
    last_message: MessageRecord | None = None
    reply: str = ""
    sent: bool = False
    send_method: str = ""
    session: SessionItem | None = None


class QianniuCliRunner:
    def __init__(self, dry_run: bool = True) -> None:
        self.reader = QianniuReader()
        self.sender = QianniuSender()
        self.sessions = QianniuSessionReader()
        self.dry_run = dry_run

    def run_once(self, reply: str = "") -> RunResult:
        if reply:
            result = self.sender.send_text(reply, dry_run=self.dry_run)
            return RunResult(
                ok=result.ok,
                stage=result.stage,
                detail=result.detail,
                reply=reply,
                sent=result.ok and not self.dry_run,
                send_method=result.method,
            )

        messages = self.reader.read_visible_messages(limit=30)
        last = messages[-1] if messages else None
        if not last:
            return RunResult(ok=False, stage="reply", detail="reply is empty and no message context")

        reply_text = f"收到：{last.text}"
        result = self.sender.send_text(reply_text, dry_run=self.dry_run)
        return RunResult(
            ok=result.ok,
            stage=result.stage,
            detail=result.detail,
            last_message=last,
            reply=reply_text,
            sent=result.ok and not self.dry_run,
            send_method=result.method,
        )

    def run_first_unread(self, reply: str = "") -> RunResult:
        sessions = self.sessions.read_visible_sessions(limit=100, detect_unread=True)
        target = next((item for item in sessions if item.unread), None)
        if not target:
            return RunResult(ok=False, stage="find_unread", detail="no unread session")

        ok, method = self.sessions.select_session(target)
        if not ok:
            return RunResult(ok=False, stage="select_session", detail=method, session=target)

        if reply:
            result = self.sender.send_text(
                reply,
                dry_run=self.dry_run,
                chat_root=self.sessions.current_chat_root(),
            )
            return RunResult(
                ok=result.ok,
                stage=result.stage,
                detail=result.detail,
                reply=reply,
                sent=result.ok and not self.dry_run,
                send_method=result.method,
                session=target,
            )

        messages = self.reader.read_visible_messages(limit=30, chat_root=self.sessions.current_chat_root())
        last = messages[-1] if messages else None
        if not last:
            return RunResult(ok=False, stage="read", detail="no messages after session select", session=target)

        reply_text = f"收到：{last.text}"
        result = self.sender.send_text(
            reply_text,
            dry_run=self.dry_run,
            chat_root=self.sessions.current_chat_root(),
        )
        send_method = result.method
        if method and method != "dry_run":
            send_method = f"{method}+{send_method}" if send_method else method
        return RunResult(
            ok=result.ok,
            stage=result.stage,
            detail=result.detail,
            last_message=last,
            reply=reply_text,
            sent=result.ok and not self.dry_run,
            send_method=send_method,
            session=target,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description="千牛命令行闭环验证入口")
    parser.add_argument("--once", action="store_true", help="执行一次读取/回复流程")
    parser.add_argument("--first-unread", action="store_true", help="优先切到第一个 unread=True 的会话")
    parser.add_argument("--reply", default="", help="固定回复文本；为空时会基于最后一条消息生成回显回复")
    parser.add_argument("--send", action="store_true", help="真实发送；默认 dry-run")
    parser.add_argument("--yes", action="store_true", help="真实发送时跳过确认")
    args = parser.parse_args()

    if args.send and not args.yes:
        answer = input("这会真实发送千牛消息，输入 SEND 确认：").strip()
        if answer != "SEND":
            print("cancelled")
            return 130

    if not args.once:
        print("当前入口先使用 --once 执行单次闭环验证。")
        return 2

    runner = QianniuCliRunner(dry_run=not args.send)
    result = runner.run_first_unread(reply=args.reply) if args.first_unread else runner.run_once(reply=args.reply)
    last = result.last_message
    print(
        f"ok={result.ok} stage={result.stage} sent={result.sent} method={result.send_method or '-'} "
        f"detail={result.detail or '-'}"
    )
    if last:
        print(
            f"last: time={last.timestamp} sender={last.sender} "
            f"direction={last.direction} status={last.status or '-'} text={last.text}"
        )
    if result.session:
        print(
            f"session: title={result.session.title} unread={result.session.unread} "
            f"score={result.session.unread_score:.4f}"
        )
    if result.reply:
        print(f"reply: {result.reply}")
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
