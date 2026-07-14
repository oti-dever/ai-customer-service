from __future__ import annotations

import json
import logging
import re
import smtplib
import ssl
import time
import uuid
from dataclasses import dataclass
from email.message import EmailMessage
from hashlib import md5
from pathlib import Path
from typing import Any


CONFIG_PATH = Path(__file__).resolve().parents[1] / "rpa" / "config" / "email_service.local.json"
TEMPLATES_PATH = Path(__file__).resolve().parents[1] / "rpa" / "config" / "email_templates.local.json"
DEFAULT_SUBJECT = "hi"
DEFAULT_BODY = "你好"
_EMAIL_RE = re.compile(r"^[A-Z0-9._%+\-]+@[A-Z0-9.\-]+\.[A-Z]{2,}$", re.IGNORECASE)
_TEMPLATE_BLOCK_RE = re.compile(r"主题[:：]\s*(.*?)\s*正文[:：]\s*(.*?)(?=\n\s*主题[:：]|\Z)", re.S)


def _now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def _mask_email(email: str) -> str:
    email = (email or "").strip()
    if "@" not in email:
        return email
    name, domain = email.split("@", 1)
    if len(name) <= 2:
        masked = name[:1] + "***"
    else:
        masked = name[:2] + "***"
    return f"{masked}@{domain}"


def _provider_defaults(provider: str) -> dict[str, Any]:
    provider = (provider or "").strip().lower()
    if provider in {"qq", "qq_mail", "qqmail"}:
        return {"provider": "qq", "smtp_host": "smtp.qq.com", "smtp_port": 465, "security": "ssl"}
    if provider in {"gmail", "google"}:
        return {"provider": "gmail", "smtp_host": "smtp.gmail.com", "smtp_port": 587, "security": "starttls"}
    return {"provider": "custom", "smtp_host": "", "smtp_port": 465, "security": "ssl"}


def _normalize_provider(provider: str) -> str:
    provider = (provider or "").strip().lower()
    if provider in {"qq", "qq_mail", "qqmail"}:
        return "qq"
    if provider in {"gmail", "google"}:
        return "gmail"
    return "custom"


def _normalize_security(security: str) -> str:
    value = (security or "").strip().lower()
    if value in {"starttls", "tls"}:
        return "starttls"
    if value in {"none", "plain", "off"}:
        return "none"
    return "ssl"


def _string_list(value: Any) -> list[str]:
    if isinstance(value, list):
        out = [str(item).strip() for item in value if str(item or "").strip()]
    else:
        text = str(value or "").strip()
        out = [part.strip() for part in re.split(r"[,，\n]+", text) if part.strip()]
    deduped: list[str] = []
    seen: set[str] = set()
    for item in out:
        key = item.lower()
        if key in seen:
            continue
        seen.add(key)
        deduped.append(item)
    return deduped


def _template_id_from_text(text: str) -> str:
    digest = md5((text or str(uuid.uuid4())).encode("utf-8")).hexdigest()[:12]
    return f"tpl_{digest}"


def _infer_template_aliases(subject: str, body: str) -> list[str]:
    aliases: list[str] = []
    candidates: list[str] = []
    first_subject_part = re.split(r"[,，、\s]+", subject.strip(), maxsplit=1)[0].strip()
    if first_subject_part:
        candidates.append(first_subject_part)
        candidates.append(first_subject_part.replace("店铺", "").strip())
    candidates.extend(re.findall(r"【([^】]{1,40})】", body or ""))
    for candidate in candidates:
        candidate = candidate.strip()
        if not candidate:
            continue
        aliases.append(candidate)
        if "店铺" not in candidate:
            aliases.append(f"{candidate}店铺")
        aliases.append(f"{candidate}看图地址")
        aliases.append(f"{candidate}粉丝群")
    return _string_list(aliases)


@dataclass
class EmailConfig:
    enabled: bool = True
    provider: str = "qq"
    sender_email: str = ""
    auth_code: str = ""
    smtp_host: str = "smtp.qq.com"
    smtp_port: int = 465
    security: str = "ssl"
    updated_at: str = ""

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "EmailConfig":
        provider = _normalize_provider(str(data.get("provider") or "qq"))
        defaults = _provider_defaults(provider)
        smtp_port_raw = data.get("smtp_port", defaults["smtp_port"])
        try:
            smtp_port = int(smtp_port_raw)
        except (TypeError, ValueError):
            smtp_port = int(defaults["smtp_port"])
        return cls(
            enabled=bool(data.get("enabled", False)),
            provider=provider,
            sender_email=str(data.get("sender_email") or "").strip(),
            auth_code=str(data.get("auth_code") or ""),
            smtp_host=str(data.get("smtp_host") or defaults["smtp_host"]).strip(),
            smtp_port=max(1, smtp_port),
            security=_normalize_security(str(data.get("security") or defaults["security"])),
            updated_at=str(data.get("updated_at") or ""),
        )

    def to_storage_dict(self) -> dict[str, Any]:
        return {
            "enabled": self.enabled,
            "provider": self.provider,
            "sender_email": self.sender_email,
            "auth_code": self.auth_code,
            "smtp_host": self.smtp_host,
            "smtp_port": self.smtp_port,
            "security": self.security,
            "updated_at": self.updated_at or _now_iso(),
        }

    def to_public_dict(self) -> dict[str, Any]:
        return {
            "enabled": self.enabled,
            "provider": self.provider,
            "sender_email": self.sender_email,
            "sender_email_masked": _mask_email(self.sender_email),
            "smtp_host": self.smtp_host,
            "smtp_port": self.smtp_port,
            "security": self.security,
            "auth_code_saved": bool(self.auth_code),
            "updated_at": self.updated_at,
        }


@dataclass
class EmailTemplate:
    template_id: str = ""
    name: str = ""
    scene: str = "store_view_link"
    subject: str = ""
    body: str = ""
    aliases: list[str] | None = None
    enabled: bool = True
    platform_scope: list[str] | None = None
    robot_scope: list[str] | None = None
    store_scope: list[str] | None = None
    updated_at: str = ""

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "EmailTemplate":
        subject = str(data.get("subject") or "").strip()
        body = str(data.get("body") or "")
        name = str(data.get("name") or "").strip() or subject[:40]
        template_id = str(data.get("template_id") or data.get("id") or "").strip()
        if not template_id:
            template_id = _template_id_from_text(subject + "\n" + body)
        return cls(
            template_id=template_id,
            name=name,
            scene=str(data.get("scene") or "store_view_link").strip() or "store_view_link",
            subject=subject,
            body=body,
            aliases=_string_list(data.get("aliases")),
            enabled=bool(data.get("enabled", True)),
            platform_scope=_string_list(data.get("platform_scope")),
            robot_scope=_string_list(data.get("robot_scope")),
            store_scope=_string_list(data.get("store_scope")),
            updated_at=str(data.get("updated_at") or ""),
        )

    def to_dict(self, include_body: bool = True) -> dict[str, Any]:
        data = {
            "template_id": self.template_id,
            "name": self.name,
            "scene": self.scene,
            "subject": self.subject,
            "aliases": self.aliases or [],
            "enabled": self.enabled,
            "platform_scope": self.platform_scope or [],
            "robot_scope": self.robot_scope or [],
            "store_scope": self.store_scope or [],
            "updated_at": self.updated_at,
        }
        if include_body:
            data["body"] = self.body
        else:
            data["body_chars"] = len(self.body or "")
        return data


class EmailService:
    def __init__(self, config_path: Path = CONFIG_PATH, templates_path: Path = TEMPLATES_PATH) -> None:
        self.config_path = config_path
        self.templates_path = templates_path

    def load_config(self) -> EmailConfig:
        if not self.config_path.exists():
            return EmailConfig()
        try:
            data = json.loads(self.config_path.read_text(encoding="utf-8"))
        except Exception as exc:
            logging.warning("Email config load failed path=%s error=%s", self.config_path, exc)
            return EmailConfig()
        return EmailConfig.from_dict(data if isinstance(data, dict) else {})

    def save_config(self, payload: dict[str, Any]) -> dict[str, Any]:
        existing = self.load_config()
        provider = _normalize_provider(str(payload.get("provider") or existing.provider or "qq"))
        defaults = _provider_defaults(provider)
        auth_code = str(payload.get("auth_code") or "")
        if not auth_code and bool(payload.get("keep_existing_auth_code", True)):
            auth_code = existing.auth_code
        smtp_port_raw = payload.get("smtp_port", defaults["smtp_port"])
        try:
            smtp_port = int(smtp_port_raw)
        except (TypeError, ValueError):
            return {"status": "error", "error": "invalid_smtp_port", "detail": "SMTP 端口不合法"}
        config = EmailConfig(
            enabled=bool(payload.get("enabled", True)),
            provider=provider,
            sender_email=str(payload.get("sender_email") or existing.sender_email).strip(),
            auth_code=auth_code,
            smtp_host=str(payload.get("smtp_host") or defaults["smtp_host"]).strip(),
            smtp_port=max(1, smtp_port),
            security=_normalize_security(str(payload.get("security") or defaults["security"])),
            updated_at=_now_iso(),
        )
        error = self._validate_config(config, require_auth=False)
        if error:
            return error
        self.config_path.parent.mkdir(parents=True, exist_ok=True)
        self.config_path.write_text(
            json.dumps(config.to_storage_dict(), ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        logging.info(
            "Email config saved provider=%s sender=%s smtp_host=%s smtp_port=%s security=%s auth_code_saved=%s",
            config.provider,
            _mask_email(config.sender_email),
            config.smtp_host,
            config.smtp_port,
            config.security,
            bool(config.auth_code),
        )
        return {"status": "success", "config": config.to_public_dict()}

    def list_templates(self, include_body: bool = True) -> dict[str, Any]:
        templates = self._load_templates()
        return {
            "status": "success",
            "templates": [template.to_dict(include_body=include_body) for template in templates],
        }

    def save_template(self, payload: dict[str, Any]) -> dict[str, Any]:
        template = EmailTemplate.from_dict(payload)
        error = self._validate_template(template)
        if error:
            return error
        template.updated_at = _now_iso()
        templates = self._load_templates()
        replaced = False
        for index, existing in enumerate(templates):
            if existing.template_id == template.template_id:
                templates[index] = template
                replaced = True
                break
        if not replaced:
            existing_ids = {item.template_id for item in templates}
            while template.template_id in existing_ids:
                template.template_id = _template_id_from_text(template.template_id + str(uuid.uuid4()))
            templates.append(template)
        self._save_templates(templates)
        logging.info(
            "Email template saved template_id=%s scene=%s enabled=%s subject_chars=%d body_chars=%d aliases=%d",
            template.template_id,
            template.scene,
            template.enabled,
            len(template.subject),
            len(template.body),
            len(template.aliases or []),
        )
        return {"status": "success", "template": template.to_dict(include_body=True)}

    def delete_template(self, template_id: str) -> dict[str, Any]:
        template_id = (template_id or "").strip()
        if not template_id:
            return {"status": "error", "error": "missing_template_id", "detail": "模板 ID 不能为空"}
        templates = self._load_templates()
        remaining = [template for template in templates if template.template_id != template_id]
        if len(remaining) == len(templates):
            return {"status": "error", "error": "template_not_found", "detail": "邮件模板不存在"}
        self._save_templates(remaining)
        logging.info("Email template deleted template_id=%s", template_id)
        return {"status": "success", "template_id": template_id}

    def import_templates(self, payload: dict[str, Any]) -> dict[str, Any]:
        incoming: list[EmailTemplate] = []
        raw_templates = payload.get("templates")
        if isinstance(raw_templates, list):
            incoming.extend(
                EmailTemplate.from_dict(item)
                for item in raw_templates
                if isinstance(item, dict)
            )
        text = str(payload.get("text") or "")
        if text.strip():
            incoming.extend(self._parse_templates_from_text(text))
        if not incoming:
            return {"status": "error", "error": "empty_templates", "detail": "没有可导入的邮件模板"}

        existing = self._load_templates()
        by_id = {template.template_id: template for template in existing}
        imported: list[dict[str, Any]] = []
        for template in incoming:
            error = self._validate_template(template)
            if error:
                logging.warning(
                    "Email template import skipped template_id=%s error=%s",
                    template.template_id,
                    error.get("error"),
                )
                continue
            template.updated_at = _now_iso()
            while template.template_id in by_id:
                template.template_id = _template_id_from_text(template.template_id + str(uuid.uuid4()))
            by_id[template.template_id] = template
            imported.append(template.to_dict(include_body=True))
        if not imported:
            return {"status": "error", "error": "no_valid_templates", "detail": "没有有效的邮件模板"}
        self._save_templates(list(by_id.values()))
        logging.info("Email templates imported count=%d", len(imported))
        return {"status": "success", "imported": imported, "count": len(imported)}

    def test_send(self, to_email: str) -> dict[str, Any]:
        return self.send(to_email=to_email, scene="email_test")

    def send(
        self,
        to_email: str,
        scene: str = "manual",
        trace_id: str = "",
        conversation_id: int | None = None,
        template_id: str = "",
    ) -> dict[str, Any]:
        config = self.load_config()
        error = self._validate_config(config, require_auth=True)
        if error:
            logging.warning(
                "Email send rejected stage=config scene=%s trace_id=%s conversation_id=%s template_id=%s error=%s detail=%s",
                scene,
                trace_id,
                conversation_id,
                template_id,
                error.get("error"),
                error.get("detail"),
            )
            return error
        to_email = (to_email or "").strip()
        if not _EMAIL_RE.match(to_email):
            logging.warning(
                "Email send rejected stage=recipient scene=%s trace_id=%s conversation_id=%s template_id=%s to=%s",
                scene,
                trace_id,
                conversation_id,
                template_id,
                _mask_email(to_email),
            )
            return {"status": "error", "error": "invalid_recipient", "detail": "收件邮箱格式不合法"}

        template = self._find_template(template_id)
        if template_id.strip() and template is None:
            logging.warning(
                "Email send rejected stage=template_not_found scene=%s trace_id=%s conversation_id=%s template_id=%s to=%s",
                scene,
                trace_id,
                conversation_id,
                template_id,
                _mask_email(to_email),
            )
            return {"status": "error", "error": "template_not_found", "detail": "邮件模板不存在或未启用"}
        subject = template.subject if template else DEFAULT_SUBJECT
        body = template.body if template else DEFAULT_BODY
        if not subject.strip() or not body.strip():
            logging.warning(
                "Email send rejected stage=invalid_template scene=%s trace_id=%s conversation_id=%s template_id=%s subject_chars=%d body_chars=%d",
                scene,
                trace_id,
                conversation_id,
                template.template_id if template else "",
                len(subject),
                len(body),
            )
            return {"status": "error", "error": "invalid_template", "detail": "邮件模板主题或正文为空"}

        started = time.monotonic()
        message_id = f"mail-{uuid.uuid4().hex}"
        message = EmailMessage()
        message["Subject"] = subject
        message["From"] = config.sender_email
        message["To"] = to_email
        message["X-YY-Trace-Message-Id"] = message_id
        if template:
            message["X-YY-Email-Template-Id"] = template.template_id
        if trace_id:
            message["X-YY-Trace-Id"] = trace_id
        message.set_content(body, subtype="plain", charset="utf-8")

        logging.info(
            "Email send started scene=%s trace_id=%s conversation_id=%s template_id=%s message_id=%s from=%s to=%s provider=%s security=%s",
            scene,
            trace_id,
            conversation_id,
            template.template_id if template else "",
            message_id,
            _mask_email(config.sender_email),
            _mask_email(to_email),
            config.provider,
            config.security,
        )
        try:
            self._send_message(config, message)
        except smtplib.SMTPAuthenticationError:
            return self._error("smtp_auth_failed", "邮箱认证失败，请检查授权码", started,
                               scene, trace_id, conversation_id, template.template_id if template else "", message_id, to_email)
        except TimeoutError:
            return self._error("smtp_timeout", "SMTP 连接超时", started,
                               scene, trace_id, conversation_id, template.template_id if template else "", message_id, to_email)
        except (OSError, smtplib.SMTPConnectError) as exc:
            return self._error("smtp_connect_failed", f"SMTP 连接失败：{exc}", started,
                               scene, trace_id, conversation_id, template.template_id if template else "", message_id, to_email)
        except smtplib.SMTPException as exc:
            return self._error("smtp_send_failed", f"SMTP 发送失败：{exc}", started,
                               scene, trace_id, conversation_id, template.template_id if template else "", message_id, to_email)
        except Exception as exc:  # pragma: no cover - defensive boundary
            logging.exception("Email send unknown error message_id=%s", message_id)
            return self._error("unknown_error", str(exc), started,
                               scene, trace_id, conversation_id, template.template_id if template else "", message_id, to_email)

        elapsed_ms = int((time.monotonic() - started) * 1000)
        logging.info(
            "Email send finished status=success scene=%s trace_id=%s conversation_id=%s template_id=%s message_id=%s elapsed_ms=%d to=%s",
            scene,
            trace_id,
            conversation_id,
            template.template_id if template else "",
            message_id,
            elapsed_ms,
            _mask_email(to_email),
        )
        return {
            "status": "success",
            "message": "sent",
            "message_id": message_id,
            "sent_at": _now_iso(),
            "elapsed_ms": elapsed_ms,
            "template_id": template.template_id if template else "",
        }

    def _load_templates(self) -> list[EmailTemplate]:
        if not self.templates_path.exists():
            return []
        try:
            data = json.loads(self.templates_path.read_text(encoding="utf-8"))
        except Exception as exc:
            logging.warning("Email templates load failed path=%s error=%s", self.templates_path, exc)
            return []
        raw_templates = data.get("templates") if isinstance(data, dict) else data
        if not isinstance(raw_templates, list):
            return []
        templates: list[EmailTemplate] = []
        for item in raw_templates:
            if isinstance(item, dict):
                templates.append(EmailTemplate.from_dict(item))
        return templates

    def _save_templates(self, templates: list[EmailTemplate]) -> None:
        self.templates_path.parent.mkdir(parents=True, exist_ok=True)
        self.templates_path.write_text(
            json.dumps(
                {"templates": [template.to_dict(include_body=True) for template in templates]},
                ensure_ascii=False,
                indent=2,
            ),
            encoding="utf-8",
        )

    def _find_template(self, template_id: str) -> EmailTemplate | None:
        template_id = (template_id or "").strip()
        if not template_id:
            return None
        for template in self._load_templates():
            if template.template_id == template_id and template.enabled:
                return template
        return None

    def _parse_templates_from_text(self, text: str) -> list[EmailTemplate]:
        templates: list[EmailTemplate] = []
        for match in _TEMPLATE_BLOCK_RE.finditer(text or ""):
            subject = match.group(1).strip()
            body = match.group(2).strip()
            if not subject or not body:
                continue
            name = subject.split("，", 1)[0].split(",", 1)[0].strip() or subject[:40]
            templates.append(
                EmailTemplate(
                    template_id=_template_id_from_text(subject + "\n" + body),
                    name=name,
                    scene="store_view_link",
                    subject=subject,
                    body=body,
                    aliases=_infer_template_aliases(subject, body),
                    enabled=True,
                    platform_scope=[],
                    robot_scope=[],
                    store_scope=[],
                    updated_at=_now_iso(),
                )
            )
        return templates

    def _validate_template(self, template: EmailTemplate) -> dict[str, Any] | None:
        if not template.template_id.strip():
            return {"status": "error", "error": "missing_template_id", "detail": "模板 ID 不能为空"}
        if not re.match(r"^[A-Za-z0-9_\-:.]{3,80}$", template.template_id):
            return {"status": "error", "error": "invalid_template_id", "detail": "模板 ID 只能包含字母、数字、下划线、短横线、点和冒号"}
        if not template.name.strip():
            return {"status": "error", "error": "missing_template_name", "detail": "模板名称不能为空"}
        if not template.subject.strip():
            return {"status": "error", "error": "missing_template_subject", "detail": "邮件主题不能为空"}
        if not template.body.strip():
            return {"status": "error", "error": "missing_template_body", "detail": "邮件正文不能为空"}
        return None

    def _send_message(self, config: EmailConfig, message: EmailMessage) -> None:
        timeout = 20
        if config.security == "ssl":
            context = ssl.create_default_context()
            with smtplib.SMTP_SSL(config.smtp_host, config.smtp_port, timeout=timeout, context=context) as smtp:
                smtp.login(config.sender_email, config.auth_code)
                smtp.send_message(message)
            return
        with smtplib.SMTP(config.smtp_host, config.smtp_port, timeout=timeout) as smtp:
            if config.security == "starttls":
                context = ssl.create_default_context()
                smtp.starttls(context=context)
            smtp.login(config.sender_email, config.auth_code)
            smtp.send_message(message)

    def _validate_config(self, config: EmailConfig, require_auth: bool) -> dict[str, Any] | None:
        if require_auth and not config.enabled:
            return {"status": "error", "error": "email_disabled", "detail": "邮件服务未启用"}
        if not _EMAIL_RE.match(config.sender_email):
            return {"status": "error", "error": "invalid_sender", "detail": "发件邮箱格式不合法"}
        if require_auth and not config.auth_code:
            return {"status": "error", "error": "email_not_configured", "detail": "请先填写邮箱授权码"}
        if not config.smtp_host:
            return {"status": "error", "error": "missing_smtp_host", "detail": "SMTP 主机不能为空"}
        if config.smtp_port <= 0:
            return {"status": "error", "error": "invalid_smtp_port", "detail": "SMTP 端口不合法"}
        return None

    def _error(
        self,
        error: str,
        detail: str,
        started: float,
        scene: str = "",
        trace_id: str = "",
        conversation_id: int | None = None,
        template_id: str = "",
        message_id: str = "",
        to_email: str = "",
    ) -> dict[str, Any]:
        elapsed_ms = int((time.monotonic() - started) * 1000)
        logging.warning(
            "Email send failed error=%s scene=%s trace_id=%s conversation_id=%s template_id=%s message_id=%s elapsed_ms=%d to=%s detail=%s",
            error,
            scene,
            trace_id,
            conversation_id,
            template_id,
            message_id,
            elapsed_ms,
            _mask_email(to_email),
            detail,
        )
        return {"status": "error", "error": error, "detail": detail, "elapsed_ms": elapsed_ms}


_EMAIL_SERVICE = EmailService()


def get_email_service() -> EmailService:
    return _EMAIL_SERVICE
