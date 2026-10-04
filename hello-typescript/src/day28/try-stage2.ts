import { UserRepository } from "./repositories/user.repository.js";
import { AuthService } from "./services/auth.service.js";
import { AppError } from "./errors.js";

const auth = new AuthService(new UserRepository());

const registered = await auth.register({
    name: "Hiep",
    email: "hiep@example.com",
    password: "secret123",
});
console.log("đăng ký:", registered);

const { token, user } = await auth.login({ email: "hiep@example.com", password: "secret123" });
console.log("đăng nhập:", user.name, "| token dài", token.length, "ký tự");

const me = await auth.authenticate(`Bearer ${token}`);
console.log("authenticate:", me.email);

async function expectError(label: string, run: () => Promise<unknown>): Promise<void> {
    try {
        await run();
        console.log(label, "→ KHÔNG lỗi (sai!)");
    } catch (error) {
        if (error instanceof AppError) {
            console.log(label, "→", error.statusCode, error.message);
        } else {
            throw error;
        }
    }
}

await expectError("email trùng", () =>
    auth.register({ name: "X", email: "hiep@example.com", password: "secret123" }));
await expectError("sai mật khẩu", () =>
    auth.login({ email: "hiep@example.com", password: "wrong-pass" }));
await expectError("email không tồn tại", () =>
    auth.login({ email: "nobody@example.com", password: "secret123" }));
await expectError("token sai", () => auth.authenticate("Bearer abc"));
await expectError("thiếu header", () => auth.authenticate(undefined));
