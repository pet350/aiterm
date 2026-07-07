-- phpMyAdmin SQL Dump
-- version 5.2.1deb3
-- https://www.phpmyadmin.net/
--
-- Host: naboo.gigaware.lan:3306
-- Generation Time: Jul 07, 2026 at 07:36 AM
-- Server version: 11.8.3-MariaDB-0+deb13u1 from Debian-log
-- PHP Version: 8.5.5


--
-- Database: `aiterm_db`
--
CREATE DATABASE IF NOT EXISTS `aiterm_db` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_uca1400_ai_ci;
USE `aiterm_db`;

-- --------------------------------------------------------

--
-- Table structure for table `aiterm_history`
--

DROP TABLE IF EXISTS `aiterm_history`;
CREATE TABLE IF NOT EXISTS `aiterm_history` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `role` varchar(20) DEFAULT NULL,
  `content` text DEFAULT NULL,
  `is_tee` tinyint(1) DEFAULT 0,
  `session_uuid` varchar(36) DEFAULT NULL,
  `created_at` timestamp NULL DEFAULT current_timestamp(),
  `sequence_id` int(11) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_content` (`content`(255))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;

-- --------------------------------------------------------

--
-- Table structure for table `command_policy`
--

DROP TABLE IF EXISTS `command_policy`;
CREATE TABLE IF NOT EXISTS `command_policy` (
  `command_name` varchar(64) NOT NULL,
  `policy_type` enum('ALLOW','BLOCK','APPROVE') NOT NULL,
  `risk_level` int(11) DEFAULT 0,
  `description` text DEFAULT NULL,
  PRIMARY KEY (`command_name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;

-- --------------------------------------------------------

--
-- Table structure for table `noise_filters`
--

DROP TABLE IF EXISTS `noise_filters`;
CREATE TABLE IF NOT EXISTS `noise_filters` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `pattern` text NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT current_timestamp(),
  `uuid` uuid NOT NULL DEFAULT uuid(),
  PRIMARY KEY (`id`),
  UNIQUE KEY `pattern` (`pattern`(255))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;

-- --------------------------------------------------------

--
-- Table structure for table `relevance_triggers`
--

DROP TABLE IF EXISTS `relevance_triggers`;
CREATE TABLE IF NOT EXISTS `relevance_triggers` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `keyword` varchar(50) DEFAULT NULL,
  `category` varchar(50) DEFAULT NULL,
  `weight` int(11) DEFAULT 1,
  `hit_count` int(11) DEFAULT 1,
  `last_used` timestamp NULL DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  PRIMARY KEY (`id`),
  UNIQUE KEY `keyword` (`keyword`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;

-- --------------------------------------------------------

--
-- Table structure for table `sessions`
--

DROP TABLE IF EXISTS `sessions`;
CREATE TABLE IF NOT EXISTS `sessions` (
  `uuid` varchar(36) NOT NULL,
  `created_at` timestamp NULL DEFAULT current_timestamp(),
  `description` text DEFAULT NULL,
  `is_active` tinyint(1) DEFAULT 0,
  `is_default` tinyint(1) DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;

