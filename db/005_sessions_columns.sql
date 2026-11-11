-- sessions: column migrations
-- Ported verbatim from the old hard-coded `session_migrations[]` array in
-- init_remote_db().
--
-- NOTE: this assumes the `sessions` table itself already exists. The old
-- hard-coded init never created that table either (search the codebase turns
-- up no `CREATE TABLE sessions` anywhere) -- so this file preserves existing
-- behavior exactly rather than introducing a new table that wasn't there
-- before. If `sessions` is expected to be created fresh on a brand new
-- install, that CREATE TABLE statement is missing and would need its own
-- earlier-numbered file (e.g. 000_create_sessions.sql).
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS load_from_session TINYINT(1) DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS debug_mode TINYINT(1) DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS tee_enabled TINYINT(1) DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS autoreply_enabled TINYINT(1) DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS auto_execute_enabled TINYINT(1) DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS ratelimit_enabled TINYINT(1) DEFAULT 1;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS smart_cache_enabled TINYINT(1) DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS noise_filter_enabled TINYINT(1) DEFAULT 1;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS xml_payload_tagging TINYINT(1) DEFAULT 1;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS write_to_global TINYINT(1) DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS read_from_global TINYINT(1) DEFAULT 1;
