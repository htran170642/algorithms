import type { CreateProjectInput, UpdateProjectInput } from "../models/project.schema.js";
import type { Project } from "../models/project.js";
import type { User } from "../models/user.js";
import type { ProjectRepository } from "../repositories/project.repository.js";
import type { TaskRepository } from "../repositories/task.repository.js";
import { ForbiddenError, NotFoundError } from "../errors.js";

export class ProjectService {
    constructor(
        private readonly projects: ProjectRepository,
        private readonly tasks: TaskRepository,
    ) {}

    async list(user: User): Promise<Project[]> {
        return this.projects.findByOwner(user.id);
    }

    // Lấy project và chắc chắn nó thuộc về user. TaskService cũng dùng hàm này.
    async getOwned(user: User, projectId: number): Promise<Project> {
        const project = await this.projects.findById(projectId);
        if (!project) {
            throw new NotFoundError(`Không tìm thấy project id=${projectId}`);
        }
        if (project.ownerId !== user.id) {
            throw new ForbiddenError("Bạn không có quyền với project này");
        }
        return project;
    }

    async create(user: User, input: CreateProjectInput): Promise<Project> {
        return this.projects.create({
            name: input.name,
            ownerId: user.id,
            createdAt: new Date(),
        });
    }

    async update(user: User, projectId: number, changes: UpdateProjectInput): Promise<Project> {
        await this.getOwned(user, projectId);
        const updated = await this.projects.update(projectId, changes);
        if (!updated) {
            throw new NotFoundError(`Không tìm thấy project id=${projectId}`);
        }
        return updated;
    }

    async remove(user: User, projectId: number): Promise<void> {
        await this.getOwned(user, projectId);
        // Xoá project thì xoá luôn các task của nó
        for (const task of await this.tasks.findByProject(projectId)) {
            await this.tasks.delete(task.id);
        }
        await this.projects.delete(projectId);
    }
}
