import type { ApiResponse } from "../types/common.js";
import type { HttpResponse } from "../types/http.js";

// Gói dữ liệu vào ApiResponse<T> để mọi response thành công có cùng hình dạng
export function respond<T>(status: number, data: T, message?: string): HttpResponse<ApiResponse<T>> {
    const body: ApiResponse<T> = message === undefined ? { data } : { data, message };
    return { status, body };
}
