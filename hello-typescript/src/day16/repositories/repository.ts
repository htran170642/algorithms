import type { Entity } from "../types/user.js";

export class Repository<T extends Entity> {
    private items: T[] = [];
    private nextId = 1;

    create(data: Omit<T, "id">): T {
        const item = { id: this.nextId++, ...data } as T;
        this.items.push(item);
        return item;
    }

    findById(id: number): T | undefined {
        return this.items.find(item => item.id === id);
    }

    findAll(): readonly T[] {
        return this.items;
    }
}
