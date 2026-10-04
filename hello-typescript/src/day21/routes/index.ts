import type { UserController } from "../controllers/user.controller.js";
import { parseId } from "../http.js";
import type { HttpResponse } from "../types/http.js";

export interface Route {
    method: "GET" | "POST" | "PATCH" | "DELETE";
    pattern: RegExp;
    handler: (match: RegExpMatchArray, body: unknown) => Promise<HttpResponse>;
}

// Nhận controller từ bên ngoài (dependency injection)
export function createRoutes(controller: UserController): Route[] {
    return [
        { method: "GET",    pattern: /^\/users$/,          handler: () => controller.list() },
        { method: "GET",    pattern: /^\/users\/([^/]+)$/, handler: m => controller.get(parseId(m[1])) },
        { method: "POST",   pattern: /^\/users$/,          handler: (_m, body) => controller.create(body) },
        { method: "PATCH",  pattern: /^\/users\/([^/]+)$/, handler: (m, body) => controller.update(parseId(m[1]), body) },
        { method: "DELETE", pattern: /^\/users\/([^/]+)$/, handler: m => controller.remove(parseId(m[1])) },
    ];
}
