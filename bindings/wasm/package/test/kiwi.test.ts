import { describe, it, expect, beforeAll } from 'vitest';
import { KiwiBuilder } from '../src/index.js';
import { Kiwi, Match } from '../src/kiwi.js';
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
    let kiwi: Kiwi;

    beforeAll(async () => {
        if (!fs.existsSync(WASM_PATH)) {
            console.warn(`WASM file not found at ${WASM_PATH}. Skipping tests.`);
            return;
        }
        kiwiBuilder = await KiwiBuilder.create(WASM_PATH);
        kiwi = await kiwiBuilder.build({
            modelFiles: loadModelFiles(),
            modelType: 'cong',
            integrateAllomorph: true,
        });
    });

    it('should be initialized', async () => {
        if (!kiwiBuilder) return;
        expect(kiwiBuilder.version()).toBeTypeOf('string');
    });

    it('should tokenize text', async () => {
        if (!kiwi) return;

        expect(kiwi.ready()).toBe(true);

        const result = kiwi.tokenize('안녕하세요 세계');
        expect(result.length).toBeGreaterThan(0);

        const tokens = result.map(t => t.str);
        expect(tokens).toContain('안녕');
    });

    it('should split sentences', async () => {
        if (!kiwi) return;

        const result = kiwi.splitIntoSents('안녕하세요. 반갑습니다!');
        expect(result.spans.length).toBe(2);
    });

    it('should correct typos with basic typo set via tokenize', async () => {
        if (!kiwi) return;

        // Without typo correction
        const tokensNoTypo = kiwi.tokenize('나 죰 도와죠.');
        expect(tokensNoTypo.map(t => t.str)).toContain('죰');

        // With basic typo correction at analyze time
        const tokensWithTypo = kiwi.tokenize('나 죰 도와죠.', Match.allWithNormalizing, undefined, undefined, 'basic');
        expect(tokensWithTypo.map(t => t.str)).toContain('좀');
    });

    it('should correct typos with basic typo set via analyze', async () => {
        if (!kiwi) return;

        const result = kiwi.analyze('나 죰 도와죠.', Match.allWithNormalizing, undefined, undefined, 'basic');
        expect(result.tokens.map(t => t.str)).toContain('좀');
    });

    it('should correct typos with basic typo set via analyzeTopN', async () => {
        if (!kiwi) return;

        const results = kiwi.analyzeTopN('나 죰 도와죠.', 3, Match.allWithNormalizing, undefined, undefined, 'basic');
        expect(results.length).toBeGreaterThan(0);
        expect(results[0].tokens.map(t => t.str)).toContain('좀');
    });

    it('should correct continual typos', async () => {
        if (!kiwi) return;

        const tokens = kiwi.tokenize('프로그래미', Match.allWithNormalizing, undefined, undefined, 'continual');
        const forms = tokens.map(t => t.str);
        expect(forms[0]).toBe('프로그램');
        expect(forms[1]).toBe('이');
    });

    it('should correct typos with basicWithContinual', async () => {
        if (!kiwi) return;

        // continual typo
        const tokens1 = kiwi.tokenize('프로그래미', Match.allWithNormalizing, undefined, undefined, 'basicWithContinual');
        expect(tokens1.map(t => t.str)[0]).toBe('프로그램');

        // basic typo
        const tokens2 = kiwi.tokenize('나 죰 도와죠.', Match.allWithNormalizing, undefined, undefined, 'basicWithContinual');
        expect(tokens2.map(t => t.str)).toContain('좀');
    });

    it('should get and set global config', async () => {
        if (!kiwi) return;

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
