import { describe, it, expect, beforeAll } from 'vitest';
import { KiwiBuilder } from '../src/index.js';
import * as fs from 'fs';
import * as path from 'path';

const PROJECT_ROOT = path.resolve(__dirname, '../../../../');
const WASM_PATH = path.resolve(PROJECT_ROOT, 'bindings/wasm/build/bindings/wasm/kiwi-wasm.wasm');
const MODEL_DIR = path.resolve(PROJECT_ROOT, 'models/cong/base');

function loadModelFiles(): Record<string, Uint8Array> {
    const modelFiles: Record<string, Uint8Array> = {};
    const files = [
        'combiningRule.txt', 'cong.mdl', 'default.dict', 
        'dialect.dict', 'extract.mdl', 'multi.dict', 
        'nounchr.mdl', 'sj.morph', 'typo.dict'
    ];

    for (const file of files) {
        const filePath = path.join(MODEL_DIR, file);
        if (fs.existsSync(filePath)) {
            modelFiles[file] = fs.readFileSync(filePath);
        }
    }
    return modelFiles;
}

describe('Kiwi WASM', () => {
    let kiwiBuilder: KiwiBuilder;

    beforeAll(async () => {
        if (!fs.existsSync(WASM_PATH)) {
            console.warn(`WASM file not found at ${WASM_PATH}. Skipping tests.`);
            return;
        }
        kiwiBuilder = await KiwiBuilder.create(WASM_PATH);
    });

    it('should be initialized', async () => {
        if (!kiwiBuilder) return;
        expect(kiwiBuilder.version()).toBeTypeOf('string');
    });

    it('should tokenize text', async () => {
        if (!kiwiBuilder) return;

        const kiwi = await kiwiBuilder.build({
            modelFiles: loadModelFiles(),
            modelType: 'cong',
            integrateAllomorph: true,
        });

        expect(kiwi.ready()).toBe(true);

        const result = kiwi.tokenize('안녕하세요 세계');
        expect(result.length).toBeGreaterThan(0);
        
        // "안녕하세요" -> 안녕/NNG, 하/XSA, 세요/EC
        const tokens = result.map(t => t.str);
        expect(tokens).toContain('안녕');
    });

    it('should split sentences', async () => {
        if (!kiwiBuilder) return;
        
        const kiwi = await kiwiBuilder.build({ 
            modelFiles: loadModelFiles(), 
            modelType: 'cong' 
        });
        const result = kiwi.splitIntoSents('안녕하세요. 반갑습니다!');
        expect(result.spans.length).toBe(2);
    });

    it('should get and set global config', async () => {
        if (!kiwiBuilder) return;

        const kiwi = await kiwiBuilder.build({ 
            modelFiles: loadModelFiles(), 
            modelType: 'cong' 
        });
        
        const config = kiwi.getGlobalConfig();
        expect(config.cutOffThreshold).toBeTypeOf('number');

        const originalThreshold = config.cutOffThreshold;
        kiwi.setGlobalConfig({ cutOffThreshold: 10 });
        
        const newConfig = kiwi.getGlobalConfig();
        expect(newConfig.cutOffThreshold).toBe(10);

        // Restore
        kiwi.setGlobalConfig({ cutOffThreshold: originalThreshold });
    });
});
