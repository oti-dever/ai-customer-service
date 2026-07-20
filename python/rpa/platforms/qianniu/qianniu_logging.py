from __future__ import annotations

import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path


_LOGGER_NAME = "rpa.platforms.qianniu"
_TIMING_LOG_FILE = Path(__file__).resolve().parents[2] / "logs" / "qianniu" / "qianniu_timing.log"
_LISTEN_FLOW_LOG_FILE = Path(__file__).resolve().parents[2] / "logs" / "qianniu" / "qianniu_listen_flow.log"
_CONFIGURED = False


class _TimingOnlyFilter(logging.Filter):
    def filter(self, record: logging.LogRecord) -> bool:
        return "timing" in record.getMessage()


class _ListenFlowOnlyFilter(logging.Filter):
    def filter(self, record: logging.LogRecord) -> bool:
        return "listen_flow" in record.getMessage()


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

    if not any(getattr(handler, "_qianniu_listen_flow_handler", False) for handler in logger.handlers):
        _LISTEN_FLOW_LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(
            _LISTEN_FLOW_LOG_FILE,
            maxBytes=5_000_000,
            backupCount=10,
            encoding="utf-8",
        )
        handler._qianniu_listen_flow_handler = True  # type: ignore[attr-defined]
        handler.setLevel(logging.INFO)
        handler.addFilter(_ListenFlowOnlyFilter())
        handler.setFormatter(
            logging.Formatter("[%(asctime)s] [%(levelname)s] [%(name)s] %(message)s")
        )
        logger.addHandler(handler)

    _CONFIGURED = True


def get_logger(name: str) -> logging.Logger:
    configure_qianniu_timing_logging()
    return logging.getLogger(name)


def get_listen_flow_logger(name: str) -> logging.Logger:
    configure_qianniu_timing_logging()
    suffix = name.replace(".", "_").strip("_") if name else "flow"
    return logging.getLogger(f"{_LOGGER_NAME}.listen_flow.{suffix}")


def timing_log_path() -> Path:
    return _TIMING_LOG_FILE


def listen_flow_log_path() -> Path:
    return _LISTEN_FLOW_LOG_FILE
