const userName: string = "Hiep"
const age: number = 30;
const isActive: boolean = true;

console.log(userName, age, isActive)

const nothing: null = null;
const notSet: undefined = undefined;

function sayHi() : void {
    console.log("Hi")
}

const scores: number[] = [1,2,3];
const names: Array<string> = ["Alice", "Bob"];

const user: [string, number] = ["Hiep", 30];

scores.push(4);
// user[0] = 90;
// const third = user[2];

let anything: any = "hello";
anything = 42;
anything = true;
// anything.foo.bar();

let input: unknown = "hello";
// input.toUpperCase();

if (typeof input === "string") {
    console.log(input.toUpperCase());
}

function fail(message: string): never {
    throw new Error(message);
}

// fail("hi");

