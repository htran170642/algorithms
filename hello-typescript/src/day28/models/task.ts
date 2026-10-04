import type { Entity } from "../types/common.js";

export const TASK_STATUSES = ["todo", "in_progress", "done"] as const;
export type TaskStatus = (typeof TASK_STATUSES)[number];

export interface Task extends Entity {
    projectId: number;
    title: string;
    description?: string | undefined;
    status: TaskStatus;
    createdAt: Date;
}
