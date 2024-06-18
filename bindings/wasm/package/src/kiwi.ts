import { AsyncMethods } from './util.js';

interface TokenInfo {
    str: string;
    position: number;
    wordPosition: number;
    sentPosition: number;
    lineNumber: number;
    length: number;
    tag: string;
    score: number;
    typoCost: number;
    typoFormId: number;
    subSentPosition: number;
}

interface TokenResult {
    tokens: TokenInfo[];
    score: number;
}

export enum Match {
    none = 0,
    url = 1 << 0,
    email = 1 << 1,
    hashtag = 1 << 2,
    mention = 1 << 3,
    serial = 1 << 4,
    emoji = 1 << 5,
    normalizeCoda = 1 << 16,
    joinNounPrefix = 1 << 17,
    joinNounSuffix = 1 << 18,
    joinVerbSuffix = 1 << 19,
    joinAdjSuffix = 1 << 20,
    joinAdvSuffix = 1 << 21,
    splitComplex = 1 << 22,
    zCoda = 1 << 23,
    joinVSuffix = joinVerbSuffix | joinAdjSuffix,
    joinAffix = joinNounPrefix |
        joinNounSuffix |
        joinVerbSuffix |
        joinAdjSuffix |
        joinAdvSuffix,
    all = url | email | hashtag | mention | serial | emoji | zCoda,
    allWithNormalizing = all | normalizeCoda,
}

interface SentenceSpan {
    start: number;
    end: number;
}

interface SentenceSplitResult {
    spans: SentenceSpan[];
    tokenResult: TokenResult | null;
}

export enum Space {
    none = 0,
    noSpace = 1,
    insertSpace = 2,
}

export interface SentenceJoinMorph {
    form: string;
    tag: string;
    space?: Space;
}

interface SentenceJoinResult {
    str: string;
    ranges: SentenceSpan[] | null;
}

export interface Kiwi {
    ready: () => boolean;
    isTypoTolerant: () => boolean;
    analyze: (str: string, matchOptions?: Match) => TokenResult;
    analyzeTopN: (
        str: string,
        n: number,
        matchOptions?: Match
    ) => TokenResult[];
    splitIntoSents: (
        str: string,
        matchOptions?: Match,
        withTokenResult?: boolean
    ) => SentenceSplitResult;
    joinSent: (
        morphs: SentenceJoinMorph[],
        lmSearch?: boolean,
        withRanges?: boolean
    ) => SentenceJoinResult;
    getCutOffThreshold: () => number;
    setCutOffThreshold: (v: number) => void;
    getUnkScoreBias: () => number;
    setUnkScoreBias: (v: number) => void;
    getUnkScoreScale: () => number;
    setUnkScoreScale: (v: number) => void;
    getMaxUnkFormSize: () => number;
    setMaxUnkFormSize: (v: number) => void;
    getSpaceTolerance: () => number;
    setSpaceTolerance: (v: number) => void;
    getSpacePenalty: () => number;
    setSpacePenalty: (v: number) => void;
    getTypoCostWeight: () => number;
    setTypoCostWeight: (v: number) => void;
    getIntegrateAllomorphic: () => boolean;
    setIntegrateAllomorphic: (v: boolean) => void;
}

export type KiwiAsync = AsyncMethods<Kiwi>;
