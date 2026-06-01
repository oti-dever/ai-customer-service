from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path


_LOGGER_NAME = "rpa.platforms.wechat"
_LOG_FILE = Path(__file__).resolve().parents[2] / "logs" / "wechat" / "wechat_runtime.log"
_CONFIGURED = False


def configure_wechat_runtime_logging() -> None:
    global _CONFIGURED
    if _CONFIGURED:
        return

    logger = logging.getLogger(_LOGGER_NAME)
    logger.setLevel(logging.INFO)
    logger.propagate = False

    if not any(getattr(handler, "_wechat_runtime_handler", False) for handler in logger.handlers):
        _LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(
            _LOG_FILE,
            maxBytes=2_000_000,
            backupCount=5,
            encoding="utf-8",
        )
        handler._wechat_runtime_handler = True  # type: ignore[attr-defined]
        handler.setLevel(logging.INFO)
        handler.setFormatter(
            logging.Formatter("[%(asctime)s] [%(levelname)s] [%(name)s] %(message)s")
        )
        logger.addHandler(handler)

    _CONFIGURED = True


def get_logger(name: str) -> logging.Logger:
    configure_wechat_runtime_logging()
    return logging.getLogger(name)
