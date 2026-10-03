function identity<T>(arg: T): T {
    return arg;
}

const num = identity<number>(42);
const str = identity<string>("Hello");

console.log(num); // 42
console.log(str); // Hello


function first<T>(arr: T[]): T | undefined {
    return arr[0];
}

const numbers = [1, 2, 3];
const firstNumber = first<number>(numbers);
console.log(firstNumber); // Output: 1

const strings = ["a", "b", "c"];
const firstString = first<string>(strings);
console.log(firstString); // Output: a


function pair<A, B>(first: A, second: B): [A, B] {
    return [first, second];
}

const p1 = pair<number, string>(1, "one");
console.log(p1); // Output: [1, "one"]

const p2 = pair<boolean, number>(true, 42);
console.log(p2); // Output: [true, 42]


interface ApiResponse<T> {
    success: boolean;
    data: T;
}

interface User {
    id: number;
    name: string;
}

const userResponse: ApiResponse<User> = {
    success: true,
    data: { id: 1, name: "Alice" }
};
console.log(userResponse); // Output: { success: true, data: { id: 1, name: "Alice" } }

const listResponse: ApiResponse<User[]> = {
    success: true,
    data: [
        { id: 1, name: "Alice" },
        { id: 2, name: "Bob" }
    ]
};
console.log(listResponse); // Output: { success: true, data: [ { id: 1, name: "Alice" }, { id: 2, name: "Bob" } ] }


// generic class
class Stack<T> {
    private items: T[] = [];

    push(item: T): void {
        this.items.push(item);
    }

    pop(): T | undefined {
        return this.items.pop();
    }

    peek(): T | undefined {
        return this.items[this.items.length - 1];
    }

    isEmpty(): boolean {
        return this.items.length === 0;
    }
}

const numberStack = new Stack<number>();
numberStack.push(1);
numberStack.push(2);
console.log(numberStack.pop()); // Output: 2
console.log(numberStack.peek()); // Output: 1

const stringStack = new Stack<string>();
stringStack.push("a");
stringStack.push("b");
console.log(stringStack.pop()); // Output: b
console.log(stringStack.peek()); // Output: a
console.log(stringStack.pop()); // Output: b
console.log(stringStack.pop()); // Output: b