import type { Task } from "../models/task.js";
import { InMemoryRepository } from "./repository.js";

export class TaskRepository extends InMemoryRepository<Task> {
    async findByProject(projectId: number): Promise<Task[]> {
        return this.items.filter(t => t.projectId === projectId);
    }
}
