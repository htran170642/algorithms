import type { User } from "../models/user.js";
import { InMemoryRepository } from "./repository.js";

export class UserRepository extends InMemoryRepository<User> {
    async findByEmail(email: string): Promise<User | null> {
        return this.items.find(u => u.email === email) ?? null;
    }
}

