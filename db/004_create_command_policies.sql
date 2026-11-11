-- command_policy table
--
-- FIX (1): the previous version of this file created `command_policies`
-- with columns (command, type, risk_level).  policy_dao.c -- the only code
-- that touches this table -- reads and writes `command_policy` with columns
-- (command_name, policy_type, risk_level), so a fresh install built from
-- the old script had no table the code could use.  This file now matches
-- the code.  risk_level is an INT because the code stores and parses it as
-- one (%d / atoi).
--
-- Installs that already ran the old script have an unused, empty
-- `command_policies` table; it is harmless and can be removed by hand with
-- `DROP TABLE IF EXISTS command_policies;`.
--
-- (The earlier fix is kept: ON UPDATE CURRENT_TIMESTAMP, not the invalid
-- `ON DUPLICATE KEY UPDATE` inside a column list.)
CREATE TABLE IF NOT EXISTS command_policy (
    command_name VARCHAR(256) PRIMARY KEY,
    policy_type  VARCHAR(32) NOT NULL,
    risk_level   INT NOT NULL DEFAULT 0,
    updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
