import type { IncomingMessage, ServerResponse } from "node:http";
import { AppError, ValidationError } from "./errors.js";
import type { ErrorBody, HttpResponse } from "./types/http.js";

// Đọc body của request và parse JSON
export async function readJsonBody(req: IncomingMessage): Promise<unknown> {
    const chunks: Buffer[] = [];
    for await (const chunk of req) {
        chunks.push(chunk as Buffer);
    }
    const text = Buffer.concat(chunks).toString("utf8");
    if (text === "") {
        return undefined;
    }
    try {
        // JSON.parse trả về any → ép về unknown để TS bắt ta kiểm tra
        return JSON.parse(text) as unknown;
    } catch {
        throw new ValidationError("Body không phải JSON hợp lệ");
    }
}

export function sendJson(res: ServerResponse, response: HttpResponse): void {
    res.writeHead(response.status, { "Content-Type": "application/json; charset=utf-8" });
    if (response.body === undefined) {
        res.end();
    } else {
        res.end(JSON.stringify(response.body));
    }
}

export function parseId(raw: string | undefined): number {
    const id = Number(raw);
    if (!Number.isInteger(id) || id <= 0) {
        throw new ValidationError(`id không hợp lệ: ${raw}`);
    }
    return id;
}

export function toErrorResponse(error: unknown): HttpResponse<ErrorBody> {
    if (error instanceof AppError) {
        return { status: error.statusCode, body: { error: error.message } };
    }
    console.error("Lỗi bất ngờ:", error);
    return { status: 500, body: { error: "Internal Server Error" } };
}
