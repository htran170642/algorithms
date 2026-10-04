export class AppError extends Error {
    readonly statusCode: number;

    constructor(message: string, statusCode: number) {
        super(message);
        this.name = "AppError";
        this.statusCode = statusCode;
    }
}

export class ValidationError extends AppError {
    constructor(message: string) {
        super(message, 400);
        this.name = "ValidationError";
    }
}

export class NotFoundError extends AppError {
    constructor(message: string) {
        super(message, 404);
        this.name = "NotFoundError";
    }
}

export class ConflictError extends AppError {
    constructor(message: string) {
        super(message, 409);
        this.name = "ConflictError";
    }
}


export class UnauthorizedError extends AppError {
    constructor(message: string) {
        super(message, 401);
        this.name = "UnauthorizedError";
    }
}

export class ForbiddenError extends AppError {
    constructor(message: string) {
        super(message, 403);
        this.name = "ForbiddenError";
    }
}
