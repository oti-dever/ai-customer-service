from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path


_LOGGER_NAME = "rpa.platforms.pdd_web"
_LOG_FILE = Path(__file__).resolve().parents[2] / "logs" / "pdd_web" / "pdd_web.log"
_CONFIGURED = False


def configure_pdd_web_logging() -> None:
    global _CONFIGURED
    if _CONFIGURED:
        return

    logger = logging.getLogger(_LOGGER_NAME)
    logger.setLevel(logging.INFO)
    logger.propagate = True
    if not any(getattr(handler, "_pdd_web_handler", False) for handler in logger.handlers):
        _LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(
            _LOG_FILE,
            maxBytes=5_000_000,
            backupCount=10,
            encoding="utf-8",
        )
        handler._pdd_web_handler = True  # type: ignore[attr-defined]
        handler.setLevel(logging.INFO)
        handler.setFormatter(logging.Formatter("[%(asctime)s] %(levelname)s %(name)s: %(message)s"))
        logger.addHandler(handler)

    _CONFIGURED = True


def get_logger(name: str) -> logging.Logger:
    configure_pdd_web_logging()
    return logging.getLogger(name)


def log_path() -> Path:
    return _LOG_FILE
