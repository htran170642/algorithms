import { z } from "zod";
import { TASK_STATUSES } from "./task.js";

const TaskFieldsSchema = z.object({
    title: z.string().trim().min(1, "title không được rỗng"),
    description: z.string().optional(),
    status: z.enum(TASK_STATUSES),
});

// Tạo mới: status mặc định là "todo"
export const CreateTaskSchema = TaskFieldsSchema.extend({
    status: z.enum(TASK_STATUSES).default("todo"),
});

// Cập nhật: mọi field tùy chọn, và KHÔNG có giá trị mặc định
export const UpdateTaskSchema = TaskFieldsSchema.partial();

export type CreateTaskInput = z.infer<typeof CreateTaskSchema>;
export type UpdateTaskInput = z.infer<typeof UpdateTaskSchema>;
