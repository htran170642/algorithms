import type { Project } from "../models/project.js";
import { InMemoryRepository } from "./repository.js";

export class ProjectRepository extends InMemoryRepository<Project> {
    async findByOwner(ownerId: number): Promise<Project[]> {
        return this.items.filter(p => p.ownerId === ownerId);
    }
}
