import type { CreateUserInput, UpdateUserInput, User } from "../models/user.js";
import type { UserService } from "../services/user.service.js";
import type { HttpResponse } from "../http.js";
import { ValidationError } from "../errors.js";

// ===== Validate thủ công: body là unknown → phải kiểm tra TỪNG field =====
function asObject(body: unknown): Record<string, unknown> {
    if (typeof body !== "object" || body === null || Array.isArray(body)) {
        throw new ValidationError("Body phải là một JSON object");
    }
    return body as Record<string, unknown>;
}

function validateName(value: unknown): string {
    if (typeof value !== "string" || value.trim() === "") {
        throw new ValidationError("name phải là chuỗi không rỗng");
    }
    return value.trim();
}

function validateEmail(value: unknown): string {
    if (typeof value !== "string" || !value.includes("@")) {
        throw new ValidationError("email không hợp lệ");
    }
    return value;
}

function validateAge(value: unknown): number {
    if (typeof value !== "number" || !Number.isInteger(value) || value < 0 || value > 150) {
        throw new ValidationError("age phải là số nguyên từ 0 đến 150");
    }
    return value;
}

function parseCreateUser(body: unknown): CreateUserInput {
    const obj = asObject(body);
    return {
        name: validateName(obj.name),
        email: validateEmail(obj.email),
        age: validateAge(obj.age),
    };
}

function parseUpdateUser(body: unknown): UpdateUserInput {
    const obj = asObject(body);
    const result: UpdateUserInput = {};
    if (obj.name !== undefined) result.name = validateName(obj.name);
    if (obj.email !== undefined) result.email = validateEmail(obj.email);
    if (obj.age !== undefined) result.age = validateAge(obj.age);
    return result;
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
        const input = parseCreateUser(body);
        return { status: 201, body: await this.service.create(input) };
    }

    async update(id: number, body: unknown): Promise<HttpResponse<User>> {
        const changes = parseUpdateUser(body);
        return { status: 200, body: await this.service.update(id, changes) };
    }

    async remove(id: number): Promise<HttpResponse> {
        await this.service.remove(id);
        return { status: 204 };   // 204 No Content: thành công, không có body
    }
}
