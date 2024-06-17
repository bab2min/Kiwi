export type AsyncMethods<T> = {
    [K in keyof T]: T[K] extends (...args: any) => any
        ? (...args: Parameters<T[K]>) => Promise<ReturnType<T[K]>>
        : T[K];
};
