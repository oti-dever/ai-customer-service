"""推荐路径下的布局解析入口。

实际实现暂时保留在 `rpa.common.layout_parser`，这里提供 `rpa.core.layout_parser`
兼容入口，便于平台代码先收敛到 core 命名空间。后续清理旧 `common/`
兼容层时，再把实现整体搬到这里。
"""
from __future__ import annotations

from ..common import layout_parser as _impl

globals().update(
    {name: getattr(_impl, name) for name in dir(_impl) if not name.startswith("__")}
)
