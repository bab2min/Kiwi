export type ModelFiles = { [name: string]: ArrayBufferView | string };

export interface BuildArgs {
    modelFiles: ModelFiles;
    integrateAllomorph?: boolean;
    loadDefaultDict?: boolean;
    loadTypoDict?: boolean;
    loadMultiDict?: boolean;
    modelType?: 'knlm' | 'sbg';
    typos?: 'basic' | 'continual' | 'basic_with_continual';
    typoCostThreshold?: number;
};
