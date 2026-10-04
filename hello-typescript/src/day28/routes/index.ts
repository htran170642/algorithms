import type { AuthController } from "../controllers/auth.controller.js";
import type { ProjectController } from "../controllers/project.controller.js";
import type { TaskController } from "../controllers/task.controller.js";
import { parseId } from "../http.js";
import type { User } from "../models/user.js";
import type { HttpResponse } from "../types/http.js";

export interface RouteContext {
    params: string[];          // các nhóm ( ) trong regex, theo thứ tự
    body: unknown;
    query: URLSearchParams;
}

// Route cần đăng nhập thì handler nhận thêm user, và kiểu đảm bảo user luôn có mặt
export type AuthedContext = RouteContext & { user: User };

interface BaseRoute {
    method: "GET" | "POST" | "PATCH" | "DELETE";
    pattern: RegExp;
}

export interface PublicRoute extends BaseRoute {
    auth: false;
    handler: (ctx: RouteContext) => Promise<HttpResponse>;
}

export interface PrivateRoute extends BaseRoute {
    auth: true;
    handler: (ctx: AuthedContext) => Promise<HttpResponse>;
}

// Discriminated union: kiểm tra route.auth thì TypeScript biết handler nhận kiểu nào
export type Route = PublicRoute | PrivateRoute;

export interface Controllers {
    auth: AuthController;
    projects: ProjectController;
    tasks: TaskController;
}

export function createRoutes({ auth, projects, tasks }: Controllers): Route[] {
    return [
        // ----- Xác thực (không cần đăng nhập) -----
        { method: "POST", pattern: /^\/auth\/register$/, auth: false, handler: ctx => auth.register(ctx.body) },
        { method: "POST", pattern: /^\/auth\/login$/,    auth: false, handler: ctx => auth.login(ctx.body) },

        // ----- Project -----
        { method: "GET",    pattern: /^\/projects$/,          auth: true, handler: ctx => projects.list(ctx.user) },
        { method: "POST",   pattern: /^\/projects$/,          auth: true, handler: ctx => projects.create(ctx.user, ctx.body) },
        { method: "GET",    pattern: /^\/projects\/([^/]+)$/, auth: true, handler: ctx => projects.get(ctx.user, parseId(ctx.params[0])) },
        { method: "PATCH",  pattern: /^\/projects\/([^/]+)$/, auth: true, handler: ctx => projects.update(ctx.user, parseId(ctx.params[0]), ctx.body) },
        { method: "DELETE", pattern: /^\/projects\/([^/]+)$/, auth: true, handler: ctx => projects.remove(ctx.user, parseId(ctx.params[0])) },

        // ----- Task (lồng trong project) -----
        { method: "GET",    pattern: /^\/projects\/([^/]+)\/tasks$/, auth: true,
          handler: ctx => tasks.list(ctx.user, parseId(ctx.params[0]), ctx.query) },
        { method: "POST",   pattern: /^\/projects\/([^/]+)\/tasks$/, auth: true,
          handler: ctx => tasks.create(ctx.user, parseId(ctx.params[0]), ctx.body) },
        { method: "GET",    pattern: /^\/projects\/([^/]+)\/tasks\/([^/]+)$/, auth: true,
          handler: ctx => tasks.get(ctx.user, parseId(ctx.params[0]), parseId(ctx.params[1])) },
        { method: "PATCH",  pattern: /^\/projects\/([^/]+)\/tasks\/([^/]+)$/, auth: true,
          handler: ctx => tasks.update(ctx.user, parseId(ctx.params[0]), parseId(ctx.params[1]), ctx.body) },
        { method: "DELETE", pattern: /^\/projects\/([^/]+)\/tasks\/([^/]+)$/, auth: true,
          handler: ctx => tasks.remove(ctx.user, parseId(ctx.params[0]), parseId(ctx.params[1])) },
    ];
}
