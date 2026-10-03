import { createUser, listUsers } from "./services/user.service.js";

// process là object của Node.js → cần @types/node để TS biết nó có gì
const appName = process.env["APP_NAME"] ?? "User CLI";
const args = process.argv.slice(2);

console.log(`=== ${appName} ===`);
console.log("Tham số dòng lệnh:", args);

createUser({ name: "Hiep", email: "hiep@example.com" });
createUser({ name: "An", email: "an@example.com" });

// Mỗi tham số có dạng "tên:email"
for (const arg of args) {
    const [name, email] = arg.split(":");
    if (name && email) {
        try {
            createUser({ name, email });
        } catch (error) {
            if (error instanceof Error) {
                console.log("Lỗi:", error.message);
            }
        }
    }
}

console.table(listUsers());
