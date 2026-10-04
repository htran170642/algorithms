import { randomBytes, scrypt, timingSafeEqual } from "node:crypto";

const KEY_LENGTH = 64;

function derive(password: string, salt: Buffer): Promise<Buffer> {
    return new Promise((resolve, reject) => {
        scrypt(password, salt, KEY_LENGTH, (error, key) => {
            if (error) {
                reject(error);
            } else {
                resolve(key);
            }
        });
    });
}

// Lưu dạng "salt:hash" (hex). Mỗi mật khẩu có salt riêng.
export async function hashPassword(password: string): Promise<string> {
    const salt = randomBytes(16);
    const key = await derive(password, salt);
    return `${salt.toString("hex")}:${key.toString("hex")}`;
}

export async function verifyPassword(password: string, stored: string): Promise<boolean> {
    const [saltHex, hashHex] = stored.split(":");
    if (saltHex === undefined || hashHex === undefined) {
        return false;
    }
    const expected = Buffer.from(hashHex, "hex");
    const actual = await derive(password, Buffer.from(saltHex, "hex"));
    return expected.length === actual.length && timingSafeEqual(expected, actual);
}
