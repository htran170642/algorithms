import type { User, CreateUserInput } from "../models/user.js";   // chỉ import KIỂU
import { isValidEmail, isValidName } from "../utils/validate.js";  // named import
import log from "../utils/logger.js";                              // default import

const users: User[] = [];      // không export → dữ liệu được "giấu" trong module
let nextId = 1;

export function createUser(input: CreateUserInput): User {
    if (!isValidName(input.name)) {
        throw new Error("Tên không hợp lệ");
    }
    if (!isValidEmail(input.email)) {
        throw new Error("Email không hợp lệ");
    }

    const user: User = { id: nextId++, ...input };
    users.push(user);
    log(`Đã tạo user ${user.name}`);
    return user;
}

export function getUsers(): readonly User[] {
    return users;
}
