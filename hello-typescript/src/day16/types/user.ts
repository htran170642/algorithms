export interface Entity {
    id: number;
}

export interface User extends Entity {
    name: string;
    email: string;
}

export type CreateUserInput = Omit<User, "id">;
