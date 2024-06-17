# kiwi-nlp, 한국어 형태소 분석기 Kiwi의 JavaScript 바인딩

## JavaScript API

```javascript
import { KiwiBuilder, Match } from 'kiwi-nlp';

async function example() {
    const builder = await KiwiBuilder.create('path to kiwi-wasm.wasm');

    const kiwi = await builder.build({
        modelFiles: {
            'combiningRule.txt': '/path/to/model/combiningRule.txt',
            'default.dict': '/path/to/model/default.dict',
            'extract.mdl': '/path/to/model/extract.mdl',
            'multi.dict': '/path/to/model/multi.dict',
            'sj.knlm': '/path/to/model/sj.knlm',
            'sj.morph': '/path/to/model/sj.morph',
            'skipbigram.mdl': '/path/to/model/skipbigram.mdl',
            'typo.dict': '/path/to/model/typo.dict',
        }
    });

    const tokens = kiwi.analyze('다음은 예시 텍스트입니다.', Match.allWithNormalizing);
    /* Output: {
        "score": -39.772212982177734,
        "tokens": [
            {
                "length": 2,
                "lineNumber": 0,
                "pairedToken": 4294967295,
                "position": 0,
                "score": -6.5904083251953125,
                "sentPosition": 0,
                "str": "다음",
                "subSentPosition": 0,
                "tag": "NNG",
                "typoCost": 0,
                "typoFormId": 0,
                "wordPosition": 0
            },
            {
                "length": 1,
                "lineNumber": 0,
                "pairedToken": 4294967295,
                "position": 2,
                "score": -1.844599723815918,
                "sentPosition": 0,
                "str": "은",
                "subSentPosition": 0,
                "tag": "JX",
                "typoCost": 0,
                "typoFormId": 0,
                "wordPosition": 0
            },
            ...
        ]
    } */
}
```
