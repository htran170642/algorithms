// === Model ===

interface User {
    id: number;
    name: string;
    email: string;
    age?: number;
}

// type CreateUser = Omit<User, "id">;

type CreateUserInput = {
    name: string;
    email: string;
    age: number;
}

// === Storage ===

const users: User[] = []
let nextId = 1;



// === validate ===

function validateUser(input: CreateUserInput): string[] {
    const errors: string[] = [];
    
    if (!input.name || input.name.trim() === "") {
        errors.push("Name is required.");
    }

    if (!input.email.includes("@")) {
        errors.push("Email must be valid.");
    }

    if (input.age < 0 || input.age > 120) {
        errors.push("Age must be between 0 and 120.");
    }

    return errors;
}

// === CRUD ===
function createUser(input: CreateUserInput): User {
    const errors = validateUser(input);

    if (errors.length > 0) {
        throw new Error(`Validation failed: ${errors.join(", ")}`);
    }

    const user: User = {id: nextId++, ...input};
    users.push(user);
    return user;
}

function getUser(id: number): User | undefined {
    return users.find(u => u.id === id);
}

function getUsers(): readonly User[] {
    return users;
}

function deleteUser(id: number): boolean {
    const index = users.findIndex(u => u.id === id);
    if (index !== -1) {
        users.splice(index, 1);
        return true;
    }
    return false;
}

/// === Example Usage ===
createUser({name: "Alice", email: "alice@example.com", age: 25});
createUser({name: "Bob", email: "bob@example.com", age: 30});
console.log(getUsers());

const found = getUser(1);
if (found) {
    console.log(`Found user: ${found.name}`);
} else {
    console.log("User not found.");
}

console.log(`Deleting user with id 1: ${deleteUser(1)}`);
console.log(getUsers());

// Input sai → bị validate chặn
try {
    createUser({ name: "", email: "abc", age: -5 });
} catch (error) {
    // error có kiểu unknown → phải kiểm tra trước khi dùng .message (học kỹ ở Day 18)
    if (error instanceof Error) {
        console.log("Error:", error.message);
    }
}

console.log(getUser(99)?.email);