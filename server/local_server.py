# -*- coding: utf-8 -*-
"""local_server.py —— 本地 / 自建服务器的排行榜后端（等价于 worker.js）

用途有两个：
  1. 本地开发：不想装 wrangler，直接 python tools/serve.py 起前端 + 后端，
     游戏里把 MAZE_API_BASE 指向 http://127.0.0.1:8787 就能联调
  2. 自建部署：扔到任何有 Python 3 的小机器上就能当全网榜服务器

只依赖标准库（http.server + sqlite3），校验规则和 worker.js 一一对应 ——
两边行为必须一致，改一边记得改另一边。

用法：
    python server/local_server.py --port 8787 --db server/leaderboard.db
"""
import argparse
import hashlib
import hmac
import json
import os
import re
import sqlite3
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# 必须和 src/Leaderboard.cpp / server/worker.js 一致
SIGN_KEY = b'maze-web-2f8a1c9d4e6b7a30-change-me-on-deploy'

LIMITS = {
    'easy':   {'maxScore':    2000, 'minElapsed':   4},
    'normal': {'maxScore':    5000, 'minElapsed':   6},
    'hard':   {'maxScore':   12000, 'minElapsed':  10},
    'hell':   {'maxScore':   12000, 'minElapsed':  10},
    'holy':   {'maxScore':   12000, 'minElapsed':  10},
    'hunger': {'maxScore':   30000, 'minElapsed':  10},
    'rogue':  {'maxScore':  120000, 'minElapsed':  60},
}
MAX_ELAPSED = 6 * 3600
MAX_NAME_CODEPOINTS = 12
RATE_LIMIT_PER_HOUR = 30
SUSPICIOUS_FACTOR = 3

DIFFICULTIES = list(LIMITS.keys())
BOARDS = ['score', 'time']
MODES = ['normal', 'nailong']

SCHEMA = '''
CREATE TABLE IF NOT EXISTS records (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL,
    difficulty TEXT    NOT NULL,
    board      TEXT    NOT NULL,
    mode       TEXT    NOT NULL,
    score      INTEGER NOT NULL,
    elapsed    REAL    NOT NULL,
    created_at TEXT    NOT NULL,
    ip_hash    TEXT,
    flagged    INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS ux_records_name_diff_board
    ON records(name, difficulty, board);
CREATE INDEX IF NOT EXISTS ix_records_board
    ON records(difficulty, board, score DESC, elapsed ASC);
'''

CTRL = re.compile(r'[\u0000-\u001f\u007f]')


def sanitize_name(raw):
    if not isinstance(raw, str):
        return None
    cleaned = CTRL.sub('', raw).strip()
    if not cleaned or len(cleaned) > MAX_NAME_CODEPOINTS:
        return None
    return cleaned


def sign(canonical: str) -> str:
    return hmac.new(SIGN_KEY, canonical.encode('utf-8'), hashlib.sha256).hexdigest()


def elapsed_string(v) -> str:
    return '%.3f' % float(v)


class Handler(BaseHTTPRequestHandler):
    db_path = None
    server_version = 'maze-leaderboard/1.0'

    # ---------------------------------------------------------------- helpers
    def _send(self, body, status=200):
        payload = json.dumps(body, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'content-type')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _conn(self):
        conn = sqlite3.connect(self.db_path, timeout=10)
        conn.row_factory = sqlite3.Row
        return conn

    def log_message(self, fmt, *args):          # 少刷屏
        sys.stderr.write('[lb] %s\n' % (fmt % args))

    # ------------------------------------------------------------------ HTTP
    def do_OPTIONS(self):
        self._send({}, 204)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == '/api/health':
            return self._send({'ok': True, 'service': 'maze-leaderboard-python'})

        if url.path == '/api/board':
            q = parse_qs(url.query)
            difficulty = (q.get('difficulty') or [''])[0]
            board = (q.get('board') or ['score'])[0]
            try:
                limit = int((q.get('limit') or ['10'])[0])
            except ValueError:
                limit = 10
            limit = max(1, min(50, limit))

            if difficulty not in DIFFICULTIES:
                return self._send({'ok': False, 'error': 'bad difficulty'}, 400)
            if board not in BOARDS:
                return self._send({'ok': False, 'error': 'bad board'}, 400)

            order = 'elapsed ASC, score DESC' if board == 'time' else 'score DESC, elapsed ASC'
            conn = self._conn()
            try:
                rows = conn.execute(
                    'SELECT name, difficulty, board, mode, score, elapsed, created_at, flagged '
                    'FROM records WHERE difficulty = ? AND board = ? '
                    'ORDER BY ' + order + ' LIMIT ?',
                    (difficulty, board, limit)).fetchall()
            finally:
                conn.close()

            return self._send({
                'ok': True, 'difficulty': difficulty, 'board': board,
                'rows': [{
                    'name': r['name'], 'difficulty': r['difficulty'], 'board': r['board'],
                    'mode': r['mode'], 'score': r['score'], 'elapsed': r['elapsed'],
                    'date': (r['created_at'] or '').replace('T', ' ')[:19],
                    'flagged': bool(r['flagged']),
                } for r in rows],
            })

        return self._send({'ok': False, 'error': 'not found'}, 404)

    def do_POST(self):
        url = urlparse(self.path)
        if url.path != '/api/score':
            return self._send({'ok': False, 'error': 'not found'}, 404)

        try:
            length = int(self.headers.get('Content-Length') or 0)
            body = json.loads(self.rfile.read(length).decode('utf-8'))
        except Exception:
            return self._send({'ok': False, 'error': 'bad json'}, 400)

        name = sanitize_name(body.get('name'))
        difficulty = str(body.get('difficulty') or '')
        board = str(body.get('board') or '')
        mode = str(body.get('mode') or '')
        score = body.get('score')
        elapsed = body.get('elapsed')
        sig = str(body.get('sig') or '')

        if not name:
            return self._send({'ok': False, 'error': 'bad name'}, 400)
        if difficulty not in DIFFICULTIES:
            return self._send({'ok': False, 'error': 'bad difficulty'}, 400)
        if board not in BOARDS:
            return self._send({'ok': False, 'error': 'bad board'}, 400)
        if mode not in MODES:
            return self._send({'ok': False, 'error': 'bad mode'}, 400)
        if not isinstance(score, int) or isinstance(score, bool):
            return self._send({'ok': False, 'error': 'bad score'}, 400)
        if not isinstance(elapsed, (int, float)):
            return self._send({'ok': False, 'error': 'bad elapsed'}, 400)

        canonical = '|'.join([name, difficulty, board, mode, str(score), elapsed_string(elapsed)])
        if not hmac.compare_digest(sign(canonical), sig):
            return self._send({'ok': False, 'error': 'bad signature'}, 403)

        lim = LIMITS[difficulty]
        if score < 0 or score > lim['maxScore']:
            return self._send({'ok': False, 'error': 'score out of range'}, 422)
        if elapsed < lim['minElapsed'] or elapsed > MAX_ELAPSED:
            return self._send({'ok': False, 'error': 'elapsed out of range'}, 422)

        ip = self.client_address[0]
        ip_hash = hashlib.sha256(ip.encode('utf-8')).hexdigest()[:32]
        now = time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime())
        since = time.strftime('%Y-%m-%dT%H:%M:%S', time.gmtime(time.time() - 3600))

        conn = self._conn()
        try:
            n = conn.execute('SELECT COUNT(*) AS n FROM records WHERE ip_hash = ? AND created_at >= ?',
                             (ip_hash, since)).fetchone()['n']
            if n >= RATE_LIMIT_PER_HOUR:
                return self._send({'ok': False, 'error': 'rate limited'}, 429)

            top = conn.execute(
                'SELECT %s AS v FROM records WHERE difficulty = ? AND board = ?'
                % ('MIN(elapsed)' if board == 'time' else 'MAX(score)'),
                (difficulty, board)).fetchone()
            flagged = 0
            if top and top['v']:
                ratio = (top['v'] / float(elapsed)) if board == 'time' else (score / float(top['v']))
                if ratio >= SUSPICIOUS_FACTOR:
                    flagged = 1

            conn.execute(
                'INSERT INTO records (name, difficulty, board, mode, score, elapsed, created_at, ip_hash, flagged) '
                'VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) '
                'ON CONFLICT(name, difficulty, board) DO UPDATE SET '
                '  mode = excluded.mode, score = excluded.score, elapsed = excluded.elapsed, '
                '  created_at = excluded.created_at, ip_hash = excluded.ip_hash, flagged = excluded.flagged '
                'WHERE (excluded.score > records.score) '
                '   OR (excluded.score = records.score AND excluded.elapsed < records.elapsed)',
                (name, difficulty, board, mode, score, float(elapsed), now, ip_hash, flagged))
            conn.commit()
        finally:
            conn.close()

        return self._send({'ok': True, 'flagged': flagged == 1})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=8787)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--db', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'leaderboard.db'))
    args = ap.parse_args()

    Handler.db_path = args.db
    conn = sqlite3.connect(args.db)
    conn.executescript(SCHEMA)
    conn.commit()
    conn.close()

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    print('leaderboard listening on http://%s:%d  (db=%s)' % (args.host, args.port, args.db))
    print('把游戏里的 MAZE_API_BASE 指向这个地址即可（同域部署时留空）')
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()