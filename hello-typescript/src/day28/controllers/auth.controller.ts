import { LoginSchema, RegisterSchema } from "../models/auth.schema.js";
import type { PublicUser } from "../models/user.js";
import type { AuthService, LoginResult } from "../services/auth.service.js";
import type { ApiResponse } from "../types/common.js";
import type { HttpResponse } from "../types/http.js";
import { parseBody } from "../utils/parse-body.js";
import { respond } from "../utils/respond.js";

export class AuthController {
    constructor(private readonly auth: AuthService) {}

    async register(body: unknown): Promise<HttpResponse<ApiResponse<PublicUser>>> {
        const user = await this.auth.register(parseBody(RegisterSchema, body));
        return respond(201, user, "Đăng ký thành công");
    }

    async login(body: unknown): Promise<HttpResponse<ApiResponse<LoginResult>>> {
        const result = await this.auth.login(parseBody(LoginSchema, body));
        return respond(200, result);
    }
}
