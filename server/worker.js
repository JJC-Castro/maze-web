// ===========================================================================
//  worker.js —— 「3D 迷宫」全网排行榜后端（Cloudflare Worker + D1）
//
//  设计目标（对应方案 2：服务端合理性校验，而不是客户端自说自话）：
//    1. 验签   —— 客户端用编译进 wasm 的密钥算 HMAC-SHA256，这里用 Web Crypto 复核
//    2. 范围   —— 分数 / 用时 / 难度 / 模式 / 用户名长度全部卡死，超界直接拒
//    3. 限流   —— 同一 IP 每小时最多 N 次提交
//    4. 去重   —— (用户名, 难度, 榜单) 唯一，只留更好的那一条
//    5. 存疑   —— 通过校验但高得离谱的成绩打标记，不删，方便你回头人工看
//
//  ⚠ 必须说清楚的边界：
//    密钥编译在 wasm 里，肯下功夫的人能挖出来。这套东西把作弊门槛从
//    「打开 DevTools 改个数字」抬高到「先逆向前端再写脚本」，但**不是**绝对安全。
//    真要杜绝作弊，只能上服务端重放验证（拿 seed 重跑一遍迷宫），那是另一个量级。
//
//  接口：
//    GET  /api/health
//    GET  /api/board?difficulty=easy&board=score&limit=10
//    POST /api/score   {name,difficulty,board,mode,score,elapsed,sig}
// ===========================================================================

// 必须和 src/Leaderboard.cpp 里的 kSignKey 完全一致
const SIGN_KEY = 'maze-web-2f8a1c9d4e6b7a30-change-me-on-deploy';

// ---------------------------------------------------------------------------
//  合理性上限（方案 2 的核心：不用重放也能挡掉绝大多数离谱数据）
//    maxScore  ：该难度理论上能拿到的最高分（宝物全吃 + 时间奖励），调成你自己的
//    minElapsed：该难度理论最短通关时间，比这快一定是改的
// ---------------------------------------------------------------------------
const LIMITS = {
  easy:   { maxScore:    2000, minElapsed:   4 },
  normal: { maxScore:    5000, minElapsed:   6 },
  hard:   { maxScore:   12000, minElapsed:  10 },
  hell:   { maxScore:   12000, minElapsed:  10 },
  holy:   { maxScore:   12000, minElapsed:  10 },
  hunger: { maxScore:   30000, minElapsed:  10 },
  rogue:  { maxScore:  120000, minElapsed:  60 },   // 五层连打，下限拉高
};
const MAX_ELAPSED = 6 * 3600;        // 超过 6 小时的成绩一律当异常
const MAX_NAME_CODEPOINTS = 12;      // 和游戏里 RANK_NAME_MAX_CHARS 保持一致
const RATE_LIMIT_PER_HOUR = 30;
const DEDUP_WINDOW_SECONDS = 60;     // 同样的成绩 60 秒内重复提交只算一次

const DIFFICULTIES = Object.keys(LIMITS);
const BOARDS = ['score', 'time'];
const MODES = ['normal', 'nailong'];

// 存疑阈值：超过当前榜首这个倍数就先打标记（不拒，可能真是大佬）
const SUSPICIOUS_FACTOR = 3;

// ---------------------------------------------------------------------------
//  小工具
// ---------------------------------------------------------------------------
const enc = new TextEncoder();

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'access-control-allow-origin': '*',
      'cache-control': 'no-store',
    },
  });
}

async function hmacHex(message) {
  const key = await crypto.subtle.importKey(
    'raw', enc.encode(SIGN_KEY), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const sig = await crypto.subtle.sign('HMAC', key, enc.encode(message));
  return [...new Uint8Array(sig)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// 定长比较，避免用 === 比签名时的时序侧信道
function safeEqual(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string') return false;
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function sanitizeName(raw) {
  if (typeof raw !== 'string') return null;
  // 去掉控制字符和首尾空白；按码点计数（中文一个字算一个，和游戏里一致）
  const cleaned = raw.replace(/[\u0000-\u001F\u007F]/g, '').trim();
  const codepoints = [...cleaned];
  if (codepoints.length === 0) return null;
  if (codepoints.length > MAX_NAME_CODEPOINTS) return null;
  return cleaned;
}

async function sha256Hex(text) {
  const buf = await crypto.subtle.digest('SHA-256', enc.encode(text));
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// 用时统一 3 位小数 —— 必须和客户端拼串时的格式一模一样，否则验签必挂
function elapsedString(v) {
  return Number(v).toFixed(3);
}

// ---------------------------------------------------------------------------
//  POST /api/score
// ---------------------------------------------------------------------------
async function handleScore(request, env) {
  let body;
  try {
    body = await request.json();
  } catch {
    return json({ ok: false, error: 'bad json' }, 400);
  }

  const name = sanitizeName(body.name);
  const difficulty = String(body.difficulty || '');
  const board = String(body.board || '');
  const mode = String(body.mode || '');
  const score = Number(body.score);
  const elapsed = Number(body.elapsed);
  const sig = String(body.sig || '');

  // ---- 1. 形状 ----
  if (!name) return json({ ok: false, error: 'bad name' }, 400);
  if (!DIFFICULTIES.includes(difficulty)) return json({ ok: false, error: 'bad difficulty' }, 400);
  if (!BOARDS.includes(board)) return json({ ok: false, error: 'bad board' }, 400);
  if (!MODES.includes(mode)) return json({ ok: false, error: 'bad mode' }, 400);
  if (!Number.isFinite(score) || !Number.isInteger(score)) return json({ ok: false, error: 'bad score' }, 400);
  if (!Number.isFinite(elapsed)) return json({ ok: false, error: 'bad elapsed' }, 400);

  // ---- 2. 验签 ----
  const canonical = [name, difficulty, board, mode, String(score), elapsedString(elapsed)].join('|');
  const expected = await hmacHex(canonical);
  if (!safeEqual(expected, sig)) {
    return json({ ok: false, error: 'bad signature' }, 403);
  }

  // ---- 3. 范围 / 合理性 ----
  const lim = LIMITS[difficulty];
  if (score < 0 || score > lim.maxScore) {
    return json({ ok: false, error: 'score out of range' }, 422);
  }
  if (elapsed < lim.minElapsed || elapsed > MAX_ELAPSED) {
    return json({ ok: false, error: 'elapsed out of range' }, 422);
  }

  // ---- 4. 限流（按 IP 哈希，不存原始 IP）----
  const ip = request.headers.get('CF-Connecting-IP') || '0.0.0.0';
  const ipHash = (await sha256Hex(ip)).slice(0, 32);
  const since = new Date(Date.now() - 3600 * 1000).toISOString();
  const recent = await env.DB.prepare(
    'SELECT COUNT(*) AS n FROM records WHERE ip_hash = ? AND created_at >= ?'
  ).bind(ipHash, since).first();
  if (recent && recent.n >= RATE_LIMIT_PER_HOUR) {
    return json({ ok: false, error: 'rate limited' }, 429);
  }

  // ---- 5. 重复提交（同一秒内双击 / 重试）----
  const dupSince = new Date(Date.now() - DEDUP_WINDOW_SECONDS * 1000).toISOString();
  const dup = await env.DB.prepare(
    `SELECT id FROM records
      WHERE name = ? AND difficulty = ? AND board = ?
        AND score = ? AND elapsed = ? AND created_at >= ?`
  ).bind(name, difficulty, board, score, elapsed, dupSince).first();
  if (dup) return json({ ok: true, deduped: true });

  // ---- 6. 存疑标记：比当前榜首高出一个量级 ----
  const top = await env.DB.prepare(
    `SELECT ${board === 'time' ? 'MIN(elapsed)' : 'MAX(score)'} AS v
       FROM records WHERE difficulty = ? AND board = ?`
  ).bind(difficulty, board).first();
  let flagged = 0;
  if (top && top.v != null && top.v > 0) {
    const ratio = board === 'time' ? top.v / elapsed : score / top.v;
    if (ratio >= SUSPICIOUS_FACTOR) flagged = 1;
  }

  // ---- 7. 写库：同名 + 同难度 + 同榜只留最好的一次 ----
  const createdAt = new Date().toISOString();
  await env.DB.prepare(
    `INSERT INTO records (name, difficulty, board, mode, score, elapsed, created_at, ip_hash, flagged)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
     ON CONFLICT(name, difficulty, board) DO UPDATE SET
        mode       = excluded.mode,
        score      = excluded.score,
        elapsed    = excluded.elapsed,
        created_at = excluded.created_at,
        ip_hash    = excluded.ip_hash,
        flagged    = excluded.flagged
     WHERE (excluded.score > records.score)
        OR (excluded.score = records.score AND excluded.elapsed < records.elapsed)`
  ).bind(name, difficulty, board, mode, score, elapsed, createdAt, ipHash, flagged).run();

  return json({ ok: true, flagged: flagged === 1 });
}

// ---------------------------------------------------------------------------
//  GET /api/board
// ---------------------------------------------------------------------------
async function handleBoard(url, env) {
  const difficulty = url.searchParams.get('difficulty') || '';
  const board = url.searchParams.get('board') || 'score';
  const limit = Math.min(50, Math.max(1, parseInt(url.searchParams.get('limit') || '10', 10) || 10));

  if (!DIFFICULTIES.includes(difficulty)) return json({ ok: false, error: 'bad difficulty' }, 400);
  if (!BOARDS.includes(board)) return json({ ok: false, error: 'bad board' }, 400);

  // 排序：分数榜按分数降序、同分用时升序；用时榜按用时升序
  const order = board === 'time'
    ? 'elapsed ASC, score DESC'
    : 'score DESC, elapsed ASC';

  const { results } = await env.DB.prepare(
    `SELECT name, difficulty, board, mode, score, elapsed, created_at, flagged
       FROM records
      WHERE difficulty = ? AND board = ?
      ORDER BY ${order}
      LIMIT ?`
  ).bind(difficulty, board, limit).all();

  const rows = (results || []).map((r) => ({
    name: r.name,
    difficulty: r.difficulty,
    board: r.board,
    mode: r.mode,
    score: r.score,
    elapsed: r.elapsed,
    date: (r.created_at || '').replace('T', ' ').replace('Z', '').slice(0, 19),
    flagged: !!r.flagged,
  }));

  return json({ ok: true, difficulty, board, rows });
}

// ---------------------------------------------------------------------------
//  入口
// ---------------------------------------------------------------------------
export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === 'OPTIONS') {
      return new Response(null, {
        status: 204,
        headers: {
          'access-control-allow-origin': '*',
          'access-control-allow-methods': 'GET, POST, OPTIONS',
          'access-control-allow-headers': 'content-type',
          'access-control-max-age': '86400',
        },
      });
    }

    try {
      if (url.pathname === '/api/health') {
        return json({ ok: true, service: 'maze-leaderboard', ts: new Date().toISOString() });
      }
      if (url.pathname === '/api/board' && request.method === 'GET') {
        return handleBoard(url, env);
      }
      if (url.pathname === '/api/score' && request.method === 'POST') {
        return handleScore(request, env);
      }
      return json({ ok: false, error: 'not found' }, 404);
    } catch (err) {
      return json({ ok: false, error: 'internal', detail: String(err && err.message || err) }, 500);
    }
  },
};