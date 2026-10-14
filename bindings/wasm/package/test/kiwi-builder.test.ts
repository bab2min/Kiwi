import { describe, it, expect } from 'vitest';
import { KiwiBuilder } from '../src/index.js';

// KiwiBuilder with a fake api, so these run without the wasm build.
const builderWith = (api: object) => new (KiwiBuilder as any)(api) as KiwiBuilder;

describe('KiwiBuilder.build() model file cleanup', () => {
    it('keeps the build error when cleanup also fails', async () => {
        const builder = builderWith({
            cmd: () => { throw new Error('build failed'); },
            loadModelFiles: async () => ({ modelPath: 'm', unload: async () => { throw new Error('cleanup failed'); } }),
        });
        await expect(builder.build({ modelFiles: {} })).rejects.toThrow('build failed');
    });
    it('does not fail a successful build when cleanup fails', async () => {
        const builder = builderWith({
            cmd: () => 1,
            loadModelFiles: async () => ({ modelPath: 'm', unload: async () => { throw new Error('cleanup failed'); } }),
        });
        await expect(builder.build({ modelFiles: {} })).resolves.toBeDefined();
    });
});
