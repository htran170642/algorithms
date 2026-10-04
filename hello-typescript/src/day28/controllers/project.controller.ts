import { CreateProjectSchema, UpdateProjectSchema } from "../models/project.schema.js";
import type { Project } from "../models/project.js";
import type { User } from "../models/user.js";
import type { ProjectService } from "../services/project.service.js";
import type { ApiResponse } from "../types/common.js";
import type { HttpResponse } from "../types/http.js";
import { parseBody } from "../utils/parse-body.js";
import { respond } from "../utils/respond.js";

export class ProjectController {
    constructor(private readonly projects: ProjectService) {}

    async list(user: User): Promise<HttpResponse<ApiResponse<Project[]>>> {
        return respond(200, await this.projects.list(user));
    }

    async get(user: User, id: number): Promise<HttpResponse<ApiResponse<Project>>> {
        return respond(200, await this.projects.getOwned(user, id));
    }

    async create(user: User, body: unknown): Promise<HttpResponse<ApiResponse<Project>>> {
        const input = parseBody(CreateProjectSchema, body);
        return respond(201, await this.projects.create(user, input));
    }

    async update(user: User, id: number, body: unknown): Promise<HttpResponse<ApiResponse<Project>>> {
        const changes = parseBody(UpdateProjectSchema, body);
        return respond(200, await this.projects.update(user, id, changes));
    }

    async remove(user: User, id: number): Promise<HttpResponse> {
        await this.projects.remove(user, id);
        return { status: 204 };
    }
}
