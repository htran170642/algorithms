export function isValidEmail(email: string): boolean {
    return email.includes("@");
}

export function isNonEmpty(value: string): boolean {
    return value.trim() !== "";
}

// KHÔNG export → chỉ dùng được trong file này
const MAX_NAME_LENGTH = 50;

export function isValidName(name: string): boolean {
    return isNonEmpty(name) && name.length <= MAX_NAME_LENGTH;
}
