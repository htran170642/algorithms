import type { z } from "zod";
import type { CreateUserSchema, UpdateUserSchema } from "./user.schema.js";

export interface User {
    id: number;
    name: string;
    email: string;
    age: number;
}

export type CreateUserInput = z.infer<typeof CreateUserSchema>;
export type UpdateUserInput = z.infer<typeof UpdateUserSchema>;
