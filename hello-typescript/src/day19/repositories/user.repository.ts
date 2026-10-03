import type { CreateUserInput, UpdateUserInput, User } from "../models/user.js";

// Lưu trong bộ nhớ, nhưng mọi method đều async:
// sau này đổi sang database thật thì service KHÔNG phải sửa gì.
export class UserRepository {
    private users: User[] = [];
    private nextId = 1;

    async findAll(): Promise<User[]> {
        return [...this.users];
    }

    async findById(id: number): Promise<User | null> {
        return this.users.find(u => u.id === id) ?? null;
    }

    async findByEmail(email: string): Promise<User | null> {
        return this.users.find(u => u.email === email) ?? null;
    }

    async create(data: CreateUserInput): Promise<User> {
        const user: User = { id: this.nextId++, ...data };
        this.users.push(user);
        return user;
    }

    async update(id: number, changes: UpdateUserInput): Promise<User | null> {
        const user = await this.findById(id);
        if (!user) {
            return null;
        }
        Object.assign(user, changes);
        return user;
    }

    async delete(id: number): Promise<boolean> {
        const before = this.users.length;
        this.users = this.users.filter(u => u.id !== id);
        return this.users.length < before;
    }
}
