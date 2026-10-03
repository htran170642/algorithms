interface Entity {
    id: number;
}

class Repository<T extends Entity> {
    private items: T[] = [];
    private nextId: number = 1;

    create(item: Omit<T, "id">): T {
        const newItem: T = { ...item, id: this.nextId++ } as T;
        this.items.push(newItem);
        return newItem;
    }

    findById(id: number): T | undefined {
        return this.items.find(item => item.id === id);
    }

    // readonly FindAll
    findAll() : readonly T[] {
        return this.items;
    }

    // Day 13: key phải là key thật của T, value phải đúng kiểu của key đó
    findBy<K extends keyof T>(key: K, value: T[K]): T[] {
        return this.items.filter(item => item[key] === value);
    }

    // Day 12: chỉ gửi field muốn đổi, không được đổi id
    update(id: number, updatedFields: Partial<Omit<T, "id">>): T | undefined {
        const item = this.findById(id);
        if (item) {
            Object.assign(item, updatedFields);
            return item;
        }
        return undefined;
    }

    delete(id: number): boolean {
        const index = this.items.findIndex(item => item.id === id);
        if (index !== -1) {
            this.items.splice(index, 1);
            return true;
        }
        return false;
    }

    count(): number {
        return this.items.length;
    }
}

interface User extends Entity {
    name: string;
    email: string;
    role: "admin" | "user" | "guest";
}

interface Product extends Entity {
    name: string;
    price: number;
}

const users = new Repository<User>();
const products = new Repository<Product>();

users.create({ name: "Alice", email: "alice@example.com", role: "user" });
users.create({ name: "Bob", email: "bob@example.com", role: "admin" });
products.create({ name: "Laptop", price: 1000 });
products.create({ name: "Mouse", price: 25 });

console.log(users.findAll()); // All users
console.log(users.findById(1)?.name); // User with id 1
console.log(users.findBy("role", "admin").map(user => user.name)); // Users with role "admin"
console.log(users.update(1, { email: "alice.updated@example.com" })); // Update user with id 1
console.log(users.delete(2)); // Delete user with id 2
console.log(users.delete(99)); // Count of users
console.log(users.count()); // Count of users

// products
console.log(products.findAll());
