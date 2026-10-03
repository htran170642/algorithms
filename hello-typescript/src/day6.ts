let a = "Hello, World!";
const b = "TypeScript is awesome!";

let dir: "up" | "down" | "left" | "right" = "up";

dir = "down"; // valid assignment
// dir = "xx";


type PaymentStatus =
    | "pending"
    | "paid"
    | "failed"
    | "refunded";


function describePayment(status: PaymentStatus): string {
    switch (status) {
        case "pending":
            return "Your payment is pending.";
        case "paid":
            return "Your payment was successful.";
        case "failed":
            return "Your payment failed. Please try again.";
        case "refunded":
            return "Your payment has been refunded.";
        default:
            return "Unknown payment status.";
    }
}

console.log(describePayment("paid")); // Output: Your payment was successful.4

// describePayment("XXX"); // Output: Your payment has been refunded.


type ID = number | string;

function printID(id: ID): void {
    if (typeof id === "number") {
        console.log(`ID is a number: ${id}`);
    } else {
        id = id.toLocaleUpperCase();
        console.log(`ID is a string: ${id}`);
    }
}

printID(123); // Output: ID is a number: 123
printID("abc"); // Output: ID is a string: ABC


type Timestamp = {
    createdAt: Date;
}

type Payment = {
    id: ID;
    amount: number;
    status: PaymentStatus;
} & Timestamp;

const payments: Payment[] = [
    {
        id: 1,
        amount: 100,
        status: "paid",
        createdAt: new Date("2023-01-01"),
    },
    {
        id: "abc123",
        amount: 50,
        status: "pending",
        createdAt: new Date("2023-02-15"),
    },
    {
        id: 2,
        amount: 75,
        status: "failed",
        createdAt: new Date("2023-03-10"),
    },
];

for (const payment of payments) {
    console.log(`Payment ID: ${payment.id}, Amount: ${payment.amount}, Status: ${payment.status}, Created At: ${payment.createdAt}`);
}

let x: PaymentStatus = "paid";
describePayment(x);

// const y = "paid";
// describePayment(y);

// type Imposible = string & number; // never