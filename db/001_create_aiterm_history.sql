-- aiterm_history: base table
-- Ported verbatim from the old hard-coded step in init_remote_db().
-- Columns are added in 002_aiterm_history_columns.sql.
CREATE TABLE IF NOT EXISTS aiterm_history (
    id INT AUTO_INCREMENT PRIMARY KEY
);
