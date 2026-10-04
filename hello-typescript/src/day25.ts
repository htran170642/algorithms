function parseAny(json: string): any {
    return JSON.parse(json);
}

const a = parseAny('{"name":"Hiep"}');
console.log(a.name.toUpperCase());     // chạy được
// console.log(a.age.toFixed(2));      // BỎ comment: editor KHÔNG báo lỗi, nhưng chạy sẽ crash


// ===== unknown: phải kiểm tra trước khi dùng =====
function parseUnknown(json: string): unknown {
    return JSON.parse(json);
}

const u = parseUnknown('{"name":"Hiep"}');
// console.log(u.name);                // BỎ comment: editor báo lỗi ngay

if (typeof u === "object" && u !== null && "name" in u) {
    console.log(u.name);               // sau khi kiểm tra, mới dùng được
}

// ===== never (1): hàm không bao giờ trả về =====
function fail(message: string): never {
    throw new Error(message);
}

// ===== never (2): kiểm tra "đã xử lý hết mọi trường hợp" =====
type Shape =
    | { kind: "circle"; radius: number }
    | { kind: "square"; side: number }
    | { kind: "triangle"; base: number; height: number };


function area(shape: Shape): number {
    switch (shape.kind) {
        case "circle":
            return Math.PI * shape.radius ** 2;
        case "square":
            return shape.side ** 2;
        case "triangle":
            return (shape.base * shape.height) / 2;

        default: {
            const unreachable: never = shape;   // nếu còn trường hợp chưa xử lý, dòng này báo lỗi
            return fail(`Không xử lý: ${String(unreachable)}`);
        }
    }
}

console.log(area({ kind: "circle", radius: 2 }));
console.log(area({ kind: "square", side: 3 }));
console.log(area({ kind: "square", side: 3 }));