// ===========================================================================
//  config.js —— Web 版运行时配置（改这个文件不用重新编译）
//
//  这个文件必须在 index.js 之前加载（shell.html 里已经引好了）。
// ===========================================================================
window.MAZE_CONFIG = {
  // 排行榜后端地址（**要带 /api 前缀**，客户端拼的是 apiBase + '/score'、'/board'）。
  //
  //  现在指向 Cloudflare Pages 版后端（源码在 server/pages/，函数直接 import
  //  server/worker.js，只有一份实现）。为什么不用 Worker 的 workers.dev：
  //  2026-10-16 实测，*.workers.dev 在国内被 DNS 污染（解析到 Facebook 的
  //  199.59.149.203 / 2a03:2880:... ，TCP 443 直接超时），而 *.pages.dev 解析到的
  //  是真 CF 边缘 IP（104.18.x / 2606:4700::，走香港节点），国内可直连。
  //
  //  想换地址不用重编 wasm，改这个文件就行（它会被拷到 index.html 旁边）：
  //    · 站点与后端同域（比如整体搬到 CF Pages）：'/api'
  //    · 后端单独部署：'https://<你的项目>.pages.dev/api'
  //    · 本地联调（python server/local_server.py）：'http://127.0.0.1:8787/api'
  apiBase: 'https://maze-api.pages.dev/api',

  // 关掉之后游戏完全不联网：不提交成绩、不拉排行榜，只写本地 log.csv。
  // 断网环境 / 不想让人看到成绩上传时用。
  online: true,
};
