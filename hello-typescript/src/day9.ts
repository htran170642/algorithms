type Shape =
    | { kind: "circle"; radius: number }
    | { kind: "square"; size: number }
    | { kind: "rectangle"; width: number; height: number };


function area(shape: Shape): number {
    switch (shape.kind) {
        case "circle":
            return Math.PI * shape.radius ** 2;
        case "square":
            return shape.size ** 2;
        case "rectangle":
            return shape.width * shape.height;
        default:
            throw new Error("Unknown shape");
    }
}


const shapes: Shape[] = [
    { kind: "circle", radius: 5 },
    { kind: "square", size: 4 },
    { kind: "rectangle", width: 3, height: 6 },
];

for (const shape of shapes) {
    console.log(`Area of ${shape.kind}: ${area(shape)}`);
}

type Result = 
    | { success: true; data: string }
    | { success: false; error: string };

function handleAge(age: number): Result {
    if (age < 0) {
        return { success: false, error: "Age cannot be negative" };
    }
    return { success: true, data: `Age is ${age}` };
}

const r1 = handleAge(25);
const r2 = handleAge(-5);

for (const r of [r1, r2]) {
    if (r.success) {
        console.log(r.data);
    } else {
        console.error(r.error);
    }
}


// Generic type

type ApiResult<T> =
    | { success: true; data: T }
    | { success: false; error: string };

interface User {
    id: number;
    name: string;
}

const users: User[] = [
    { id: 1, name: "Alice" },
    { id: 2, name: "Bob" },
];

function findUser(id: number): ApiResult<User> {
    if (id < 0) {
        return { success: false, error: "ID cannot be negative" };
    }

    const user = users.find(u => u.id === id);
    if (user) {
        return { success: true, data: user };
    }
    return { success: false, error: "User not found" };
}

for (const id of [1, 3, -1]) {
    const result = findUser(id);
    if (result.success) {
        console.log(`Found user: ${result.data.name}`);
    } else {
        console.error(result.error);
    }
}


// const bad: Shape = { kind: "circle", size: 3 };
//  → 'size' does not exist in type '{ kind: "circle"; radius: number }'.

