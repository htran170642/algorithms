// ===== 1. JavaScript cho phép throw BẤT CỨ THỨ GÌ =====
function throwWeirdThings(kind: number): void {
    if (kind === 1) throw new Error("Lỗi chuẩn");
    if (kind === 2) throw "chỉ là một chuỗi";
    if (kind === 3) throw 404;
    throw { code: "E_WEIRD" };
}

for (const kind of [1, 2, 3, 4]) {
    try {
        throwWeirdThings(kind);
    } catch (error) {
        // hover: error: unknown → TS không biết cái gì bị ném ra
        console.log(`kind=${kind}:`, typeof error, error instanceof Error);
    }
}

// ===== 2. Hàm tiện ích: lấy message từ unknown an toàn =====
function getErrorMessage(error: unknown): string {
    if (error instanceof Error) {
        return error.message;          // error: Error
    }
    if (typeof error === "string") {
        return error;                  // error: string
    }
    return "Lỗi không xác định";
}

// ===== 3. Custom errors =====
class AppError extends Error {
    readonly statusCode: number;

    constructor(message: string, statusCode: number) {
        super(message);                // gọi constructor của Error → gán message
        this.name = "AppError";        // tên hiện ra khi in lỗi / stack trace
        this.statusCode = statusCode;
    }
}

class UserNotFoundError extends AppError {
    readonly userId: number;

    constructor(userId: number) {
        super(`Không tìm thấy user id=${userId}`, 404);
        this.name = "UserNotFoundError";
        this.userId = userId;
    }
}

class ValidationError extends AppError {
    readonly field: string;

    constructor(field: string, message: string) {
        super(message, 400);
        this.name = "ValidationError";
        this.field = field;
    }
}

// ===== 4. Service ném lỗi có ý nghĩa =====
interface User {
    id: number;
    name: string;
    email: string;
}

const usersDb: User[] = [{ id: 1, name: "Hiep", email: "hiep@example.com" }];

function sleep(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function getUser(id: number): Promise<User> {
    await sleep(100);
    if (!Number.isInteger(id) || id <= 0) {
        throw new ValidationError("id", "id phải là số nguyên dương");
    }
    const user = usersDb.find(u => u.id === id);
    if (!user) {
        throw new UserNotFoundError(id);
    }
    return user;
}

// ===== 5. Chuyển lỗi thành HTTP response (chuẩn bị cho Day 19) =====
interface ErrorResponse {
    status: number;
    body: { error: string; field?: string };
}

function toErrorResponse(error: unknown): ErrorResponse {
    // Kiểm tra lớp CON trước, lớp CHA sau (ValidationError cũng là AppError!)
    if (error instanceof ValidationError) {
        return { status: error.statusCode, body: { error: error.message, field: error.field } };
    }
    if (error instanceof AppError) {
        return { status: error.statusCode, body: { error: error.message } };
    }
    // Lỗi không lường trước → 500, KHÔNG lộ chi tiết cho client
    console.error("Lỗi bất ngờ:", error);
    return { status: 500, body: { error: "Internal Server Error" } };
}

async function handleGetUser(id: number): Promise<void> {
    try {
        const user = await getUser(id);
        console.log("200", user);
    } catch (error) {
        const res = toErrorResponse(error);
        console.log(res.status, res.body);
    } finally {
        // finally LUÔN chạy, dù thành công hay lỗi (đóng kết nối, ghi log, …)
        console.log(`  (finally) đã xử lý request id=${id}`);
    }
}

await handleGetUser(1);     // 200
await handleGetUser(99);    // 404
await handleGetUser(-5);    // 400

// ===== 6. Bẫy: quên await trong try =====
async function forgotAwaitInTry(): Promise<void> {
    try {
        getUser(99);            // KHÔNG await → lỗi xảy ra SAU khi try đã kết thúc
        console.log("Tưởng là ổn...");
    } catch {
        console.log("Không bao giờ chạy tới đây");
    }
}

// Bắt các Promise bị reject mà không ai xử lý (chỉ để minh họa)
process.on("unhandledRejection", reason => {
    console.log("💥 unhandledRejection:", getErrorMessage(reason));
});

await forgotAwaitInTry();
await sleep(200);

console.log(getErrorMessage("abc"), "|", getErrorMessage(42));

// ===== 7. Thí nghiệm: bỏ comment TỪNG dòng, đọc lỗi =====

// (a) Dùng error mà không narrowing
// try { throw new Error("x"); } catch (error) { console.log(error.message); }
//   → 'error' is of type 'unknown'.

// (b) Tự khai báo kiểu cho error
// try { throw new Error("x"); } catch (error: Error) { console.log(error); }
//   → Catch clause variable type annotation must be 'any' or 'unknown' if specified.

// (c) Khai báo field mà quên gán trong constructor
// class E2 extends Error { readonly code: number; constructor(m: string) { super(m); } }
//   → Property 'code' has no initializer and is not definitely assigned in the constructor.
