import type { Entity, Patch } from "../types/common.js";

export interface Repository<T extends Entity> {
    findById(id: number): Promise<T | null>;
    findAll(): Promise<T[]>;
    create(data: Omit<T, "id">): Promise<T>;
    update(id: number, changes: Partial<Omit<T, "id">>): Promise<T | null>;
    delete(id: number): Promise<void>;
}

export class InMemoryRepository<T extends Entity> implements Repository<T> {
    protected items: T[] = [];
    private nextId = 1;

    async findById(id: number): Promise<T | null> {
        return this.items.find(item => item.id === id) ?? null;
    }

    async findAll(): Promise<T[]> {
        return [...this.items];
    }

    async create(data: Omit<T, "id">): Promise<T> {
        const item = { ...data, id: this.nextId++ } as T;
        this.items.push(item);
        return item;
    }

    async update(id: number, changes: Patch<Omit<T, "id">>): Promise<T | null> {
        const item = await this.findById(id);
        if (!item) {
            return null;
        }
        Object.assign(item, changes);
        return item;
    }

    async delete(id: number): Promise<void> {
        this.items = this.items.filter(item => item.id !== id);
    }
}
