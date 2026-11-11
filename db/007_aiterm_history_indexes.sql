-- aiterm_history: indexes
--
-- The table had only its primary key, yet every AI request filters it by
-- session_uuid and sorts it.  These scripts run on every startup, so each
-- statement must be idempotent (CREATE INDEX IF NOT EXISTS, MariaDB 10.1.4+).
--
-- History loaders:   WHERE session_uuid ... ORDER BY created_at DESC, id DESC LIMIT 100
CREATE INDEX IF NOT EXISTS idx_history_session_created
    ON aiterm_history (session_uuid, created_at, id);

-- OpenAI-path loader: ... AND sequence_id > N ... ORDER BY sequence_id DESC, id DESC
CREATE INDEX IF NOT EXISTS idx_history_session_seq
    ON aiterm_history (session_uuid, sequence_id, id);

-- Tee snapshot lookup: session_uuid = X AND is_tee = 1 AND role = 'terminal' ORDER BY id DESC LIMIT 1
CREATE INDEX IF NOT EXISTS idx_history_session_tee_role
    ON aiterm_history (session_uuid, is_tee, role, id);
