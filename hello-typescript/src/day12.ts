interface User {
    id: number;
    name: string;
    email: string;
    age?: number;
    password?: string;
}

// Omit
type CreateUserInput = Omit<User, "id">;

// no password
type PublicUser = Omit<User, "password">;

// Pick id name, email
type UserSummary = Pick<User, "id" | "name" | "email">;

// Partial
type UpdateUserInput = Partial<CreateUserInput>;

// readonly
type ReadonlyUser = Readonly<User>;

// Object
type UserMap = Record<number, User>;

type Role = "admin" | "user" | "guest";
const permissions: Record<Role, string[]> = {
    admin: ["read", "write", "delete"],
    user: ["read", "write"],
    guest: ["read"],
};


interface AppConfig {
    host?: string;
    port?: number;
    useSSL?: boolean;
}

function connect(config: AppConfig): Required<AppConfig>{
    const host = config.host ?? "localhost";
    const port = config.port ?? 80;
    const useSSL = config.useSSL ?? false;

    return { host, port, useSSL };
}

console.log(connect({}));
console.log(connect({ host: "example.com", port: 443, useSSL: true }));


// use all

const users: User[] = [];
let nextId = 1;

function createUser(input: CreateUserInput): User {
    const user: User = { id: nextId++, ...input };
    users.push(user);
    return user;
}

function updateUser(id: number, input: UpdateUserInput): User | undefined {
    const user = users.find(u => u.id === id);
    if (user) {
        Object.assign(user, input);
        return user;
    }
    return undefined;
}

function toPublicUser(user: User): PublicUser {
    const { password, ...publicUser } = user;
    return publicUser;
}

function buildUserMap(users: User[]): UserMap {
    const userMap: UserMap = {};
    for (const user of users) {
        userMap[user.id] = user;
    }
    return userMap;
}

// Example usage
const u1 = createUser({ name: "Alice", email: "alice@example.com" });
const u2 = createUser({ name: "Bob", email: "bob@example.com" });

console.log(toPublicUser(u2));
console.log(buildUserMap([u1, u2]));