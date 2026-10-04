// ===== Phần 1: điều kiện cơ bản =====
type IsString<T> = T extends string ? true : false;

type A = IsString<string>;    // true
type B = IsString<number>;    // false
type C = IsString<"hello">;   // true

// ===== Phần 2: infer =====
type ElementOf<T> = T extends (infer U)[] ? U : T;

type E1 = ElementOf<string[]>;   // string
type E2 = ElementOf<number[]>;   // number
type E3 = ElementOf<boolean>;    // boolean (không phải mảng → trả lại chính T)

// ===== Phần 3: kiểu trả về của hàm =====
type ReturnOf<F> = F extends (...args: any[]) => infer R ? R : never;

function greet(name: string): string {
    return `Hi ${name}`;
}
function count(): number {
    return 3;
}

type R1 = ReturnOf<typeof greet>;   // string
type R2 = ReturnOf<typeof count>;   // number

// Dòng dưới chỉ để kiểm chứng: gán giá trị đúng kiểu thì không lỗi
const r1: R1 = "ok";
const r2: R2 = 42;
console.log(r1, r2);
