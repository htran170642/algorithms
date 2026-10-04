export interface Entity {
    id: number;
}

export interface ApiResponse<T> {
    data: T;
    message?: string;
}

// Giống Partial<T>, nhưng cho phép giá trị undefined tường minh.
// Cần vì exactOptionalPropertyTypes: kiểu do Zod sinh ra có dạng `title?: string | undefined`.
export type Patch<T> = {
    [K in keyof T]?: T[K] | undefined;
};