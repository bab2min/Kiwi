import { ModelFiles } from "./build-args";

interface LoadModelFilesResult {
    unload: () => Promise<void>;
    modelPath: string;
}

export interface KiwiApi {
    cmd: (args: any) => any;
    loadModelFiles: (files: ModelFiles) => Promise<LoadModelFilesResult>;
}

export interface KiwiApiAsync {
    cmd: (...args: any) => Promise<any>;
    loadFiles: (files: ModelFiles) => Promise<LoadModelFilesResult>;
}
