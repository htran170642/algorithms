import { randomBytes } from "node:crypto";
import type { LoginInput, RegisterInput } from "../models/auth.schema.js";
import { toPublicUser } from "../models/user.js";
import type { PublicUser, User } from "../models/user.js";
import type { UserRepository } from "../repositories/user.repository.js";
import { ConflictError, UnauthorizedError } from "../errors.js";
import { hashPassword, verifyPassword } from "../utils/password.js";

export interface LoginResult {
    token: string;
    user: PublicUser;
}

export class AuthService {
    // token → id của user. Lưu trong bộ nhớ nên restart server là mất.
    private readonly sessions = new Map<string, number>();

    constructor(private readonly users: UserRepository) {}

    async register(input: RegisterInput): Promise<PublicUser> {
        if (await this.users.findByEmail(input.email)) {
            throw new ConflictError(`Email đã tồn tại: ${input.email}`);
        }
        const user = await this.users.create({
            name: input.name,
            email: input.email,
            passwordHash: await hashPassword(input.password),
            createdAt: new Date(),
        });
        return toPublicUser(user);
    }

    async login(input: LoginInput): Promise<LoginResult> {
        const user = await this.users.findByEmail(input.email);
        // Cùng một thông báo cho "sai email" và "sai mật khẩu" để không lộ email nào tồn tại
        if (!user || !(await verifyPassword(input.password, user.passwordHash))) {
            throw new UnauthorizedError("Email hoặc mật khẩu không đúng");
        }
        const token = randomBytes(32).toString("hex");
        this.sessions.set(token, user.id);
        return { token, user: toPublicUser(user) };
    }

    // Nhận giá trị của header Authorization, trả về User đang đăng nhập
    async authenticate(header: string | undefined): Promise<User> {
        const token = header?.startsWith("Bearer ") ? header.slice("Bearer ".length) : undefined;
        const userId = token === undefined ? undefined : this.sessions.get(token);
        if (userId === undefined) {
            throw new UnauthorizedError("Chưa đăng nhập hoặc token không hợp lệ");
        }
        const user = await this.users.findById(userId);
        if (!user) {
            throw new UnauthorizedError("Chưa đăng nhập hoặc token không hợp lệ");
        }
        return user;
    }
}
