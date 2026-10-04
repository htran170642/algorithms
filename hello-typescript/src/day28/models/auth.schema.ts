import { z } from "zod";

export const RegisterSchema = z.object({
    name: z.string().trim().min(1, "name không được rỗng"),
    email: z.email("email không hợp lệ"),
    password: z.string().min(8, "password tối thiểu 8 ký tự"),
});

export const LoginSchema = z.object({
    email: z.email("email không hợp lệ"),
    password: z.string().min(1, "password không được rỗng"),
});

export type RegisterInput = z.infer<typeof RegisterSchema>;
export type LoginInput = z.infer<typeof LoginSchema>;
