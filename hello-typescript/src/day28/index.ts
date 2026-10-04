import { createServer } from "node:http";
import type { IncomingMessage } from "node:http";
import { AuthController } from "./controllers/auth.controller.js";
import { ProjectController } from "./controllers/project.controller.js";
import { TaskController } from "./controllers/task.controller.js";
import { readJsonBody, sendJson, toErrorResponse } from "./http.js";
import { ProjectRepository } from "./repositories/project.repository.js";
import { TaskRepository } from "./repositories/task.repository.js";
import { UserRepository } from "./repositories/user.repository.js";
import { createRoutes } from "./routes/index.js";
import { AuthService } from "./services/auth.service.js";
import { ProjectService } from "./services/project.service.js";
import { TaskService } from "./services/task.service.js";
import type { HttpResponse } from "./types/http.js";

// ===== Lắp ráp: Repository → Service → Controller → Routes =====
const userRepo = new UserRepository();
const projectRepo = new ProjectRepository();
const taskRepo = new TaskRepository();

const authService = new AuthService(userRepo);
const projectService = new ProjectService(projectRepo, taskRepo);
const taskService = new TaskService(taskRepo, projectService);

const routes = createRoutes({
    auth: new AuthController(authService),
    projects: new ProjectController(projectService),
    tasks: new TaskController(taskService),
});

// Chỉ POST và PATCH mới có body
async function readBody(req: IncomingMessage, method: string): Promise<unknown> {
    return method === "POST" || method === "PATCH" ? readJsonBody(req) : undefined;
}

// ===== HTTP server =====
const server = createServer(async (req, res) => {
    const method = req.method ?? "GET";
    const url = new URL(req.url ?? "/", "http://localhost");

    try {
        for (const route of routes) {
            if (route.method !== method) continue;
            const match = url.pathname.match(route.pattern);
            if (!match) continue;

            const base = { params: match.slice(1), query: url.searchParams };
            let response: HttpResponse;

            if (route.auth) {
                // Xác thực TRƯỚC khi đọc body; sai token thì ném 401 ngay
                const user = await authService.authenticate(req.headers.authorization);
                response = await route.handler({ ...base, body: await readBody(req, method), user });
            } else {
                response = await route.handler({ ...base, body: await readBody(req, method) });
            }

            console.log(`${method} ${url.pathname} → ${response.status}`);
            sendJson(res, response);
            return;
        }
        console.log(`${method} ${url.pathname} → 404`);
        sendJson(res, { status: 404, body: { error: `Không có route ${method} ${url.pathname}` } });
    } catch (error) {
        const response = toErrorResponse(error);
        console.log(`${method} ${url.pathname} → ${response.status}`);
        sendJson(res, response);
    }
});

const PORT = Number(process.env["PORT"] ?? 3000);
server.listen(PORT, () => {
    console.log(`Task API chạy tại http://localhost:${PORT}`);
});
