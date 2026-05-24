"""
Incremental detection: deduplicate messages by content_hash + fuzzy similarity.
Only emit messages not seen before (sliding window).
Persists state to file for restart resilience.

去重依据：每条 ParsedMessage 的 (side, content) 经 SHA1 得到哈希；与平台真实消息 ID 无关。
写入 inbox 的 platform_msg_id 形如 ``{platform}:{conv_id}:{content_hash}``。
"""
from __future__ import annotations

import hashlib
import json
from collections import defaultdict
from difflib import SequenceMatcher
from pathlib import Path
from typing import Dict, List, Set, Tuple

from .layout_parser import ParsedMessage


def content_hash(side: str, content: str) -> str:
    """Stable hash for deduplication."""
    return hashlib.sha1(f"{side}|{content}".encode("utf-8")).hexdigest()


def make_platform_msg_id(platform: str, conv_id: str, content_hash_str: str) -> str:
    """Unique id for inbox; used as DB unique constraint."""
    return f"{platform}:{conv_id}:{content_hash_str}"


def _text_similarity(a: str, b: str) -> float:
    """Character-level similarity ratio (0-1) via stdlib SequenceMatcher."""
    if a == b:
        return 1.0
    if not a or not b:
        return 0.0
    return SequenceMatcher(None, a, b).ratio()


class IncrementalDetector:
    """
    Tracks seen messages; only returns new ones.
    Exact hash matching + optional fuzzy similarity to handle OCR flicker.
    Optionally persists state to file for restart resilience.
    """

    def __init__(
        self,
        max_window: int = 50,
        state_path: Path | None = None,
        fuzzy_threshold: float = 1.0,
    ):
        """
        Args:
            max_window: max known hashes to keep per capacity bucket.
            state_path: JSON file for cross-restart persistence.
            fuzzy_threshold: similarity ratio above which a message is
                considered duplicate. Set to 1.0 to disable fuzzy matching
                (exact hash only). Recommended: 0.85.
        """
        self.max_window = max_window
        self.state_path = state_path
        self.fuzzy_threshold = fuzzy_threshold
        self._known: Set[str] = set()
        self._recent_texts: Dict[str, List[Tuple[str, str]]] = defaultdict(list)
        self._max_recent = 30
        self._load_state()

    def _load_state(self) -> None:
        if self.state_path and self.state_path.exists():
            try:
                data = json.loads(self.state_path.read_text(encoding="utf-8"))
                self._known = set(data.get("known", []))
                for cid, pairs in data.get("recent_texts", {}).items():
                    self._recent_texts[cid] = [
                        (p[0], p[1])
                        for p in pairs
                        if isinstance(p, (list, tuple)) and len(p) >= 2
                    ]
            except Exception:
                pass

    def _save_state(self) -> None:
        if self.state_path:
            try:
                self.state_path.parent.mkdir(parents=True, exist_ok=True)
                rt = {
                    cid: [list(p) for p in pairs[-self._max_recent :]]
                    for cid, pairs in self._recent_texts.items()
                }
                data = {"known": list(self._known), "recent_texts": rt}
                self.state_path.write_text(
                    json.dumps(data, ensure_ascii=False), encoding="utf-8"
                )
            except Exception:
                pass

    def filter_new(
        self,
        messages: List[ParsedMessage],
        conv_id: str | None = None,
    ) -> List[ParsedMessage]:
        """Return only messages not in known set and not fuzzy-matching recent texts."""
        new_list: List[ParsedMessage] = []
        for msg in messages:
            h = content_hash(msg.side, msg.content)
            key = f"{conv_id}:{h}" if conv_id else h

            if key in self._known:
                continue

            # Fuzzy similarity check (same conversation + same side)
            if conv_id and self.fuzzy_threshold < 1.0:
                recent = self._recent_texts.get(conv_id, [])
                same_side = [t for s, t in recent[-20:] if s == msg.side]
                if any(
                    _text_similarity(msg.content, t) >= self.fuzzy_threshold
                    for t in same_side
                ):
                    self._known.add(key)
                    continue

            self._known.add(key)
            if conv_id:
                self._recent_texts[conv_id].append((msg.side, msg.content))
                if len(self._recent_texts[conv_id]) > self._max_recent:
                    self._recent_texts[conv_id] = self._recent_texts[conv_id][
                        -self._max_recent :
                    ]
            new_list.append(msg)

            cap = self.max_window * 5 if conv_id else self.max_window
            while len(self._known) > cap:
                self._known.pop()

        if new_list:
            self._save_state()
        return new_list

    def purge_conversation(self, conv_id: str) -> None:
        """
        从已知哈希与 recent 中移除某 platform_conversation_id（如 DB/界面已清空该会话消息）。
        conv_id 须与 filter_new(..., conv_id=...) 传入的字符串一致，例如 wechat_贺子秋。
        """
        cid = str(conv_id or "").strip()
        if not cid:
            return
        prefix = f"{cid}:"
        self._known = {k for k in self._known if not str(k).startswith(prefix)}
        self._recent_texts.pop(cid, None)
        self._save_state()
