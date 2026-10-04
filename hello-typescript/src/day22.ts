type User = {
    id: number;
    name: string;
    active: boolean;
}

function getProperty<T, K extends keyof T>(object: T, key: K): T[K] {
    return object[key];
}

const user: User = { id: 1, name: "Hiep", active: true};

const a = getProperty(user, "id");
const b = getProperty(user, "name");
const c = getProperty(user, "active");

console.log(a,b,c)


