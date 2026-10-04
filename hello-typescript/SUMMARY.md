# TypeScript — Tổng hợp Day 1–26

Tài liệu ôn tập nhanh. Mỗi mục gồm: **là gì**, **vì sao dùng**, **ví dụ ngắn**.

---

## 0. Mô hình tư duy

```text
.ts  ──tsc (kiểm tra kiểu)──▶  .js  ──node──▶  chạy
```

- `tsc` làm hai việc: **kiểm tra kiểu** và **chuyển `.ts` thành `.js`**. `node` chỉ chạy `.js`.
- **Kiểu biến mất khi chạy.** `interface User` không kiểm tra được JSON thật.
- Vì vậy: *kiểm tra kiểu (compile-time) ≠ kiểm tra dữ liệu (runtime)*. Dữ liệu từ ngoài phải qua Zod.

### Lệnh của dự án

| Lệnh | Việc |
|---|---|
| `npm run typecheck` | `tsc --noEmit`: kiểm tra kiểu toàn dự án |
| `npx tsx src/dayN.ts` | Chạy thẳng `.ts`, **KHÔNG kiểm tra kiểu** |
| `npm run build` | `tsc` → `dist/` |

> Luôn chạy `typecheck` cùng `tsx`. `tsx` chạy được không có nghĩa là kiểu đúng.

---

## 1. Kiểu cơ bản (Day 2)

```ts
const name: string = "Hiep";
const age: number = 30;
const active: boolean = true;
const scores: number[] = [1, 2, 3];
const user: [string, number] = ["Hiep", 30];   // tuple: độ dài và kiểu từng vị trí cố định
```

| Kiểu | Ý nghĩa |
|---|---|
| `null` / `undefined` | Cố ý không có / chưa gán |
| `void` | Hàm không trả gì |
| `any` | Tắt kiểm tra (tránh dùng) |
| `unknown` | Chưa biết, phải thu hẹp kiểu trước khi dùng |
| `never` | Không có giá trị nào |

---

## 2. Object (Day 3)

```ts
type User = {
    id: number;
    name: string;
    age?: number;            // tùy chọn
    readonly email: string;  // không sửa sau khi tạo
};

type Scores = { [subject: string]: number };   // index signature: không biết trước tên key
```

---

## 3. Hàm (Day 4)

```ts
function greet(name: string, greeting?: string): string {
    return `${greeting ?? "Hello"} ${name}`;   // ?? : chỉ thay khi null/undefined
}
function power(base: number, exp: number = 2): number { return base ** exp; }

type Calculator = (a: number, b: number) => number;
const add: Calculator = (a, b) => a + b;       // a, b tự suy ra từ Calculator
```

> `10 / 0` trong JS ra `Infinity`, **không** ném lỗi. Muốn lỗi thì phải tự `throw`.

---

## 4. interface vs type (Day 5)

```ts
interface Admin extends User { permissions: string[] }   // kế thừa
type Admin = User & { permissions: string[] };            // intersection
```

| | `interface` | `type` |
|---|---|---|
| Khai báo trùng tên | **Gộp lại** (declaration merging) | Lỗi |
| Union, tuple, kiểu nguyên thủy | Không | **Có** |
| Mở rộng | `extends` | `&` |

Quy tắc thực dụng: object/hợp đồng dùng `interface`; union và kiểu tính toán dùng `type`.

---

## 5. Union, Intersection, Literal (Day 6)

```ts
type Status = "pending" | "paid" | "failed";   // union của literal
type ID = string | number;
type UserWithTime = User & { createdAt: Date }; // intersection: thỏa CẢ HAI
```

---

## 6. Thu hẹp kiểu — Narrowing (Day 8)

```ts
if (typeof v === "string") { v.toUpperCase() }     // typeof
if (e instanceof Error)    { e.message }           // instanceof
if ("name" in obj)         { obj.name }            // in
```

Bên trong `if`, TypeScript biết kiểu hẹp hơn (control-flow analysis).

---

## 7. Discriminated union & kiểm tra đầy đủ (Day 9, 25)

```ts
type Shape =
    | { kind: "circle"; radius: number }
    | { kind: "square"; side: number };

function area(s: Shape): number {
    switch (s.kind) {
        case "circle": return Math.PI * s.radius ** 2;
        case "square": return s.side ** 2;
        default: { const _: never = s; return fail("unreachable"); }
    }
}
```

- Field chung (`kind`) là **discriminant**; kiểm tra nó thì kiểu được thu hẹp.
- `never` ở `default`: thêm biến thể mới mà quên `case` → **lỗi biên dịch**.
- `as` ép kiểu có thể "nói dối" và lọt xuống `default` lúc chạy.

---

## 8. Generics (Day 10–11, 22)

```ts
function first<T>(items: T[]): T | undefined { return items[0]; }
function getId<T extends { id: number }>(o: T): number { return o.id; }   // ràng buộc
function getProperty<T, K extends keyof T>(o: T, k: K): T[K] { return o[k]; }
```

- `<T>`: bất kỳ kiểu; `<T extends U>`: kiểu **ít nhất** có hình dạng `U`.
- `keyof T`: union các tên key. `T[K]`: kiểu của giá trị tại key `K`.
- `T[K]` (đúng một key) khác `T[keyof T]` (hợp mọi key).

---

## 9. Utility types (Day 12)

| Kiểu | Kết quả |
|---|---|
| `Partial<T>` | Mọi field tùy chọn |
| `Required<T>` | Mọi field bắt buộc |
| `Readonly<T>` | Mọi field chỉ đọc |
| `Pick<T, K>` | Chỉ giữ các key `K` |
| `Omit<T, K>` | Bỏ các key `K` |
| `Record<K, V>` | Object có key `K`, giá trị `V` |

```ts
type CreateUserInput = Omit<User, "id">;
type UpdateUserInput = Partial<CreateUserInput>;
```

---

## 10. Modules (Day 15–16)

Dự án là **ESM** (`"type": "module"`, `module: "nodenext"`):

```ts
import type { User } from "./models/user.js";   // chỉ kiểu → bắt buộc `import type`
import { parseId } from "./http.js";             // đuôi .js dù file nguồn là .ts
```

- `verbatimModuleSyntax` ép phân biệt `import type` và `import`.
- `moduleDetection: "force"`: mỗi file là một module riêng, nên các `User` ở các file day khác nhau không xung đột.

---

## 11. Async & lỗi (Day 17–18)

```ts
async function getUser(id: number): Promise<User> { ... }

const all = await Promise.all([a(), b()]);          // chờ tất cả; một cái lỗi thì lỗi cả nhóm
const each = await Promise.allSettled([a(), b()]);  // chờ tất cả; mỗi cái có kết quả riêng
```

```ts
class AppError extends Error {
    constructor(message: string, readonly statusCode: number) { super(message); }
}
try { ... } catch (e) {             // e là unknown
    if (e instanceof AppError) { ... }
}
```

`catch (e)`: `e` là `unknown`, nên phải thu hẹp (`instanceof`) trước khi dùng.

---

## 12. REST API 3 tầng (Day 19–21)

```text
Client → index.ts (router) → http.ts (đọc body/id)
       → Controller (Zod + status code)
       → Service (nghiệp vụ: email trùng? không tìm thấy?)
       → Repository (lưu/đọc dữ liệu)
```

| Tầng | Biết | Không biết |
|---|---|---|
| Controller | HTTP, validate đầu vào | Dữ liệu lưu ở đâu |
| Service | Quy tắc nghiệp vụ | HTTP |
| Repository | Cách lưu | Nghiệp vụ |

- Lỗi: tầng nào cũng `throw` một lớp con của `AppError` (`ValidationError` 400, `NotFoundError` 404, `ConflictError` 409). `toErrorResponse` ở trên cùng đổi thành HTTP response. Lỗi lạ → 500.
- Mã trạng thái: `200` OK, `201` tạo mới, `204` xóa (không body), `400` dữ liệu sai, `404` không tìm thấy, `409` trùng, `500` lỗi server.
- Cấu trúc thư mục: `controllers/ services/ repositories/ models/ routes/ types/ index.ts`.

---

## 13. DTO & Zod (Day 20)

```ts
export const CreateUserSchema = z.object({
    name: z.string().trim().min(1),
    email: z.email(),
    age: z.number().int().min(0).max(150),
});
export const UpdateUserSchema = CreateUserSchema.partial();
export type CreateUserInput = z.infer<typeof CreateUserSchema>;

function parseBody<T>(schema: z.ZodType<T>, body: unknown): T {
    const r = schema.safeParse(body);
    if (!r.success) throw new ValidationError(/* ... */);
    return r.data;
}
```

| | Schema | `Input` (kiểu) |
|---|---|---|
| Là | Giá trị (chạy lúc runtime) | Kiểu (chỉ lúc compile) |
| Dùng ở | **Biên**, nơi dữ liệu `unknown` đi vào | Mọi nơi **sau** biên |

- `safeParse` trả discriminated union `{ success: true, data } | { success: false, error }`.
- `z.infer` rút kiểu từ schema → kiểu và luật kiểm tra **không thể lệch nhau**.
- Service/Repository **không** validate lại hình dạng; Service chỉ kiểm tra nghiệp vụ.

---

## 14. Conditional & Mapped types (Day 23–24)

```ts
type IsString<T> = T extends string ? true : false;
type ElementOf<T> = T extends (infer U)[] ? U : T;          // infer: "tự điền chỗ trống"
type ReturnOf<F> = F extends (...a: any[]) => infer R ? R : never;
```

```ts
type Optional<T> = { [K in keyof T]?: T[K] };               // chính là Partial<T>
type MyPick<T, K extends keyof T> = { [P in K]: T[P] };     // chính là Pick<T, K>
type Mutable<T> = { -readonly [K in keyof T]: T[K] };       // dấu - gỡ modifier
```

- `infer` chỉ viết được ở vế `extends` của conditional type.
- Mapped type = vòng lặp trên key. `Omit<T,K>` = `Pick<T, Exclude<keyof T, K>>`.
- Mối quan hệ: `keyof` (lấy key) + generics (dùng mọi kiểu) + mapped (duyệt key) + conditional (quyết định biến đổi).

---

## 15. any / unknown / never (Day 25)

| | `any` | `unknown` | `never` |
|---|---|---|---|
| Là | Tắt kiểm tra | Chưa biết, phải kiểm chứng | Tập rỗng |
| Dùng `x.foo` | Được (nguy hiểm) | **Lỗi** đến khi thu hẹp | — |
| Dùng khi | Hầu như không | Dữ liệu từ ngoài | Hàm luôn `throw`; kiểm tra đầy đủ |

`JSON.parse` trả `any` → ép về `unknown` rồi validate bằng Zod.

---

## 16. Design patterns (Day 26)

```ts
type Result<T> = { success: true; data: T } | { success: false; error: string };

interface Repository<T> {
    findById(id: number): Promise<T | null>;
    findAll(): Promise<T[]>;
    create(data: T): Promise<T>;
}

class UserService {
    constructor(private repository: Repository<User>) {}   // DI: nhận interface, không tự new
}
```

| Mẫu | Giải quyết vấn đề |
|---|---|
| `Result<T>` | Hàm có thể thất bại: kiểu trả về nói rõ, buộc người gọi xử lý cả hai nhánh |
| `Repository<T>` (interface) | Service không phụ thuộc cách lưu; `implements` ép class đủ method |
| Dependency Injection | Đổi/thay phụ thuộc mà không sửa Service; dễ kiểm thử |

`throw` hợp với lỗi bất thường (bay lên `catch` trên cùng); `Result` hợp khi thất bại là kết quả bình thường cần xử lý ngay.

---

## 17. Bẫy đã gặp

| Bẫy | Cách nhớ |
|---|---|
| `tsc` vẫn sinh `.js` dù có lỗi kiểu | Muốn dừng thì `noEmitOnError: true` |
| `tsx` chạy được nhưng kiểu sai | Luôn chạy `npm run typecheck` |
| `10 / 0` → `Infinity` | Tự `throw` khi chia cho 0 |
| `exactOptionalPropertyTypes` | `name?: string` ≠ `name?: string \| undefined` |
| `as Type` "nói dối" | Lọt dữ liệu sai; dùng Zod ở biên thay vì `as` |
| Import thiếu `.js` hoặc quên `import type` | ESM + `verbatimModuleSyntax` |
| PowerShell: `curl` là alias `Invoke-WebRequest` | Dùng `curl.exe`, JSON viết `\"` |
| PowerShell chặn `npm.ps1` | `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` |
| `String(obj)` ra `[object Object]` | Dùng `JSON.stringify` trong thông báo lỗi |

---

## 18. Tiến độ

| Tuần | Nội dung | Trạng thái |
|---|---|---|
| 1 | Day 1–7: nền tảng, kiểu cơ bản, hàm, interface/type, union, CLI | Xong |
| 2 | Day 8–14: narrowing, generics, utility types, repository | Xong |
| 3 | Day 15–21: modules, async, lỗi, REST API, Zod | Xong |
| 4 | Day 22–26: generics nâng cao, conditional/mapped, `never`, patterns | Xong |
| 4 | Day 27: đọc code TypeScript thật | **Chưa làm** |
| 4 | Day 28: Task Management API (Users, Projects, Tasks, Auth) | **Chưa làm** |

### Tự kiểm tra (cuối tháng, giải thích bằng lời của bạn)

- Structural typing là gì? Vì sao `interface` và `type` thường dùng thay nhau được?
- Vì sao cần cả TypeScript lẫn Zod?
- `any` khác `unknown` thế nào, khi nào dùng `never`?
- `K extends keyof T` ngăn lỗi gì, vì sao trả về `T[K]`?
- Vì sao Service nhận `Repository<User>` (interface) mà không nhận class cụ thể?
