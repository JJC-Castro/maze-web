// ===========================================================================
//  [[path]].js —— Cloudflare Pages Functions 入口
//
//  Pages 只负责把 /api/* 的请求转给 server/worker.js 的 fetch 处理，
//  校验规则 / 限流 / D1 写入全在那边，只有一份实现，别在这里再抄一遍。
//  这样后端既能以 Worker 部署，也能以 Pages（国内可达）部署。
// ===========================================================================
import worker from "../../../worker.js";

export const onRequest = (context) => worker.fetch(context.request, context.env);
