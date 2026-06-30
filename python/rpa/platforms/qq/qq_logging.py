from __future__ import annotations

import logging
from pathlib import Path


_LOGGER_NAME = "rpa.platforms.qq"
_LOG_FILE = Path(__file__).resolve().parents[2] / "logs" / "qq" / "qq_probe.log"


def configure_qq_logging() -> None:
    logger = logging.getLogger(_LOGGER_NAME)
    logger.setLevel(logging.INFO)
    logger.propagate = True
    if any(getattr(handler, "_qq_probe_handler", False) for handler in logger.handlers):
        return

    _LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    handler = logging.FileHandler(_LOG_FILE, encoding="utf-8")
    handler.setFormatter(logging.Formatter("[%(asctime)s] %(levelname)s %(name)s: %(message)s"))
    handler._qq_probe_handler = True  # type: ignore[attr-defined]
    logger.addHandler(handler)


def get_logger(name: str) -> logging.Logger:
    configure_qq_logging()
    return logging.getLogger(name)
