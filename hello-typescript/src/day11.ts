// constraint

function getLength<T extends { length: number }>(arg: T): number {
    return arg.length;
}

console.log(getLength("Hello")); // Output: 5
console.log(getLength([1, 2, 3])); // Output: 3
console.log(getLength({ length: 10 })); // Output: 10


// constraint with interface

interface HasId {
    id: number;
}

function getId<T extends HasId>(obj: T): number {
    return obj.id;
}

const user = { id: 1, name: "Alice" };
console.log(getId(user)); // Output: 1

const product = { id: 2, name: "Laptop", price: 999.99 };
console.log(getId(product)); // Output: 2


// no generic: return hasID -> loss name, email

function findByIdPlain(items: HasId[], id: number): HasId | undefined {
    return items.find(item => item.id === id);
}


function findById<T extends HasId>(items: T[], id: number): T | undefined {
    return items.find(item => item.id === id);
}

const users = [
    { id: 1, name: "Alice", email: "alice@example.com" },
    { id: 2, name: "Bob", email: "bob@example.com" }
];

const p1 = findByIdPlain(users, 1);
console.log(p1?.id); // Output: "Alice"

const p2 = findById(users, 1);
console.log(p2?.name); // Output: "Alice"