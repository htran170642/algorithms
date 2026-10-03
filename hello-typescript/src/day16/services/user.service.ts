import type { CreateUserInput, User } from "../types/user.js";
import { Repository } from "../repositories/repository.js";
import { isValidEmail } from "../utils/validate.js";

const repo = new Repository<User>();

export function createUser(input: CreateUserInput): User {
    if (!isValidEmail(input.email)) {
        throw new Error(`Email không hợp lệ: ${input.email}`);
    }
    return repo.create(input);
}

export function listUsers(): readonly User[] {
    return repo.findAll();
}
