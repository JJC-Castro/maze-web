-- ===========================================================================
--  schema.sql —— 排行榜数据库（Cloudflare D1 / SQLite）
--  应用方式：  wrangler d1 execute maze-leaderboard --file=server/schema.sql --remote
-- ===========================================================================

CREATE TABLE IF NOT EXISTS records (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL,              -- 用户名（UTF-8，最多 12 个码点）
    difficulty TEXT    NOT NULL,              -- easy/normal/hard/hell/holy/hunger/rogue
    board      TEXT    NOT NULL,              -- score（分数榜）/ time（用时榜）
    mode       TEXT    NOT NULL,              -- normal（正常模式）/ nailong（奶龙模式）
    score      INTEGER NOT NULL,
    elapsed    REAL    NOT NULL,              -- 秒，3 位小数
    created_at TEXT    NOT NULL,              -- ISO8601 UTC
    ip_hash    TEXT,                          -- 提交方 IP 的 SHA-256 前 32 位（不存原始 IP）
    flagged    INTEGER NOT NULL DEFAULT 0     -- 1 = 通过校验但高得离谱，留给你人工复核
);

-- 同名 + 同难度 + 同榜只保留一条：客户端提交时靠这个唯一键做 UPSERT。
-- 注意 key 里**没有** mode —— 正常模式和奶龙模式在同一张榜上比，和游戏内一致。
CREATE UNIQUE INDEX IF NOT EXISTS ux_records_name_diff_board
    ON records(name, difficulty, board);

-- 榜单查询走这个索引：分数降序 / 用时升序都是前缀友好的
CREATE INDEX IF NOT EXISTS ix_records_board
    ON records(difficulty, board, score DESC, elapsed ASC);

-- 限流用：按 IP 哈希 + 时间查最近一小时的提交数
CREATE INDEX IF NOT EXISTS ix_records_ratelimit
    ON records(ip_hash, created_at);