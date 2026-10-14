# -*- coding: utf-8 -*-
"""排行榜后端联调：用 selftest.exe 算出来的签名去打 local_server.py，
   验证「客户端自写 HMAC」和「服务端验签」真的对得上。"""
import json, os, subprocess, sys, time, urllib.request, urllib.error, hashlib, hmac

ROOT = r'D:\maze_web'
PORT = 8791
DB = os.path.join(ROOT, 'build', '_lb_test.db')
if os.path.exists(DB):
    os.remove(DB)

srv = subprocess.Popen([sys.executable, os.path.join(ROOT, 'server', 'local_server.py'),
                        '--port', str(PORT), '--db', DB],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
base = 'http://127.0.0.1:%d' % PORT

def wait_health():
    for _ in range(60):
        try:
            with urllib.request.urlopen(base + '/api/health', timeout=1) as r:
                return json.loads(r.read())
        except Exception:
            time.sleep(0.3)
    raise SystemExit('服务端没起来')

def post(payload):
    req = urllib.request.Request(base + '/api/score',
                                 data=json.dumps(payload).encode('utf-8'),
                                 headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())

def get(path):
    with urllib.request.urlopen(base + path, timeout=5) as r:
        return json.loads(r.read())

results = []
def check(name, ok, detail=''):
    results.append((name, ok, detail))
    print('[%s] %s %s' % ('ok ' if ok else 'BAD', name, detail))

print('health:', wait_health())

# 签名来自 build\desktop\Release\selftest.exe 的 C++ 输出
CPP_SIG = '8131522250c671ecfaa8e780fce9c71c41d8461fcdd25838b928c8e37c03bb48'
base_score = {'name': 'JJC', 'difficulty': 'holy', 'board': 'score',
              'mode': 'nailong', 'score': 4242, 'elapsed': 123.457}

st, body = post(dict(base_score, sig=CPP_SIG))
check('C++ 签名的成绩被接受', st == 200 and body.get('ok'), '%s %s' % (st, body))

st, body = post(dict(base_score, score=9999, sig=CPP_SIG))
check('改分数后签名失效', st == 403, '%s %s' % (st, body))

st, body = post(dict(base_score, sig='0' * 64))
check('随便编个签名被拒', st == 403, '%s %s' % (st, body))

# 时间越界（holy 最短 10s）
fast = dict(base_score, elapsed=2.0)
canon = '|'.join(['JJC', 'holy', 'score', 'nailong', str(4242), '%.3f' % 2.0])
fast['sig'] = hmac.new(b'maze-web-2f8a1c9d4e6b7a30-change-me-on-deploy',
                       canon.encode('utf-8'), hashlib.sha256).hexdigest()
st, body = post(fast)
check('非法用时被拒', st == 422, '%s %s' % (st, body))

board = get('/api/board?difficulty=holy&board=score&limit=10')
names = [r['name'] for r in board.get('rows', [])]
check('榜单里能查到自己', 'JJC' in names, str(board)[:160])

# 重复提交：同一条应该只留一条（去重/取更好，不无限膨胀）
post(dict(base_score, sig=CPP_SIG))
board2 = get('/api/board?difficulty=holy&board=score&limit=50')
cnt = sum(1 for r in board2['rows'] if r['name'] == 'JJC')
check('重复提交不会刷榜', cnt == 1, 'JJC 条数 = %d' % cnt)

srv.terminate()
try:
    srv.wait(timeout=5)
except Exception:
    srv.kill()
bad = [r for r in results if not r[1]]
print('---')
print('%d/%d 通过' % (len(results) - len(bad), len(results)))
os.remove(DB)
sys.exit(1 if bad else 0)