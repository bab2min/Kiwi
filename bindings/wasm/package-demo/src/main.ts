import './style.css';

import * as lib from 'kiwi-nlp';
import { KiwiBuilder } from 'kiwi-nlp';

import kiwiWasmPath from 'kiwi-nlp/dist/kiwi-wasm.wasm?url';

import { modelFiles } from './modelFiles';

function setGlobal(key: string, value: any) {
    // @ts-ignore
    globalThis[key] = value;
}

setGlobal('lib', lib);
for (const [key, value] of Object.entries(lib)) {
    setGlobal(key, value);
}

async function init() {
    const builder = await KiwiBuilder.create(kiwiWasmPath);
    setGlobal('builder', builder);
    
}

setGlobal('modelFiles', modelFiles);
init();
