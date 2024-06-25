import { WorkerRequest, WorkerResponse } from './worker.js';
import { TokenInfo } from 'kiwi-nlp';

const elVersion = document.getElementById('version')!;
const elInput = document.getElementById('input') as HTMLInputElement;
const elResultTable = document.getElementById('result') as HTMLTableElement;

const worker = new Worker(
    new URL('./worker.ts', import.meta.url),
    { type: 'module' }
);

worker.onmessage = (event) => {
    const response: WorkerResponse = event.data;

    switch (response.type) {
        case 'inited':
            inited(response.version);
            break;
        case 'analyzed':
            analyzed(response.result, response.text);
            break;
        default:
            console.error('Unknown worker message');
            break;
    }
};

function sendWorkerRequest(request: WorkerRequest) {
    worker.postMessage(request);
}

sendWorkerRequest({ type: 'init' });

function inited(version: string) {
    elVersion.innerText = 'v' + version;

    elInput.hidden = false;
    elInput.addEventListener('input', analyze);
    analyze();
}

function analyze() {
    const text = elInput.value;
    worker.postMessage({ type: 'analyze', text });
}

function analyzed(tokenInfos: TokenInfo[], text: string) {
    while (elResultTable.rows.length > 1) {
        elResultTable.deleteRow(1);
    }

    for (const tokenInfo of tokenInfos) {
        const surface = text.substring(
            tokenInfo.position,
            tokenInfo.position + tokenInfo.length
        );

        const row = elResultTable.insertRow();
        row.insertCell().innerText = tokenInfo.position.toString();
        row.insertCell().innerText = tokenInfo.length.toString();
        row.insertCell().innerText = surface;
        row.insertCell().innerText = tokenInfo.str;
        row.insertCell().innerText = tokenInfo.tag;
        row.insertCell().innerText = tokenInfo.score.toString();
    }

    elResultTable.hidden = tokenInfos.length === 0;
}
