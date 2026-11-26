-- OdinGuard server tables. The login- and map-server create them at start (CREATE TABLE IF NOT EXISTS,
-- src/common/odinguard.cpp og_sql_ddl); this file is the reference copy for manual setups and the web panel.
-- Signatures, allowed exe code hashes, auras, colours and LGP skills are in conf/odinguard_athena.conf.

-- every machine (unique id) an account logged in from
CREATE TABLE IF NOT EXISTS `og_client` (
  `unique_id`  CHAR(64)     NOT NULL,
  `account_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `first_seen` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_seen`  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_ip`    VARCHAR(45)  NOT NULL DEFAULT '',
  `build`      VARCHAR(32)  NOT NULL DEFAULT '',
  `exe_code`   CHAR(64)     NOT NULL DEFAULT '',
  PRIMARY KEY (`unique_id`, `account_id`),
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB;

-- salted hashes of the hardware components per machine, for fuzzy ban matching (k of n)
CREATE TABLE IF NOT EXISTS `og_component` (
  `unique_id` CHAR(64)    NOT NULL,
  `name`      VARCHAR(16) NOT NULL,
  `hash`      CHAR(64)    NOT NULL,
  PRIMARY KEY (`unique_id`, `name`),
  KEY `hash` (`hash`)
) ENGINE=InnoDB;

-- machine bans (@ogban / @ogunban); expires NULL = permanent
CREATE TABLE IF NOT EXISTS `og_ban` (
  `id`        INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `unique_id` CHAR(64)     NOT NULL,
  `reason`    VARCHAR(255) NOT NULL DEFAULT '',
  `banned_by` VARCHAR(24)  NOT NULL DEFAULT '',
  `created`   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `expires`   DATETIME     NULL,
  PRIMARY KEY (`id`),
  KEY `unique_id` (`unique_id`)
) ENGINE=InnoDB;

-- detections and actions (tool found, alive-timeout, exe code mismatch, gm-ban, ...)
CREATE TABLE IF NOT EXISTS `og_event` (
  `id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `time`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `unique_id`  CHAR(64)        NOT NULL DEFAULT '',
  `account_id` INT UNSIGNED    NOT NULL DEFAULT 0,
  `char_name`  VARCHAR(24)     NOT NULL DEFAULT '',
  `type`       VARCHAR(64)     NOT NULL,
  `detail`     VARCHAR(255)    NOT NULL DEFAULT '',
  `action`     VARCHAR(8)      NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `unique_id` (`unique_id`),
  KEY `account_id` (`account_id`)
) ENGINE=InnoDB;

-- flags with points from detections; the ban wave turns open flags of an account into bans
CREATE TABLE IF NOT EXISTS `og_flag` (
  `id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `time`       DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `unique_id`  CHAR(64)        NOT NULL DEFAULT '',
  `account_id` INT UNSIGNED    NOT NULL DEFAULT 0,
  `char_name`  VARCHAR(24)     NOT NULL DEFAULT '',
  `rule`       VARCHAR(64)     NOT NULL,
  `score`      INT             NOT NULL DEFAULT 0,
  `ban`        TINYINT(1)      NOT NULL DEFAULT 0,
  `detail`     VARCHAR(255)    NOT NULL DEFAULT '',
  `state`      ENUM('open','banned','cleared') NOT NULL DEFAULT 'open',
  PRIMARY KEY (`id`),
  KEY `account_id` (`account_id`, `state`),
  KEY `unique_id` (`unique_id`)
) ENGINE=InnoDB;

-- commands from a web panel for players online right now (kick after a web ban, message); the map-server that has the
-- player takes the row with a conditional UPDATE, rows nobody takes expire after og_command_expire_s
CREATE TABLE IF NOT EXISTS `og_command` (
  `id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created`    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `account_id` INT UNSIGNED    NOT NULL DEFAULT 0,
  `unique_id`  CHAR(64)        NOT NULL DEFAULT '',
  `command`    ENUM('kick','message') NOT NULL,
  `arg`        VARCHAR(255)    NOT NULL DEFAULT '',
  `issued_by`  VARCHAR(24)     NOT NULL DEFAULT '',
  `state`      ENUM('open','done','expired') NOT NULL DEFAULT 'open',
  `done_at`    DATETIME        NULL,
  PRIMARY KEY (`id`),
  KEY `state` (`state`, `id`)
) ENGINE=InnoDB;

-- account lock: an account with rows here logs in only from these machines (web panel or @oglock; og_account_lock)
CREATE TABLE IF NOT EXISTS `og_account_lock` (
  `account_id` INT UNSIGNED NOT NULL,
  `unique_id`  CHAR(64)     NOT NULL,
  `created`    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `note`       VARCHAR(64)  NOT NULL DEFAULT '',
  PRIMARY KEY (`account_id`, `unique_id`)
) ENGINE=InnoDB;

-- one token per map login for the crash report upload (og_crash_upload_url); the panel checks the token
-- (account, machine) and sets `used`. Tokens older than 7 days are removed by the map-server.
CREATE TABLE IF NOT EXISTS `og_crash_token` (
  `token`      CHAR(64)     NOT NULL,
  `account_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `unique_id`  CHAR(64)     NOT NULL DEFAULT '',
  `created`    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `used`       DATETIME     NULL,
  PRIMARY KEY (`token`),
  KEY `created` (`created`)
) ENGINE=InnoDB;
