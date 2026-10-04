import { z } from "zod";

export const CreateUserSchema = z.object(
    {
        name: z.string().trim().min(1, "can not be empty"),
        email: z.email("invalid email"),
        age: z.number().int().min(0).max(150),
    }
);

export const UpdateUserSchema = CreateUserSchema.partial();

export type CreateUserInput = z.infer<typeof CreateUserSchema>;
export type UpdateUserInput = z.infer<typeof UpdateUserSchema>;
