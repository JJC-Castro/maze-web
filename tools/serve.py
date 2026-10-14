# -*- coding: utf-8 -*-
"""serve.py —— 本地预览 Web 版（带正确的 WASM / MIME / 缓存头）

为什么不能直接双击 index.html：
    file:// 下浏览器会以 CORS 为由拒绝加载 .wasm / .data，游戏起不来。
    必须走 HTTP。这个脚本就是一个零依赖的本地静态服务器。

用法：
    python tools/serve.py                    # 服务 build/web，端口 8080
    python tools/serve.py --dir build/web --port 9000
    python tools/serve.py --open             # 起完自动开浏览器
"""
import argparse
import functools
import http.server
import os
import socketserver
import sys
import webbrowser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIR = os.path.join(ROOT, 'build', 'web')


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        '.wasm': 'application/wasm',           # 不带这个 MIME，流式实例化会被拒
        '.data': 'application/octet-stream',
        '.js':   'text/javascript',
        '.mjs':  'text/javascript',
        '.glb':  'model/gltf-binary',
    }

    def end_headers(self):
        # 开发期别让浏览器缓存住 .wasm / .data —— 改一行代码看到旧画面很浪费生命
        self.send_header('Cache-Control', 'no-store, must-revalidate')
        # 万一以后要用 pthreads，这两个头是 SharedArrayBuffer 的前提，先留着不亏
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write('[web] %s\n' % (fmt % args))


class ReusableServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--open', action='store_true')
    args = ap.parse_args()

    directory = os.path.abspath(args.dir)
    if not os.path.isdir(directory):
        raise SystemExit('目录不存在：%s\n先跑一次构建：\n  cmake --build build/web' % directory)

    if not os.path.isfile(os.path.join(directory, 'index.html')):
        print('提示：%s 里还没有 index.html，先构建一次。' % directory)

    url = 'http://%s:%d/' % (args.host, args.port)
    handler = functools.partial(Handler, directory=directory)
    with ReusableServer((args.host, args.port), handler) as httpd:
        print('serving %s' % directory)
        print('open     %s' % url)
        if args.open:
            webbrowser.open(url)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print('\nbye')


if __name__ == '__main__':
    main()