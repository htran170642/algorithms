type User = {
    id: number;
    name: string;
    email: string;
}

type Optional<T> = {
    [K in keyof T]?: T[K];
};

type OptionalUser = Optional<User>;
// { id?: number; name?: string; email?: string }



// ===== Phần 2: đổi kiểu của từng thuộc tính =====
type Stringify<T> = {
    [K in keyof T]: string;
};

type UserForm = Stringify<User>;
// { id: string; name: string; email: string }


// ===== Phần 3: thêm / bỏ modifier =====
type ReadonlyAll<T> = {
    readonly [K in keyof T]: T[K];
};


type Mutable<T> = {
    -readonly [K in keyof T]: T[K];
};

type Required2<T> = {
    [K in keyof T]-?: T[K];
};

// ===== Phần 4: kết hợp với conditional type =====
type NullableStrings<T> = {
    [K in keyof T]: T[K] extends string ? T[K] | null : T[K];
};

type UserWithNull = NullableStrings<User>;
// { id: number; name: string | null; email: string | null }


// const locked: ReadonlyAll<User> = { id: 1, name: "Hiep", email: "h@x.com" };
// locked.name = "An";            // ❌ lỗi: không gán được vì là thuộc tính chỉ đọc

const open: Mutable<ReadonlyAll<User>> = { id: 1, name: "Hiep", email: "h@x.com" };
open.name = "An";              // ✅ hợp lệ


type MyPick<T, K extends keyof T> = {
    [P in K]: T[P];
};

type UserPreview = MyPick<User, "id" | "name">;
// { id: number; name: string }


const draft: OptionalUser = { name: "Hiep" };
const form: UserForm = { id: "1", name: "Hiep", email: "h@x.com" };
const locked: ReadonlyAll<User> = { id: 1, name: "Hiep", email: "h@x.com" };
const maybe: UserWithNull = { id: 1, name: null, email: "h@x.com" };


console.log(draft, form, locked, maybe);

