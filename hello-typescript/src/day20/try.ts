import { CreateUserSchema, UpdateUserSchema } from "./schema.js";

const inputs: unknown[] = [
    { name: "  An  ", email: "an@example.com", age: 25 },   // đúng (name được trim)
    { name: "An", age: 25 },                                 // thiếu email
    { name: "An", email: "an@example.com", age: "25" },      // age là chuỗi
    "không phải object",                                     // sai hẳn kiểu
];


for (const input of inputs) {
    const result = CreateUserSchema.safeParse(input);

    // result là discriminated union: { success: true, data } | { success: false, error }
    if (result.success) {
        console.log("OK  ", result.data);
    } else {
        console.log("FAIL", result.error.issues.map(i => `${i.path.join(".")}: ${i.message}`));
    }
}

console.log("update:", UpdateUserSchema.safeParse({ age: 31 }).success);   // true
console.log("update:", UpdateUserSchema.safeParse({ age: -1 }).success);   // false