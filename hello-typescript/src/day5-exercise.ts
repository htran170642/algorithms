interface User {
    id: number;
    name: string;
    readonly email: string;
}

interface Admin extends User {
    permissions: string[]
}

// type + intersection
type Customer = User & {
    loyaltyPoints: number;
    phone?: string;
};


// recursive type
interface Employee extends User {
    department: string;
    salary: number;
    manager?: Employee;
}


// create 
const admin: Admin = {
    id: 1,
    name: "Admin User",
    email: "admin@example.com",
    permissions: ["read", "write", "delete"]
};


const customer: Customer = {
    id: 2,
    name: "Customer User",
    email: "customer@gmail.com",
    loyaltyPoints: 150,
    phone: "123-456-7890"
};

const boss: Employee = {
    id: 3,
    name: "Employee User",
    email: "employee@example.com",
    department: "Engineering",
    salary: 75000,
};

const employee: Employee = {
    id: 4,
    name: "Junior Employee",
    email: "junior@example.com",
    department: "Marketing",
    salary: 50000,
    manager: boss
};

console.log(admin);
console.log(customer);
console.log(employee);

// admin.email = "xx"

// const c2: Customer = {
//     id: 5,
//     name: "Another Customer",
//     email: "another_customer@gmail.com",
//     phone: undefined,
//     loyaltyPoints: 200
// };

