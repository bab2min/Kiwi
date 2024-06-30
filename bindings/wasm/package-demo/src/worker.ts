import { KiwiBuilder, Kiwi, TokenInfo } from 'kiwi-nlp';
import kiwiWasmPath from 'kiwi-nlp/dist/kiwi-wasm.wasm?url';
import { modelFiles } from './modelFiles';

let kiwiBuilder: KiwiBuilder | null = null;
let kiwi: Kiwi | null = null;
let version: string | null = null;

export type WorkerRequest =
    | { type: 'init' }
    | { type: 'analyze'; text: string };
export type WorkerResponse =
    | { type: 'inited'; version: string }
    | { type: 'analyzed'; result: TokenInfo[]; text: string };

function sendResponse(response: WorkerResponse) {
    self.postMessage(response);
}

async function init() {
    kiwiBuilder = await KiwiBuilder.create(kiwiWasmPath);
    version = kiwiBuilder.version();
    kiwi = await kiwiBuilder.build({ modelFiles });

    sendResponse({ type: 'inited', version });
}

function analyze(text: string) {
    if (!kiwi) {
        throw new Error('Kiwi is not initialized');
    }

    const result = kiwi.tokenize(text);
    sendResponse({ type: 'analyzed', result, text });
}

self.onmessage = (event) => {
    const request: WorkerRequest = event.data;

    switch (request.type) {
        case 'init':
            init();
            break;
        case 'analyze':
            analyze(request.text);
            break;
        default:
            throw new Error('Unknown request type');
    }
};
