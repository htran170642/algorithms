// ===== Mẫu 1: Result type =====
type Result<T> =
    | { success: true; data: T }
    | { success: false; error: string };

interface User {
    id: number;
    name: string;
}

// ===== Mẫu 2: Repository interface =====
interface Repository<T> {
    findById(id: number): Promise<T | null>;
    findAll(): Promise<T[]>;
    create(data: T): Promise<T>;
}

class InMemoryUserRepository implements Repository<User> {
    private users: User[] = [];

    async findById(id: number): Promise<User | null> {
        return this.users.find(u => u.id === id) ?? null;
    }

    async findAll(): Promise<User[]> {
        return [...this.users];
    }

    async create(data: User): Promise<User> {
        this.users.push(data);
        return data;
    }
}

// ===== Mẫu 3: Dependency Injection =====
class UserService {
    constructor(private repository: Repository<User>) {}

    async getUser(id: number): Promise<Result<User>> {
        const user = await this.repository.findById(id);

        if (!user) {
            return { success: false, error: "User not found" };
        }
        return { success: true, data: user };
    }
}

// ===== Lắp ráp và dùng =====
const repo = new InMemoryUserRepository();
const service = new UserService(repo);

await repo.create({ id: 1, name: "Hiep" });

for (const id of [1, 99]) {
    const result = await service.getUser(id);

    if (result.success) {
        console.log("OK  ", result.data.name);
    } else {
        console.log("FAIL", result.error);
    }
}