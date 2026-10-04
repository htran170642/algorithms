import { UserRepository } from "./repositories/user.repository.js";
import { ProjectRepository } from "./repositories/project.repository.js";
import { TaskRepository } from "./repositories/task.repository.js";

const users = new UserRepository();
const projects = new ProjectRepository();
const tasks = new TaskRepository();

const user = await users.create({
    name: "Hiep",
    email: "hiep@example.com",
    passwordHash: "fake",
    createdAt: new Date(),
});

const project = await projects.create({
    name: "Học TypeScript",
    ownerId: user.id,
    createdAt: new Date(),
});

const t1 = await tasks.create({
    projectId: project.id,
    title: "Làm Day 28",
    status: "todo",
    createdAt: new Date(),
});
await tasks.create({
    projectId: project.id,
    title: "Đọc code thật",
    description: "Day 27",
    status: "todo",
    createdAt: new Date(),
});

await tasks.update(t1.id, { status: "in_progress" });

console.log("user:", await users.findByEmail("hiep@example.com"));
console.log("tasks của project:", await tasks.findByProject(project.id));

await tasks.delete(t1.id);
console.log("sau khi xoá:", (await tasks.findAll()).length);
