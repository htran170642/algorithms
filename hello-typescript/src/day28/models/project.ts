import type { Entity } from "../types/common.js";

export interface Project extends Entity {
    name: string;
    ownerId: number;
    createdAt: Date;
}
