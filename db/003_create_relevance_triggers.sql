-- relevance_triggers table
--
-- FIX: the old hard-coded version of this table put `ON DUPLICATE KEY UPDATE`
-- inside the column list, which is not valid CREATE TABLE syntax in
-- MySQL/MariaDB (that clause only applies to INSERT / CREATE TABLE ... SELECT
-- statements). Because the old init code never checked mysql_query()'s return
-- value, this statement was almost certainly failing silently on every run.
-- Replaced with `ON UPDATE CURRENT_TIMESTAMP`, the actual column attribute
-- that keeps last_used fresh whenever a row is updated.
CREATE TABLE IF NOT EXISTS relevance_triggers (
    id INT AUTO_INCREMENT PRIMARY KEY,
    keyword VARCHAR(50) UNIQUE,
    hit_count INT DEFAULT 1,
    last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
