import type { CreateUserInput, UpdateUserInput, User } from "../models/user.js";
import type { UserRepository } from "../repositories/user.repository.js";
import { ConflictError, NotFoundError } from "../errors.js";

// Service: logic nghiệp vụ. Không biết gì về HTTP.
export class UserService {
    private readonly repo: UserRepository;

    constructor(repo: UserRepository) {
        this.repo = repo;
    }

    async list(): Promise<User[]> {
        return this.repo.findAll();
    }

    async getById(id: number): Promise<User> {
        const user = await this.repo.findById(id);
        if (!user) {
            throw new NotFoundError(`Không tìm thấy user id=${id}`);
        }
        return user;
    }

    async create(input: CreateUserInput): Promise<User> {
        if (await this.repo.findByEmail(input.email)) {
            throw new ConflictError(`Email đã tồn tại: ${input.email}`);
        }
        return this.repo.create(input);
    }

    async update(id: number, changes: UpdateUserInput): Promise<User> {
        if (changes.email !== undefined) {
            const other = await this.repo.findByEmail(changes.email);
            if (other && other.id !== id) {
                throw new ConflictError(`Email đã tồn tại: ${changes.email}`);
            }
        }
        const user = await this.repo.update(id, changes);
        if (!user) {
            throw new NotFoundError(`Không tìm thấy user id=${id}`);
        }
        return user;
    }

    async remove(id: number): Promise<void> {
        const deleted = await this.repo.delete(id);
        if (!deleted) {
            throw new NotFoundError(`Không tìm thấy user id=${id}`);
        }
    }
}
