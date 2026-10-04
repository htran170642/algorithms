import { UserRepository } from "./repositories/user.repository.js";
import { ProjectRepository } from "./repositories/project.repository.js";
import { TaskRepository } from "./repositories/task.repository.js";
import { ProjectService } from "./services/project.service.js";
import { TaskService } from "./services/task.service.js";
import { AppError } from "./errors.js";

const users = new UserRepository();
const projects = new ProjectRepository();
const tasks = new TaskRepository();
const projectService = new ProjectService(projects, tasks);
const taskService = new TaskService(tasks, projectService);

const make = (name: string) =>
    users.create({ name, email: `${name}@example.com`, passwordHash: "x", createdAt: new Date() });
const alice = await make("alice");
const bob = await make("bob");

async function expectError(label: string, run: () => Promise<unknown>): Promise<void> {
    try {
        await run();
        console.log(label, "→ KHÔNG lỗi (sai!)");
    } catch (error) {
        if (error instanceof AppError) {
            console.log(label, "→", error.statusCode, error.message);
        } else {
            throw error;
        }
    }
}

const p = await projectService.create(alice, { name: "Học TypeScript" });
console.log("project:", p.id, p.name, "| owner", p.ownerId);

const t1 = await taskService.create(alice, p.id, { title: "Day 28", status: "todo" });
const t2 = await taskService.create(alice, p.id, { title: "Day 27", description: "đọc code", status: "todo" });
console.log("task mới:", t1.id, t1.status, "| description:", t1.description, "|", t2.id, t2.description);

// PATCH {} không được đổi status về "todo"
await taskService.update(alice, p.id, t1.id, { status: "in_progress" });
const afterEmpty = await taskService.update(alice, p.id, t1.id, {});
console.log("PATCH rỗng giữ nguyên status:", afterEmpty.status);

console.log("lọc in_progress:", (await taskService.list(alice, p.id, "in_progress")).map(t => t.title));
console.log("project của alice:", (await projectService.list(alice)).length, "| của bob:", (await projectService.list(bob)).length);

await expectError("bob xem project của alice", () => taskService.list(bob, p.id));
await expectError("bob tạo task trong project của alice", () => taskService.create(bob, p.id, { title: "hack", status: "todo" }));
await expectError("project không tồn tại", () => taskService.list(alice, 999));
await expectError("task không tồn tại", () => taskService.get(alice, p.id, 999));

const p2 = await projectService.create(alice, { name: "Project 2" });
await expectError("task của project 1 qua đường dẫn project 2", () => taskService.get(alice, p2.id, t1.id));

await projectService.remove(alice, p.id);
console.log("sau khi xoá project, còn task:", (await tasks.findAll()).length);
