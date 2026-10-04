import type { CreateTaskInput, UpdateTaskInput } from "../models/task.schema.js";
import type { Task, TaskStatus } from "../models/task.js";
import type { User } from "../models/user.js";
import type { TaskRepository } from "../repositories/task.repository.js";
import type { ProjectService } from "./project.service.js";
import { NotFoundError } from "../errors.js";

export class TaskService {
    constructor(
        private readonly tasks: TaskRepository,
        private readonly projectService: ProjectService,
    ) {}

    async list(user: User, projectId: number, status?: TaskStatus): Promise<Task[]> {
        await this.projectService.getOwned(user, projectId);
        const all = await this.tasks.findByProject(projectId);
        return status === undefined ? all : all.filter(t => t.status === status);
    }

    async get(user: User, projectId: number, taskId: number): Promise<Task> {
        await this.projectService.getOwned(user, projectId);
        const task = await this.tasks.findById(taskId);
        // Task phải thuộc đúng project trong đường dẫn, nếu không coi như không tồn tại
        if (!task || task.projectId !== projectId) {
            throw new NotFoundError(`Không tìm thấy task id=${taskId} trong project ${projectId}`);
        }
        return task;
    }

    async create(user: User, projectId: number, input: CreateTaskInput): Promise<Task> {
        await this.projectService.getOwned(user, projectId);
        return this.tasks.create({
            projectId,
            title: input.title,
            description: input.description,
            status: input.status,
            createdAt: new Date(),
        });
    }

    async update(user: User, projectId: number, taskId: number, changes: UpdateTaskInput): Promise<Task> {
        await this.get(user, projectId, taskId);
        const updated = await this.tasks.update(taskId, changes);
        if (!updated) {
            throw new NotFoundError(`Không tìm thấy task id=${taskId}`);
        }
        return updated;
    }

    async remove(user: User, projectId: number, taskId: number): Promise<void> {
        await this.get(user, projectId, taskId);
        await this.tasks.delete(taskId);
    }
}
