// ===========================================================================
//  config.js —— Web 版运行时配置（改这个文件不用重新编译）
//
//  这个文件必须在 index.js 之前加载（shell.html 里已经引好了）。
// ===========================================================================
window.MAZE_CONFIG = {
  // 排行榜后端地址（**要带 /api 前缀**，客户端拼的是 apiBase + '/score'、'/board'）。
  //   · 同域部署（Cloudflare Pages 用 /api/* 路由到 Worker）：'/api'
  //   · 后端单独部署在别的域名：'https://maze-leaderboard.你的账号.workers.dev/api'
  //   · 本地联调（python server/local_server.py）：'http://127.0.0.1:8787/api'
  // 想只改地址不重新编译 wasm，就改这个文件（它会被拷到 index.html 旁边）。
  apiBase: '/api',

  // 关掉之后游戏完全不联网：不提交成绩、不拉排行榜，只写本地 log.csv。
  // 断网环境 / 不想让人看到成绩上传时用。
  online: true,
};