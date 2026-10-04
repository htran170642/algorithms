import { z } from "zod";

export const CreateProjectSchema = z.object({
    name: z.string().trim().min(1, "name không được rỗng"),
});

export const UpdateProjectSchema = CreateProjectSchema.partial();

export type CreateProjectInput = z.infer<typeof CreateProjectSchema>;
export type UpdateProjectInput = z.infer<typeof UpdateProjectSchema>;
