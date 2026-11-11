-- aiterm_history: column migrations
-- Ported verbatim from the old hard-coded `migrations[]` array in init_remote_db().
ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS role VARCHAR(20);
ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS content TEXT;
ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS is_tee TINYINT(1) DEFAULT 0;
ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS session_uuid VARCHAR(36);
ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS sequence_id INT DEFAULT 0;
ALTER TABLE aiterm_history ADD COLUMN IF NOT EXISTS created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP;
