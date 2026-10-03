interface User {
    id: number;
    name: string;
}

const u1: User = {id: 1, name: "Hiep"};
console.log(u1);

interface Admin extends User {
    permissions: string[]
}

const a1: Admin = {
    id: 2,
    name: "An",
    permissions: ["read", "write"],
};

console.log(a1);


type UserT = {
    id: number;
    name: string;
}

type AdminT = UserT & {
    permissions: string[]
}

const a2: AdminT = {
    id: 3,
    name: "Binh",
    permissions: ["read"]
}

console.log(a2);

interface Config {
    host: string;
}

interface Config {
    port: number;
}

const cfg: Config = {host: "localhost", port: 3000 };
console.log(cfg)

type Settings = { theme: string };
type Settings = { fontSize: number };
