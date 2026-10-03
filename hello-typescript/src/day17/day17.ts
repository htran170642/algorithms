interface User {
    id: number;
    name: string;
}

interface Post {
    id: number;
    userId: number;
    title: string;
}

const usersDb: User[] = [
    { id: 1, name: "Alice" },
    { id: 2, name: "Bob" },
    { id: 3, name: "Charlie" },
];

const postsDb: Post[] = [
    { id: 1, userId: 1, title: "Post 1 by Alice" },
    { id: 2, userId: 1, title: "Post 2 by Alice" },
    { id: 3, userId: 2, title: "Post 1 by Bob" },
];

function sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

async function getUser(id: number): Promise<User | undefined> {
    await sleep(1000); // Giả lập delay 1 giây
    return usersDb.find((user) => user.id === id);
}

async function getUsers(): Promise<User[]> {
    await sleep(1000); // Giả lập delay 1 giây
    return usersDb;
}

async function getPostsByUserId(userId: number): Promise<Post[]> {
    await sleep(1000); // Giả lập delay 1 giây
    return postsDb.filter((post) => post.userId === userId);
}

async function saveLog(message: string): Promise<void> {
    await sleep(500); // Giả lập delay 0.5 giây
    console.log("Log saved:", message);
}
async function getUserOrFail(id: number): Promise<User> {
    await sleep(1000); // Giả lập delay 1 giây
    const user = usersDb.find((user) => user.id === id);
    if (!user) {
        throw new Error(`User with id ${id} not found`);
    }
    return user;
}

async function sequential(): Promise<void> {
    console.log("=== Sequential ===");
    const startTime = Date.now();
    const user1 = await getUser(1);
    const posts1 = await getPostsByUserId(1);
    console.log("User 1:", user1);
    console.log("Posts by User 1:", posts1);
    console.log("Time taken (ms):", Date.now() - startTime);
}

async function parallel(): Promise<void> {
    console.log("=== Parallel ===");
    const startTime = Date.now();
    const userPromise = getUser(1);
    const postsPromise = getPostsByUserId(1);
    const [user1, posts1] = await Promise.all([userPromise, postsPromise]);
    console.log("User 1:", user1);
    console.log("Posts by User 1:", posts1);
    console.log("Time taken (ms):", Date.now() - startTime);
}

async function allFails(): Promise<void> {
    try {
        await Promise.all([getUserOrFail(1), getUserOrFail(999)]); // 999 không tồn tại
    } catch (error) {
        if (error instanceof Error) {
            console.log("Error:", error.message);
        }
    }
}

async function allSettled(): Promise<void> {
    const results = await Promise.allSettled([getUserOrFail(1), getUserOrFail(999)]);

    for (const result of results) {
        if (result.status === "fulfilled") {
            console.log("Fulfilled:", result.value);
        } else {
            console.log("Rejected:", result.reason.message);
        }
    }
}


await sequential();
await parallel();
await allFails();
await allSettled();