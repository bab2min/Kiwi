import initKiwi from './build/kiwi-wasm.js';
import { KiwiApi } from './kiwi-api.js';
import { Kiwi } from './kiwi.js';
import { BuildArgs } from './build-args.js';

async function createKiwiApi(wasmPath: string): Promise<KiwiApi> {
    const kiwi = await initKiwi({
        locateFile: (path) => {
            if (path.endsWith('.wasm')) {
                return wasmPath;
            }
            return path;
        },
    });

    return {
        cmd: (args: any) => {
            return JSON.parse(kiwi.api(JSON.stringify(args)));
        },
        loadModelFiles: async (files) => {
            const modelPath =
                Math.random().toString(36).substring(2) + Date.now();

            kiwi.FS.mkdir(modelPath);

            const fileEntries = Object.entries(files);

            await Promise.all(
                fileEntries.map(async ([name, data]) => {
                    const path = modelPath + '/' + name;

                    if (typeof data === 'string') {
                        const response = await fetch(data);

                        if (!response.ok) {
                            throw new Error(`Failed to fetch file: ${data}`);
                        }

                        const fetchData = await response.arrayBuffer();

                        kiwi.FS.writeFile(path, new Uint8Array(fetchData));
                    } else {
                        kiwi.FS.writeFile(path, data);
                    }
                })
            );

            return {
                unload: async () => {
                    kiwi.FS.rmdir(modelPath);
                },
                modelPath,
            };
        },
    };
}

export class KiwiBuilder {
    private api: KiwiApi;

    private constructor(api: KiwiApi) {
        this.api = api;
    }

    static async create(wasmPath: string): Promise<KiwiBuilder> {
        const api = await createKiwiApi(wasmPath);
        return new KiwiBuilder(api);
    }

    public async build(buildArgs: BuildArgs): Promise<Kiwi> {
        const modelFiles = buildArgs.modelFiles;
        const loadResult = await this.api.loadModelFiles(modelFiles);

        const apiBuildArgs = {
            ...buildArgs,
            modelPath: loadResult.modelPath,
        };
        apiBuildArgs.modelFiles = undefined;

        const id = this.api.cmd({
            method: 'build',
            args: [apiBuildArgs],
        }) as number;

        return new Proxy({}, {
            get: (_target, prop) => {
                // prevent recursive promise resolution
                if (prop === 'then') {
                    return undefined;
                }

                return (...methodArgs: any[]) => {
                    return this.api.cmd({
                        method: prop.toString(),
                        id,
                        args: methodArgs,
                    });
                };
            },
        }) as Kiwi;
    }
}
