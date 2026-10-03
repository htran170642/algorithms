import { createServer } from "node:http";
import { UserRepository } from "./repositories/user.repository.js";
import { UserService } from "./services/user.service.js";
import { UserController } from "./controllers/user.controller.js";
import { parseId, readJsonBody, sendJson, toErrorResponse } from "./http.js";
import type { HttpResponse } from "./http.js";

// ===== Lắp ráp các tầng: Repository → Service → Controller =====
const repo = new UserRepository();
const service = new UserService(repo);
const controller = new UserController(service);

// Dữ liệu mẫu
await service.create({ name: "Hiep", email: "hiep@example.com", age: 30 });
await service.create({ name: "An", email: "an@example.com", age: 25 });

// ===== Router: method + đường dẫn → hàm xử lý =====
interface Route {
    method: "GET" | "POST" | "PATCH" | "DELETE";
    pattern: RegExp;
    handler: (match: RegExpMatchArray, body: unknown) => Promise<HttpResponse>;
}

const routes: Route[] = [
    { method: "GET",    pattern: /^\/users$/,          handler: () => controller.list() },
    { method: "GET",    pattern: /^\/users\/([^/]+)$/, handler: m => controller.get(parseId(m[1])) },
    { method: "POST",   pattern: /^\/users$/,          handler: (_m, body) => controller.create(body) },
    { method: "PATCH",  pattern: /^\/users\/([^/]+)$/, handler: (m, body) => controller.update(parseId(m[1]), body) },
    { method: "DELETE", pattern: /^\/users\/([^/]+)$/, handler: m => controller.remove(parseId(m[1])) },
];

// ===== HTTP server =====
const server = createServer(async (req, res) => {
    const method = req.method ?? "GET";
    const path = new URL(req.url ?? "/", "http://localhost").pathname;

    try {
        for (const route of routes) {
            if (route.method !== method) continue;
            const match = path.match(route.pattern);
            if (!match) continue;

            const body = method === "POST" || method === "PATCH" ? await readJsonBody(req) : undefined;
            const response = await route.handler(match, body);
            console.log(`${method} ${path} → ${response.status}`);
            sendJson(res, response);
            return;
        }
        console.log(`${method} ${path} → 404`);
        sendJson(res, { status: 404, body: { error: `Không có route ${method} ${path}` } });
    } catch (error) {
        const response = toErrorResponse(error);
        console.log(`${method} ${path} → ${response.status}`);
        sendJson(res, response);
    }
});

const PORT = Number(process.env["PORT"] ?? 3000);
server.listen(PORT, () => {
    console.log(`Server chạy tại http://localhost:${PORT}`);
});
