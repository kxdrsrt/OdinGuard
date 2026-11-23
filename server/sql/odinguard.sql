-- OdinGuard server tables. Created automatically at map-server start (CREATE TABLE IF NOT EXISTS),
-- this file is the reference copy.

CREATE TABLE IF NOT EXISTS `og_client` (
  `unique_id`   CHAR(64)     NOT NULL,
  `account_id`  INT UNSIGNED NOT NULL DEFAULT 0,
  `first_seen`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_seen`   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_ip`     VARCHAR(45)  NOT NULL DEFAULT '',
  `build`       VARCHAR(32)  NOT NULL DEFAULT '',
  PRIMARY KEY (`unique_id`, `account_id`),
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB;

-- hashed (salted) hardware components per client, used for fuzzy matching (k of n)
CREATE TABLE IF NOT EXISTS `og_component` (
  `unique_id` CHAR(64)    NOT NULL,
  `name`      VARCHAR(24) NOT NULL,
  `hash`      CHAR(64)    NOT NULL,
  PRIMARY KEY (`unique_id`, `name`),
  KEY `hash` (`hash`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `og_ban` (
  `id`         INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `unique_id`  CHAR(64)     NULL,
  `account_id` INT UNSIGNED  NULL,
  `reason`     VARCHAR(255) NOT NULL DEFAULT '',
  `banned_by`  VARCHAR(24)  NOT NULL DEFAULT '',
  `created`    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `expires`    DATETIME     NULL,
  PRIMARY KEY (`id`),
  KEY `unique_id` (`unique_id`),
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB;

-- allowed client builds: one row per (build, file)
CREATE TABLE IF NOT EXISTS `og_manifest` (
  `build`  VARCHAR(32)  NOT NULL,
  `file`   VARCHAR(128) NOT NULL,
  `sha256` CHAR(64)     NOT NULL,
  `active` TINYINT(1)   NOT NULL DEFAULT 1,
  PRIMARY KEY (`build`, `file`)
) ENGINE=InnoDB;

-- allowed in-memory code hash of the exe per build
CREATE TABLE IF NOT EXISTS `og_exe_code` (
  `build` VARCHAR(32) NOT NULL,
  `hash`  CHAR(64)    NOT NULL,
  PRIMARY KEY (`build`, `hash`)
) ENGINE=InnoDB;

-- detection list delivered to clients (process name, window class, module name, ...)
CREATE TABLE IF NOT EXISTS `og_signature` (
  `id`      INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `kind`    ENUM('process','window','module') NOT NULL,
  `pattern` VARCHAR(128) NOT NULL,
  `label`   VARCHAR(64)  NOT NULL DEFAULT '',
  `action`  ENUM('log','kick','ban') NOT NULL DEFAULT 'kick',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `og_event` (
  `id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `time`       DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `unique_id`  CHAR(64)     NOT NULL DEFAULT '',
  `account_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `type`       VARCHAR(24)  NOT NULL,
  `detail`     TEXT         NULL,
  PRIMARY KEY (`id`),
  KEY `unique_id` (`unique_id`),
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB;
