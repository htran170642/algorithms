import { z } from "zod";
import { CreateTaskSchema, UpdateTaskSchema } from "../models/task.schema.js";
import { TASK_STATUSES } from "../models/task.js";
import type { Task } from "../models/task.js";
import type { User } from "../models/user.js";
import type { TaskService } from "../services/task.service.js";
import type { ApiResponse } from "../types/common.js";
import type { HttpResponse } from "../types/http.js";
import { parseBody } from "../utils/parse-body.js";
import { respond } from "../utils/respond.js";

// ?status=done → "done"; không có tham số → undefined; giá trị lạ → lỗi 400
const StatusQuerySchema = z.enum(TASK_STATUSES).optional();

export class TaskController {
    constructor(private readonly tasks: TaskService) {}

    async list(user: User, projectId: number, query: URLSearchParams): Promise<HttpResponse<ApiResponse<Task[]>>> {
        const status = parseBody(StatusQuerySchema, query.get("status") ?? undefined);
        return respond(200, await this.tasks.list(user, projectId, status));
    }

    async get(user: User, projectId: number, taskId: number): Promise<HttpResponse<ApiResponse<Task>>> {
        return respond(200, await this.tasks.get(user, projectId, taskId));
    }

    async create(user: User, projectId: number, body: unknown): Promise<HttpResponse<ApiResponse<Task>>> {
        const input = parseBody(CreateTaskSchema, body);
        return respond(201, await this.tasks.create(user, projectId, input));
    }

    async update(user: User, projectId: number, taskId: number, body: unknown): Promise<HttpResponse<ApiResponse<Task>>> {
        const changes = parseBody(UpdateTaskSchema, body);
        return respond(200, await this.tasks.update(user, projectId, taskId, changes));
    }

    async remove(user: User, projectId: number, taskId: number): Promise<HttpResponse> {
        await this.tasks.remove(user, projectId, taskId);
        return { status: 204 };
    }
}
