export interface User {
    id: number;
    name: string;
    email: string;
    age: number;
}

export type CreateUserInput = Omit<User, "id">
export type UpdateUserInput = Partial<CreateUserInput>