"""
OCR 名称稳定器：连续 N 次一致才接受改名，减少会话抖动。

用于防止 OCR 识别波动导致的会话分裂。例如当 OCR 偶尔把"张三"识别成"张二"时，
不会立即切换会话，而是要求连续多次识别一致才接受新名称。
"""
from __future__ import annotations

from typing import Optional


class NameStabilizer:
    """
    Prevents conversation splitting from OCR flicker by requiring
    multiple consecutive consistent readings before accepting a name change.
    """

    def __init__(self, required_consistent: int = 2):
        """
        Args:
            required_consistent: 连续多少次一致才接受改名，默认 2 次。
        """
        self.current: Optional[str] = None
        self._required = required_consistent
        self._candidate: Optional[str] = None
        self._candidate_count: int = 0

    def update(self, name: Optional[str]) -> Optional[str]:
        """
        Feed an OCR reading. Returns the stabilized name.
        
        Args:
            name: 本次 OCR 识别的名称，可为 None。
            
        Returns:
            稳定后的名称。如果名称变化但未达到一致次数要求，返回旧名称。
        """
        if not name:
            return self.current
        if self.current is None:
            self.current = name
            return self.current
        if name == self.current:
            self._candidate = None
            self._candidate_count = 0
            return self.current
        # Name differs — require N consecutive identical readings
        if name == self._candidate:
            self._candidate_count += 1
            if self._candidate_count >= self._required:
                self.current = name
                self._candidate = None
                self._candidate_count = 0
        else:
            self._candidate = name
            self._candidate_count = 1
        return self.current

    def force_set(self, name: str) -> None:
        """
        Force-accept name after an intentional conversation switch.
        
        Args:
            name: 强制设置的名称。
        """
        if name:
            self.current = name
            self._candidate = None
            self._candidate_count = 0
