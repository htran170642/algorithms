// typeof: discriminate primitive types

function printValue(value: string | number | boolean): void {
    if (typeof value === "string") {
        console.log(`Value is a string: ${value}`);
    } else if (typeof value === "number") {
        console.log(`Value is a number: ${value}`);
    } else if (typeof value === "boolean") {
        console.log(`Value is a boolean: ${value}`);
    } else {
        console.log("Unknown type");
    }
}

printValue("Hello"); // Output: Value is a string: Hello
printValue(42);      // Output: Value is a number: 42
printValue(true);    // Output: Value is a boolean: true

// truthliness

function greet(name: string | null | undefined): string {
    if (!name) {
        return "Hello, Guest!";
    }
    return `Hello, ${name}!`;
}

console.log(greet("Alice")); // Output: Hello, Alice!
console.log(greet(null));    // Output: Hello, Guest!
console.log(greet(undefined)); // Output: Hello, Guest!
console.log(greet("")); // Output: Hello, Guest!


// instanceof: discriminate class instances

function formatDate(input: Date | string): string {
    if (input instanceof Date) {
        return input.toISOString();
    } else {
        const date = new Date(input);
        return date.toISOString();
    }
}

console.log(formatDate(new Date())); // Output: current date in ISO format
console.log(formatDate("2024-01-01")); // Output: 2024-01-01T00:00:00.000Z

// check attribute in object

interface Dog {
    bark(): void;
}

interface Cat {
    meow(): void;
}

function speak(animal: Dog | Cat): void {
    if ("bark" in animal) {
        animal.bark();
    } else {
        animal.meow();
    }
}

const dog: Dog = { bark: () => console.log("Woof!") };
const cat: Cat = { meow: () => console.log("Meow!") };

speak(dog); // Output: Woof!
speak(cat); // Output: Meow!