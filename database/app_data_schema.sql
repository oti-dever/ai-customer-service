CREATE TABLE IF NOT EXISTS conversations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  platform TEXT NOT NULL,
  platform_conversation_id TEXT,
  account_id TEXT DEFAULT '',
  customer_name TEXT NOT NULL,
  last_message TEXT DEFAULT '',
  last_time DATETIME,
  unread_count INTEGER DEFAULT 0,
  status TEXT DEFAULT 'new',
  updated_at DATETIME,
  deleted_at DATETIME,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(platform, platform_conversation_id)
);

CREATE TABLE IF NOT EXISTS messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  conversation_id INTEGER NOT NULL,
  platform_message_id TEXT DEFAULT '',
  client_message_id TEXT DEFAULT '',
  direction TEXT NOT NULL,
  sender TEXT NOT NULL,
  sender_name TEXT DEFAULT '',
  content_type TEXT NOT NULL DEFAULT 'text',
  content TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'observed',
  error_reason TEXT DEFAULT '',
  message_time DATETIME,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  deleted_at DATETIME,
  FOREIGN KEY(conversation_id) REFERENCES conversations(id)
);

CREATE INDEX IF NOT EXISTS idx_messages_conv_id ON messages(conversation_id);
CREATE INDEX IF NOT EXISTS idx_messages_platform_message_id ON messages(platform_message_id);
CREATE INDEX IF NOT EXISTS idx_messages_client_message_id ON messages(client_message_id);

CREATE TABLE IF NOT EXISTS wechat_conversations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  conversation_id INTEGER NOT NULL UNIQUE,
  wechat_account_id TEXT DEFAULT '',
  wechat_conversation_key TEXT DEFAULT '',
  display_name TEXT DEFAULT '',
  last_unread_badge INTEGER DEFAULT 0,
  last_observed_at DATETIME,
  last_health_status TEXT DEFAULT '',
  raw_payload_json TEXT DEFAULT '',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_wechat_conversations_key
  ON wechat_conversations(wechat_account_id, wechat_conversation_key);

CREATE TABLE IF NOT EXISTS qianniu_conversations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  conversation_id INTEGER NOT NULL UNIQUE,
  qianniu_account_id TEXT DEFAULT '',
  qianniu_conversation_key TEXT DEFAULT '',
  display_name TEXT DEFAULT '',
  last_unread_badge INTEGER DEFAULT 0,
  last_observed_at DATETIME,
  last_health_status TEXT DEFAULT '',
  raw_payload_json TEXT DEFAULT '',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_qianniu_conversations_key
  ON qianniu_conversations(qianniu_account_id, qianniu_conversation_key);

CREATE TABLE IF NOT EXISTS wechat_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  message_id INTEGER NOT NULL UNIQUE,
  conversation_id INTEGER NOT NULL,
  wechat_account_id TEXT DEFAULT '',
  wechat_conversation_key TEXT DEFAULT '',
  wechat_display_name TEXT DEFAULT '',
  platform_message_id TEXT DEFAULT '',
  direction TEXT DEFAULT '',
  sender_role TEXT DEFAULT '',
  source_type TEXT DEFAULT '',
  confidence INTEGER DEFAULT 0,
  verification_status TEXT DEFAULT '',
  original_timestamp TEXT DEFAULT '',
  content_image_path TEXT DEFAULT '',
  raw_control_name TEXT DEFAULT '',
  raw_control_type TEXT DEFAULT '',
  role_method TEXT DEFAULT '',
  role_confidence REAL DEFAULT 0,
  bubble_rect TEXT DEFAULT '',
  message_list_rect TEXT DEFAULT '',
  observation_method TEXT DEFAULT '',
  evidence_ref TEXT DEFAULT '',
  raw_payload_json TEXT DEFAULT '',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE,
  FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_wechat_messages_conv_id ON wechat_messages(conversation_id);
CREATE INDEX IF NOT EXISTS idx_wechat_messages_platform_message_id ON wechat_messages(platform_message_id);

CREATE TABLE IF NOT EXISTS qianniu_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  message_id INTEGER NOT NULL UNIQUE,
  conversation_id INTEGER NOT NULL,
  qianniu_account_id TEXT DEFAULT '',
  qianniu_conversation_key TEXT DEFAULT '',
  qianniu_display_name TEXT DEFAULT '',
  platform_message_id TEXT DEFAULT '',
  direction TEXT DEFAULT '',
  sender_role TEXT DEFAULT '',
  raw_sender TEXT DEFAULT '',
  raw_timestamp_text TEXT DEFAULT '',
  parser_source TEXT DEFAULT '',
  source_type TEXT DEFAULT '',
  confidence INTEGER DEFAULT 0,
  verification_status TEXT DEFAULT '',
  original_timestamp TEXT DEFAULT '',
  content_image_path TEXT DEFAULT '',
  role_method TEXT DEFAULT '',
  role_confidence REAL DEFAULT 0,
  bubble_rect TEXT DEFAULT '',
  message_list_rect TEXT DEFAULT '',
  evidence_ref TEXT DEFAULT '',
  raw_payload_json TEXT DEFAULT '',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY(message_id) REFERENCES messages(id) ON DELETE CASCADE,
  FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_qianniu_messages_conv_id ON qianniu_messages(conversation_id);
CREATE INDEX IF NOT EXISTS idx_qianniu_messages_platform_message_id ON qianniu_messages(platform_message_id);

CREATE TABLE IF NOT EXISTS rpa_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  event_id TEXT UNIQUE,
  event_type TEXT NOT NULL,
  platform TEXT NOT NULL,
  account_id TEXT DEFAULT '',
  conversation_key TEXT DEFAULT '',
  occurred_at DATETIME,
  payload_json TEXT DEFAULT '{}',
  raw_event_json TEXT DEFAULT '{}',
  source_role TEXT NOT NULL DEFAULT 'python_service_truth',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_rpa_events_platform_id ON rpa_events(platform, id);

CREATE TABLE IF NOT EXISTS conversation_mutations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  platform TEXT NOT NULL,
  account_id TEXT DEFAULT '',
  conversation_key TEXT NOT NULL,
  mutation_type TEXT NOT NULL,
  effective_at DATETIME NOT NULL,
  operator TEXT DEFAULT '',
  reason TEXT DEFAULT '',
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_conversation_mutations_target
  ON conversation_mutations(platform, conversation_key, id);

CREATE TABLE IF NOT EXISTS ui_state (
  key TEXT PRIMARY KEY,
  value TEXT DEFAULT '',
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ui_conversation_state (
  platform TEXT NOT NULL,
  conversation_key TEXT NOT NULL,
  state_json TEXT NOT NULL DEFAULT '{}',
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY(platform, conversation_key)
);

CREATE TABLE IF NOT EXISTS ui_conversation_drafts (
  platform TEXT NOT NULL,
  conversation_key TEXT NOT NULL,
  content TEXT NOT NULL DEFAULT '',
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY(platform, conversation_key)
);

CREATE TABLE IF NOT EXISTS ui_compose_attachments (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  platform TEXT NOT NULL,
  conversation_key TEXT NOT NULL,
  local_path TEXT NOT NULL DEFAULT '',
  file_name TEXT DEFAULT '',
  mime_type TEXT DEFAULT '',
  size_bytes INTEGER DEFAULT 0,
  sort_order INTEGER DEFAULT 0,
  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_ui_compose_attachments_conversation
  ON ui_compose_attachments(platform, conversation_key, sort_order, id);

CREATE TABLE IF NOT EXISTS ui_window_layout (
  key TEXT PRIMARY KEY,
  value_json TEXT NOT NULL DEFAULT '{}',
  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE VIEW IF NOT EXISTS active_conversations AS
SELECT *
FROM conversations
WHERE deleted_at IS NULL OR deleted_at = '';

CREATE VIEW IF NOT EXISTS conversation_messages AS
SELECT m.*
FROM messages m
JOIN conversations c ON c.id = m.conversation_id
WHERE (c.deleted_at IS NULL OR c.deleted_at = '')
  AND (m.deleted_at IS NULL OR m.deleted_at = '');

CREATE VIEW IF NOT EXISTS conversation_last_messages AS
SELECT c.id AS conversation_id,
       c.platform,
       c.platform_conversation_id,
       c.last_message,
       c.last_time,
       c.updated_at
FROM conversations c
WHERE c.deleted_at IS NULL OR c.deleted_at = '';
