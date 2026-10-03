function add(a: number, b: number): number {
    return a+b;
}

// add(2, "3");
// add(2);

function greet(name:string, greeting?: string) : string {
    return `${greeting ?? "Heello"} ${name}`;
}

function power(base: number, exp: number = 2) : number {
    return base ** exp;
}

console.log(greet("Hiep"));
console.log(greet("Hiep", "Hi"));
console.log(power(3));
console.log(power(3, 3));


const multiply = (a: number, b: number): number => a*b;
console.log(multiply(4,5));

type Calculator = (a: number, b: number) => number;

const sum: Calculator = (a, b) => a + b;
const multi: Calculator = (a,b) => a*b;

const add1: Calculator = (a, b) => a + b;
const subtrac: Calculator = (a,b) => a -b;
const multiply1: Calculator = (a, b) => a * b;
const divide: Calculator = (a,b) => {
    if (b === 0) {
        throw new Error("Cannot divide by zero");
    }
    return a / b;
};


console.log(sum(2, 3));
console.log(multi(2, 3));

try {
    console.log(divide(10, 0));
} catch (error) {
    console.log("Lỗi: chia cho 0");
}
