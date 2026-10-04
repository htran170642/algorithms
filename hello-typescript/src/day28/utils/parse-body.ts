import type { z } from "zod";
import { ValidationError } from "../errors.js";

// Cửa vào duy nhất của dữ liệu không đáng tin: unknown → T đã được kiểm tra
export function parseBody<T>(schema: z.ZodType<T>, value: unknown): T {
    const result = schema.safeParse(value);
    if (!result.success) {
        const message = result.error.issues
            .map(issue => `${issue.path.join(".") || "body"}: ${issue.message}`)
            .join("; ");
        throw new ValidationError(message);
    }
    return result.data;
}
