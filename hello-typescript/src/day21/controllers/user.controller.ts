import type { z } from "zod";
import type { User } from "../models/user.js";
import type { UserService } from "../services/user.service.js";
import type { HttpResponse } from "../types/http.js";
import { ValidationError } from "../errors.js";
import { CreateUserSchema, UpdateUserSchema } from "../models/user.schema.js";

function parseBody<T>(schema: z.ZodType<T>, body: unknown): T {
    const result = schema.safeParse(body);
    if (!result.success) {
        const message = result.error.issues
            .map(issue => `${issue.path.join(".") || "body"}: ${issue.message}`)
            .join("; ");
        throw new ValidationError(message);
    }
    return result.data;
}

// ===== Controller: dịch HTTP ↔ service =====
export class UserController {
    private readonly service: UserService;

    constructor(service: UserService) {
        this.service = service;
    }

    async list(): Promise<HttpResponse<User[]>> {
        return { status: 200, body: await this.service.list() };
    }

    async get(id: number): Promise<HttpResponse<User>> {
        return { status: 200, body: await this.service.getById(id) };
    }

    async create(body: unknown): Promise<HttpResponse<User>> {
        const input = parseBody(CreateUserSchema, body);
        return { status: 201, body: await this.service.create(input) };
    }

    async update(id: number, body: unknown): Promise<HttpResponse<User>> {
        const changes = parseBody(UpdateUserSchema, body);
        return { status: 200, body: await this.service.update(id, changes) };
    }

    async remove(id: number): Promise<HttpResponse> {
        await this.service.remove(id);
        return { status: 204 };   // 204 No Content: thành công, không có body
    }
}
