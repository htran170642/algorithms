# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository context

This is a personal TypeScript learning workspace that follows the 4-week plan below. Each `src/dayN.ts` file holds that day's exercises. The plan's checkboxes track progress. Tick an item only when the user has actually done it.

The user is learning, so explain *why* a TypeScript feature is used, not just how to write it. Prefer guiding the user over writing the exercise solutions for them unless they ask for code.

## Commands

`node_modules/` is not committed, so run `npm install` once before anything else.

```bash
npx tsc --noEmit          # type-check the whole project
npx tsc                   # compile; emits .js/.d.ts/.map files NEXT TO each .ts in src/
node src/day5.js          # run one day's file after compiling
```

There is no test runner (`npm test` is a placeholder that fails), no linter, and no `dev`/`build` scripts yet. The plan adds those on Day 16.

## Compiler setup that affects code

- The TypeScript version is `^7.x`. `tsconfig.json` has no `rootDir`/`outDir`, so output lands beside the sources. `.gitignore` ignores `src/**/*.js`, `*.d.ts` and `*.map`.
- `moduleDetection: "force"` makes every file its own module. The same names (`User`, `add`, …) can therefore be declared in several day files without conflicts.
- `module: "nodenext"` + `package.json` `"type": "commonjs"` + `verbatimModuleSyntax` is an unusual combination, and no file uses cross-file `import`/`export` yet. When modules arrive (Day 15), run `npx tsc` and fix the config if it rejects ESM syntax in CommonJS files. Switching to `"type": "module"` (which then requires `.js` extensions on relative imports) is the usual fix. Under `verbatimModuleSyntax`, type-only imports must use `import type`.
- `strict` is on, along with `noUncheckedIndexedAccess` (`arr[i]` is `T | undefined`) and `exactOptionalPropertyTypes` (you cannot assign `undefined` to an optional property unless its type includes `undefined`).

---

# TypeScript — 4 Week Learning Plan

## Goal

After 4 weeks, I should be able to:

* [ ] Understand JavaScript fundamentals needed for TypeScript
* [ ] Write strongly typed TypeScript code
* [ ] Understand interfaces, types, unions, intersections
* [ ] Understand type narrowing
* [ ] Use generics confidently
* [ ] Use common utility types
* [ ] Understand `any`, `unknown`, `never`
* [ ] Work with async/await and `Promise<T>`
* [ ] Build a REST API with Node.js + TypeScript
* [ ] Understand DTOs and runtime validation
* [ ] Read and understand real-world TypeScript code
* [ ] Build a complete TypeScript project

---

# Week 1 — JavaScript + TypeScript Fundamentals

## Day 1 — JavaScript Runtime & TypeScript Setup

### Learn

* [x] JavaScript vs TypeScript
* [x] TypeScript compiler (`tsc`)
* [x] JavaScript runtime
* [x] Node.js
* [x] npm
* [x] `package.json`
* [x] `tsconfig.json`
* [x] `.ts` files

### Practice

```bash
mkdir typescript-learning
cd typescript-learning

npm init -y
npm install -D typescript

npx tsc --init
```

Create:

```ts
const name: string = "Hiep";

console.log(`Hello ${name}`);
```

### Understand

```text
TypeScript
    ↓
Type checking
    ↓
JavaScript
    ↓
Node.js / Browser
```

---

## Day 2 — Basic Types

### Learn

* [x] `string`
* [x] `number`
* [x] `boolean`
* [x] `null`
* [x] `undefined`
* [x] `unknown`
* [x] `any`
* [x] `never`
* [x] `void`

### Arrays

```ts
const numbers: number[] = [1, 2, 3];

const names: Array<string> = [
    "Alice",
    "Bob"
];
```

### Tuples

```ts
const user: [string, number] = [
    "Hiep",
    30
];
```

### Exercise

Create typed variables for:

* [x] User name
* [x] Age
* [x] Email
* [x] Is active
* [x] List of scores
* [x] User tuple

---

## Day 3 — Objects

### Learn

* [x] Object types
* [x] Optional properties
* [x] Readonly properties
* [x] Index signatures

### Example

```ts
type User = {
    id: number;
    name: string;
    age?: number;
    readonly email: string;
};
```

### Practice

Create:

```ts
type Product = {
    id: number;
    name: string;
    price: number;
    description?: string;
};
```

Implement:

```ts
const products: Product[] = [];
```

---

## Day 4 — Functions

### Learn

* [x] Parameter types
* [x] Return types
* [x] Optional parameters
* [x] Default parameters
* [x] Arrow functions
* [x] Function types

### Example

```ts
function add(
    a: number,
    b: number
): number {
    return a + b;
}
```

### Function type

```ts
type Calculator = (
    a: number,
    b: number
) => number;

const add: Calculator = (a, b) => a + b;
```

### Exercise

Implement:

```ts
add()
subtract()
multiply()
divide()
```

with proper types.

---

## Day 5 — Interface vs Type

### Learn

* [x] `interface`
* [x] `type`
* [x] `extends`
* [x] Intersection `&`

### Interface

```ts
interface User {
    id: number;
    name: string;
}
```

### Type

```ts
type User = {
    id: number;
    name: string;
};
```

### Extension

```ts
interface Admin extends User {
    permissions: string[];
}
```

### Intersection

```ts
type Admin = User & {
    permissions: string[];
};
```

### Exercise

Create:

```text
User
Admin
Customer
Employee
```

using interfaces/types.

---

## Day 6 — Union & Intersection

### Learn

* [x] Union `|`
* [x] Intersection `&`
* [x] Literal types

### Union

```ts
type Status =
    | "pending"
    | "success"
    | "failed";
```

### Union values

```ts
type ID = string | number;
```

### Intersection

```ts
type Timestamped = {
    createdAt: Date;
};

type User = {
    name: string;
};

type UserWithTimestamp =
    User & Timestamped;
```

### Exercise

Create:

```ts
type PaymentStatus =
    | "pending"
    | "paid"
    | "failed"
    | "refunded";
```

---

# Day 7 — Mini Project #1

## User Management CLI

Build a small application.

### Model

```ts
interface User {
    id: number;
    name: string;
    email: string;
    age: number;
}
```

### Functions

```ts
createUser()
getUser()
getUsers()
deleteUser()
```

### Storage

```ts
const users: User[] = [];
```

### Requirements

* [x] Create user
* [x] Get user by ID
* [x] List users
* [x] Delete user
* [x] Validate basic input
* [x] Use proper TypeScript types

---

# Week 2 — TypeScript Type System

# Day 8 — Type Narrowing

### Learn

* [x] `typeof`
* [x] `instanceof`
* [x] `in`
* [x] Equality narrowing
* [x] Control-flow analysis

### Example

```ts
function print(value: string | number) {
    if (typeof value === "string") {
        console.log(value.toUpperCase());
    } else {
        console.log(value.toFixed(2));
    }
}
```

### Exercise

Create functions that accept:

```ts
string | number
```

and behave differently based on the actual type.

---

# Day 9 — Discriminated Unions

### Learn

* [x] Discriminated unions
* [x] Exhaustive checking

### Example

```ts
type Result =
    | {
        status: "success";
        data: string;
    }
    | {
        status: "error";
        error: string;
    };
```

### Practice

Create:

```ts
type ApiResult<T> =
    | {
        status: "success";
        data: T;
    }
    | {
        status: "error";
        error: string;
    };
```

---

# Day 10 — Generics

### Learn

* [x] Generic functions
* [x] Generic interfaces
* [x] Generic classes
* [x] Generic arrays

### Example

```ts
function identity<T>(value: T): T {
    return value;
}
```

### Generic array

```ts
function first<T>(
    items: T[]
): T | undefined {
    return items[0];
}
```

### Generic response

```ts
interface ApiResponse<T> {
    data: T;
    success: boolean;
}
```

### Exercise

Implement:

```ts
function last<T>(items: T[]): T | undefined
```

---

# Day 11 — Generic Constraints

### Learn

```ts
<T extends Something>
```

### Example

```ts
function getId<T extends { id: number }>(
    obj: T
): number {
    return obj.id;
}
```

### Understand

```text
<T>
```

vs

```text
<T extends U>
```

### Exercise

Create a generic function that accepts only objects containing:

```ts
{
    id: number
}
```

---

# Day 12 — Utility Types

### Learn

* [x] `Partial<T>`
* [x] `Required<T>`
* [x] `Readonly<T>`
* [x] `Pick<T, K>`
* [x] `Omit<T, K>`
* [x] `Record<K, T>`

### Example

```ts
interface User {
    id: number;
    name: string;
    email: string;
}
```

### Partial

```ts
type UpdateUser = Partial<User>;
```

### Omit

```ts
type CreateUser = Omit<User, "id">;
```

### Pick

```ts
type UserPreview =
    Pick<User, "id" | "name">;
```

### Record

```ts
type UserMap =
    Record<number, User>;
```

---

# Day 13 — keyof / typeof / Indexed Access

### Learn

* [x] `keyof`
* [x] `typeof`
* [x] Indexed access types
* [x] Generic key constraints

### Example

```ts
type User = {
    id: number;
    name: string;
};

type UserKey = keyof User;
```

Result:

```ts
"id" | "name"
```

### Important pattern

```ts
function getValue<
    T,
    K extends keyof T
>(
    obj: T,
    key: K
): T[K] {
    return obj[key];
}
```

Understand why this works.

---

# Day 14 — Mini Project #2

## Generic Repository

### Base Entity

```ts
interface Entity {
    id: number;
}
```

### Repository

```ts
class Repository<T extends Entity> {

    private items: T[] = [];

    create(item: T): void {
        this.items.push(item);
    }

    findById(
        id: number
    ): T | undefined {
        return this.items.find(
            item => item.id === id
        );
    }

    findAll(): T[] {
        return this.items;
    }

    delete(id: number): void {
        this.items = this.items.filter(
            item => item.id !== id
        );
    }
}
```

### Use it

```ts
interface User extends Entity {
    name: string;
}

const users =
    new Repository<User>();
```

### Goal

Understand:

```text
Generics
    ↓
Constraints
    ↓
Reusable abstractions
```

---

# Week 3 — TypeScript + Node.js

# Day 15 — Modules

### Learn

* [ ] `export`
* [ ] `import`
* [ ] ES Modules
* [ ] CommonJS
* [ ] Module resolution

### Example

```ts
// user.ts

export interface User {}

export function createUser() {}
```

```ts
// index.ts

import {
    User,
    createUser
} from "./user";
```

---

# Day 16 — npm & Project Structure

### Learn

* [ ] `package.json`
* [ ] `package-lock.json`
* [ ] `node_modules`
* [ ] dependencies
* [ ] devDependencies
* [ ] npm scripts

### Example

```json
{
    "scripts": {
        "dev": "tsx src/index.ts",
        "build": "tsc",
        "start": "node dist/index.js"
    }
}
```

### Practice

Create:

```text
src/
├── index.ts
├── types/
├── services/
├── repositories/
└── utils/
```

---

# Day 17 — Async TypeScript

### Learn

* [ ] `Promise<T>`
* [ ] `async`
* [ ] `await`
* [ ] `Promise.all`
* [ ] `Promise.allSettled`

### Example

```ts
async function getUser(
    id: number
): Promise<User> {
    // ...
}
```

Understand:

```text
Promise<User>
Promise<User[]>
Promise<void>
```

---

# Day 18 — Error Handling

### Learn

* [ ] `try/catch`
* [ ] `Error`
* [ ] `unknown`
* [ ] Custom errors

### Example

```ts
try {
    await getUser(1);
} catch (error) {

    if (error instanceof Error) {
        console.log(error.message);
    }
}
```

### Exercise

Create:

```ts
class UserNotFoundError
    extends Error {
}
```

---

# Day 19 — REST API

Build a REST API with Node.js + TypeScript.

### Endpoints

```text
GET    /users
GET    /users/:id
POST   /users
PATCH  /users/:id
DELETE /users/:id
```

### Architecture

```text
HTTP
 ↓
Controller
 ↓
Service
 ↓
Repository
 ↓
Database
```

Focus on understanding TypeScript rather than the framework.

---

# Day 20 — DTO & Runtime Validation

### Learn

```text
HTTP Request
      ↓
DTO
      ↓
Validation
      ↓
Business Logic
      ↓
Repository
```

### DTO

```ts
interface CreateUserRequest {
    name: string;
    email: string;
    age: number;
}
```

### Important

TypeScript types disappear at runtime.

This:

```ts
interface User {
    name: string;
}
```

does **not** validate incoming JSON.

Learn a runtime validation library such as:

```text
Zod
```

---

# Day 21 — Mini Project #3

## User REST API

### Structure

```text
src/
├── controllers/
├── services/
├── repositories/
├── models/
├── routes/
├── types/
└── index.ts
```

### Requirements

* [ ] CRUD API
* [ ] Type all request/response objects
* [ ] Service layer
* [ ] Repository layer
* [ ] Error handling
* [ ] Async/await
* [ ] Runtime validation
* [ ] Proper HTTP status codes

---

# Week 4 — Advanced TypeScript

# Day 22 — Advanced Generics

### Master

```ts
<T>
<T extends U>
keyof
T[K]
```

### Practice

```ts
function getProperty<
    T,
    K extends keyof T
>(
    object: T,
    key: K
): T[K] {
    return object[key];
}
```

---

# Day 23 — Conditional Types

### Learn

```ts
T extends U ? X : Y
```

### Example

```ts
type IsString<T> =
    T extends string
        ? true
        : false;
```

### Goal

Understand conditional types.

Do not spend excessive time memorizing complicated type tricks.

---

# Day 24 — Mapped Types

### Learn

```ts
type Optional<T> = {
    [K in keyof T]?: T[K];
};
```

Understand the relationship between:

```text
keyof
    +
generics
    +
mapped types
    +
conditional types
```

---

# Day 25 — any / unknown / never

## any

```ts
let value: any;
```

Disables type safety.

## unknown

```ts
let value: unknown;
```

Must be narrowed before use.

## never

```ts
function fail(
    message: string
): never {
    throw new Error(message);
}
```

### Goal

Be able to explain:

```text
any
unknown
never
```

without looking at documentation.

---

# Day 26 — TypeScript Design Patterns

### Result Type

```ts
type Result<T> =
    | {
        success: true;
        data: T;
    }
    | {
        success: false;
        error: string;
    };
```

### Repository

```ts
interface Repository<T> {

    findById(
        id: number
    ): Promise<T | null>;

    findAll(): Promise<T[]>;

    create(
        data: T
    ): Promise<T>;
}
```

### Dependency Injection

```ts
class UserService {

    constructor(
        private repository: UserRepository
    ) {}
}
```

---

# Day 27 — Read Real TypeScript

Choose a real TypeScript project.

For every unfamiliar piece of code, identify:

* [ ] Interfaces
* [ ] Type aliases
* [ ] Generics
* [ ] Classes
* [ ] Services
* [ ] Repositories
* [ ] DTOs
* [ ] Utility types
* [ ] Async functions
* [ ] Error handling

Ask:

> What problem is this type solving?

Do not try to understand the entire codebase.

Focus on recognizing patterns.

---

# Day 28 — Final Project

# Task Management API

Build a complete application.

## Domain

```text
Users
Projects
Tasks
Authentication
```

## Task

```ts
interface Task {
    id: number;
    title: string;
    description?: string;
    status: TaskStatus;
    createdAt: Date;
}
```

## Status

```ts
type TaskStatus =
    | "todo"
    | "in_progress"
    | "done";
```

## Generic Response

```ts
interface ApiResponse<T> {
    data: T;
    message?: string;
}
```

## Repository

```ts
interface Repository<T> {

    findById(
        id: number
    ): Promise<T | null>;

    findAll(): Promise<T[]>;

    create(
        data: T
    ): Promise<T>;

    delete(
        id: number
    ): Promise<void>;
}
```

---

# Final Project Architecture

```text
                    HTTP
                     │
                     ▼
                Controller
                     │
                     ▼
                  Service
                     │
                     ▼
                Repository
                     │
                     ▼
                  Database
```

Recommended structure:

```text
src/
├── controllers/
│   ├── user.controller.ts
│   ├── project.controller.ts
│   └── task.controller.ts
│
├── services/
│   ├── user.service.ts
│   ├── project.service.ts
│   └── task.service.ts
│
├── repositories/
│   ├── user.repository.ts
│   ├── project.repository.ts
│   └── task.repository.ts
│
├── models/
│   ├── user.ts
│   ├── project.ts
│   └── task.ts
│
├── types/
│   ├── api.ts
│   └── common.ts
│
├── routes/
│   └── index.ts
│
└── index.ts
```

---

# Daily Study Routine

Spend approximately **2–3 hours/day**.

```text
30 min  → Learn theory
30 min  → Write examples
60 min  → Build something
30 min  → Exercises
30 min  → Review / read real code
```

If you only have 1 hour:

```text
20 min → Theory
30 min → Coding
10 min → Review
```

Rule:

> Spend more time writing TypeScript than reading about TypeScript.

---

# Priority Topics

## Must Master

* [ ] Basic types
* [ ] Objects
* [ ] Functions
* [ ] Interfaces
* [ ] Type aliases
* [ ] Union types
* [ ] Intersection types
* [ ] Type narrowing
* [ ] Discriminated unions
* [ ] Generics
* [ ] Generic constraints
* [ ] Utility types
* [ ] `keyof`
* [ ] `T[K]`
* [ ] `unknown`
* [ ] `any`
* [ ] `never`
* [ ] `Promise<T>`
* [ ] async/await
* [ ] Modules
* [ ] Node.js + TypeScript

## Learn After the Basics

* [ ] Conditional types
* [ ] Mapped types
* [ ] Advanced generic patterns
* [ ] Decorators
* [ ] Declaration merging
* [ ] Advanced compiler configuration
* [ ] Type-level programming

---

# TypeScript Mental Model

Keep this model in mind:

```text
JavaScript
    │
    │ runtime
    ▼
┌──────────────────┐
│ JavaScript Code  │
└──────────────────┘

TypeScript
    │
    │ compile-time
    ▼
┌──────────────────┐
│ Type Checking    │
└──────────────────┘
    │
    ▼
JavaScript
    │
    ▼
Runtime
```

The key idea:

> TypeScript improves developer-time safety, but TypeScript types do not exist at runtime.

Therefore:

```text
TypeScript type checking
        ≠
Runtime validation
```

This distinction is extremely important when building APIs.

---

# End-of-Month Checklist

By the end of 4 weeks, I should be able to explain:

* [ ] What TypeScript is
* [ ] TypeScript vs JavaScript
* [ ] Structural typing
* [ ] `interface` vs `type`
* [ ] Union vs intersection
* [ ] Type narrowing
* [ ] Discriminated unions
* [ ] Generics
* [ ] Generic constraints
* [ ] `keyof`
* [ ] Indexed access types
* [ ] Utility types
* [ ] `any` vs `unknown`
* [ ] `never`
* [ ] Promise and async/await
* [ ] Runtime validation
* [ ] TypeScript modules
* [ ] TypeScript + Node.js
* [ ] TypeScript project architecture

---

# Final Goal

At the end of the 4 weeks, I should be able to open a TypeScript codebase and understand code like:

```ts
interface Repository<T> {
    findById(
        id: number
    ): Promise<T | null>;

    findAll(): Promise<T[]>;
}

type Result<T> =
    | {
        success: true;
        data: T;
    }
    | {
        success: false;
        error: string;
    };

class UserService {

    constructor(
        private repository: Repository<User>
    ) {}

    async getUser(
        id: number
    ): Promise<Result<User>> {

        const user =
            await this.repository.findById(id);

        if (!user) {
            return {
                success: false,
                error: "User not found"
            };
        }

        return {
            success: true,
            data: user
        };
    }
}
```

And explain **why each TypeScript feature is being used**, not just what the syntax means.
