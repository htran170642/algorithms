import type { Entity } from "../types/common.js";

export interface User extends Entity {
    name: string;
    email: string;
    passwordHash: string;
    createdAt: Date;
}

// Kiểu trả về cho client: KHÔNG bao giờ lộ passwordHash
export type PublicUser = Omit<User, "passwordHash">;

export function toPublicUser(user: User): PublicUser {
    const { passwordHash: _passwordHash, ...rest } = user;
    return rest;
}

