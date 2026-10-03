import { createUser, getUsers } from "./services/user.service.js";
import * as validate from "./utils/validate.js";    // namespace import: gom tất cả vào một object
import log from "./utils/logger.js";

createUser({ name: "Hiep", email: "hiep@example.com" });
createUser({ name: "An", email: "an@example.com" });

log(`Tổng số user: ${getUsers().length}`);
console.log(getUsers());

console.log(validate.isValidEmail("abc"));   // false

try {
    createUser({ name: "", email: "x@x.com" });
} catch (error) {
    if (error instanceof Error) {
        log(`Lỗi: ${error.message}`);
    }
}
