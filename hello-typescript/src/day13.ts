interface User {
    id: number;
    name: string;
    email: string;
    age: number;
}

const user: User = {
    id: 1,
    name: "Alice",
    email: "alice@example.com",
    age: 30
};

type UserKey = keyof User; // "id" | "name" | "email" | "age"

const k1: UserKey = "name"; // valid
//const k2: UserKey = "password"; // Error

type UserId = User["id"]; // number
const userId: UserId = user.id; // 1

type UserName = User["name"]; // string
const userName: UserName = user.name; // "Alice"

const defaultConfig = {
    host: "localhost",
    port: 8080,
    useSSL: false
}

type Config = typeof defaultConfig; // { host: string; port: number; useSSL: boolean }
type ConfigKeys = keyof Config; // "host" | "port" | "useSSL"
// console.log(ConfigKeys); // "localhost"

const ROLES = ["admin", "user", "guest"] as const;
type Role = (typeof ROLES)[number]; // "admin" | "user" | "guest"

const role: Role = "admin"; // valid
// const invalidRole: Role = "superadmin"; // Error

for (const r of ROLES) {
    console.log(r); // "admin", "user", "guest"
}