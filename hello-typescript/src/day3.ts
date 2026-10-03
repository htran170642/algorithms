type User = {
    id: number;
    name: string;
    age?: number;
    readonly email: string;
};

const user1: User = {
    id: 1,
    name: "abc",
    email: "test@gmail.com"
}

// user1.email = "test3/";
// console.log(user1)


// const user2: User = {
//     id: 2,
//     name: "An",
// };

user1.age = 30;

type Scores = {
    [subject: string]: number;
};

const myScores: Scores = {
    math: 90,
    english: 85,
};

myScores.physics = 70;




type Product = {
    id: number;
    name: string;
    price: number;
    description?: string;
};

const products: Product[] = [];
const p1: Product = {
    id: 1,
    name: "p1",
    price: 2
}
const p2: Product = {
    id: 2,
    name: "p2",
    price: 3
}
products.push(p1);
products.push(p2);

console.log(products);