export interface HttpResponse<T = unknown> {
    status: number;
    body?: T;
}

export interface ErrorBody {
    error: string
}