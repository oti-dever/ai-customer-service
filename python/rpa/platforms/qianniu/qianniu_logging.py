from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path


_LOGGER_NAME = "rpa.platforms.qianniu"
_TIMING_LOG_FILE = Path(__file__).resolve().parents[2] / "logs" / "qianniu" / "qianniu_timing.log"
_CONFIGURED = False


class _TimingOnlyFilter(logging.Filter):
    def filter(self, record: logging.LogRecord) -> bool:
        return "timing" in record.getMessage()


def configure_qianniu_timing_logging() -> None:
    global _CONFIGURED
    if _CONFIGURED:
        return

    logger = logging.getLogger(_LOGGER_NAME)
    logger.setLevel(logging.INFO)

    if not any(getattr(handler, "_qianniu_timing_handler", False) for handler in logger.handlers):
        _TIMING_LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(
            _TIMING_LOG_FILE,
            maxBytes=5_000_000,
            backupCount=10,
            encoding="utf-8",
        )
        handler._qianniu_timing_handler = True  # type: ignore[attr-defined]
        handler.setLevel(logging.INFO)
        handler.addFilter(_TimingOnlyFilter())
        handler.setFormatter(
            logging.Formatter("[%(asctime)s] [%(levelname)s] [%(name)s] %(message)s")
        )
        logger.addHandler(handler)

    _CONFIGURED = True


def get_logger(name: str) -> logging.Logger:
    configure_qianniu_timing_logging()
    return logging.getLogger(name)


def timing_log_path() -> Path:
    return _TIMING_LOG_FILE
