import { AsyncMethods } from './util.js';

/**
 * Describes a single morpheme in the input string of the morphological analysis.
 */
export interface TokenInfo {
    /**
     * The form of the morpheme.
     */
    str: string;
    /**
     * The start position in the input string.
     */
    position: number;
    /**
     * Word index in the input string (space based).
     */
    wordPosition: number;
    /**
     * Sentence index in the input string.
     */
    sentPosition: number;
    /**
     * Line index in the input string.
     */
    lineNumber: number;
    /**
     * Length of the morpheme in the input string.
     */
    length: number;
    /**
     * Part of speech tag of the morpheme.
     */
    tag: string;
    /**
     * Language model score of the morpheme.
     */
    score: number;
    /**
     * Cost of the typo that was corrected. If no typo correction was performed, this value is 0.
     */
    typoCost: number;
    /**
     * Typo correction form if typo correction was performed. Id of pretokenized span if no typo correction was performed.
     */
    typoFormId: number;
    /**
     * For morphemes belonging to SSO, SSC part of speech tags, the position of the paired morpheme (-1 means no corresponding morpheme).
     */
    pairedToken: number;
    /**
     * The index of the sub-sentence enclosed in quotation marks or parentheses. Starts at 1. A value of 0 indicates that it is not a subordinate sentence.
     */
    subSentPosition: number;
    /**
     * The id of the morpheme information in the used Kiwi object. -1 indicates OOV.
     */
    morphId: number;

}

export interface TokenResult {
    /**
     * Array of `TokenInfo` objects representing the morphemes in the input string.
     */
    tokens: TokenInfo[];
    /**
     * The score of the morphological analysis result.
     */
    score: number;
}

/**
 * Describes matching options when performing morphological analysis.
 * These options can be combined using the bitwise OR operator.
 */
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
    compatibleJamo = 1 << 24,
    joinVSuffix = joinVerbSuffix | joinAdjSuffix,
    joinAffix = joinNounPrefix |
        joinNounSuffix |
        joinVerbSuffix |
        joinAdjSuffix |
        joinAdvSuffix,
    all = url | email | hashtag | mention | serial | emoji | zCoda,
    allWithNormalizing = all | normalizeCoda,
}

export interface SentenceSpan {
    start: number;
    end: number;
}

export interface SentenceSplitResult {
    /**
     * Array of `SentenceSpan` objects representing the start and end positions of each sentence.
     */
    spans: SentenceSpan[];
    /**
     * Array of `TokenResult` objects representing the morphological analysis result of the input string.
     */
    tokenResult: TokenResult | null;
}

export enum Space {
    none = 0,
    noSpace = 1,
    insertSpace = 2,
}

export interface Morph {
    form: string;
    tag: string;
}

export interface SentenceJoinMorph extends Morph {
    space?: Space;
}

export interface SentenceJoinResult {
    str: string;
    ranges: SentenceSpan[] | null;
}

export type MorphemeSet = number;

export interface PretokenizedToken extends Morph {
    start: number;
    end: number;
}

export interface PretokenizedSpan {
    start: number;
    end: number;
    tokenization: PretokenizedToken[];
}

/**
 * Interface that performs the actual morphological analysis.
 * Cannot be constructed directly, use {@link KiwiBuilder} to create a new instance.
 */
export interface Kiwi {
    /**
     * Tells whether the current Kiwi object is ready to perform morphological analysis.
     * @returns `true` if it is ready for morphological analysis.
     */
    ready: () => boolean;
    /**
     * Tells you if the current Kiwi object was created with typo correction turned on.
     * @returns `true` if typo correction is turned on.
     */
    isTypoTolerant: () => boolean;
    /**
     * Performs morphological analysis. Returns a single list of tokens along with an analysis score. Use `tokenize` if the result score is not needed. Use `analyzeTopN` if you need multiple results.
     * @param str String to analyze
     * @param matchOptions Specifies the special string pattern extracted. This can be set to any combination of `Match` by using the bitwise OR operator.
     * @param blockList Specifies a list of morphemes to prohibit from appearing as candidates in the analysis.
     * @param pretokenized Predefines the result of morphological analysis of a specific segment of text prior to morphological analysis. The section of text defined by this value will always be tokenized in that way only.
     * @returns A single `TokenResult` object.
     */
    analyze: (
        str: string,
        matchOptions?: Match,
        blockList?: Morph[] | MorphemeSet,
        pretokenized?: PretokenizedSpan[]
    ) => TokenResult;
    /**
     * Performs morphological analysis. Returns multiple list of tokens along with an analysis score. Use `tokenizeTopN` if the result scores are not needed. Use `analyze` if you need only one result.
     * @param str String to analyze
     * @param n Number of results to return
     * @param matchOptions Specifies the special string pattern extracted. This can be set to any combination of `Match` by using the bitwise OR operator.
     * @param blockList Specifies a list of morphemes to prohibit from appearing as candidates in the analysis.
     * @param pretokenized Predefines the result of morphological analysis of a specific segment of text prior to morphological analysis. The section of text defined by this value will always be tokenized in that way only.
     * @returns A list of `TokenResult` objects.
     */
    analyzeTopN: (
        str: string,
        n: number,
        matchOptions?: Match,
        blockList?: Morph[] | MorphemeSet,
        pretokenized?: PretokenizedSpan[]
    ) => TokenResult[];
    /**
     * Performs morphological analysis. Returns a single list of tokens. Use `analyze` if the result score is needed. Use `tokenizeTopN` if you need multiple results.
     * @param str String to analyze
     * @param matchOptions Specifies the special string pattern extracted. This can be set to any combination of `Match` by using the bitwise OR operator.
     * @param blockList Specifies a list of morphemes to prohibit from appearing as candidates in the analysis.
     * @param pretokenized Predefines the result of morphological analysis of a specific segment of text prior to morphological analysis. The section of text defined by this value will always be tokenized in that way only.
     * @returns A list of `TokenInfo` object.
     */
    tokenize: (
        str: string,
        matchOptions?: Match,
        blockList?: Morph[] | MorphemeSet,
        pretokenized?: PretokenizedSpan[]
    ) => TokenInfo[];
    /**
     * Performs morphological analysis. Returns multiple lists of tokens. Use `analyzeTopN` if the result scores are needed. Use `tokenize` if you need only one result.
     * @param str String to analyze
     * @param n Number of results to return
     * @param matchOptions Specifies the special string pattern extracted. This can be set to any combination of `Match` by using the bitwise OR operator.
     * @param blockList Specifies a list of morphemes to prohibit from appearing as candidates in the analysis.
     * @param pretokenized Predefines the result of morphological analysis of a specific segment of text prior to morphological analysis. The section of text defined by this value will always be tokenized in that way only.
     * @returns A list of lists of `TokenInfo` objects.
     */
    tokenizeTopN: (
        str: string,
        n: number,
        matchOptions?: Match,
        blockList?: Morph[] | MorphemeSet,
        pretokenized?: PretokenizedSpan[]
    ) => TokenInfo[][];
    /**
     * Returns the input text split into sentences. This method uses stemming internally during the sentence splitting process, so it can also be used to get stemming results simultaneously with sentence splitting.
     * @param str String to split
     * @param matchOptions Specifies the special string pattern extracted. This can be set to any combination of `Match` by using the bitwise OR operator.
     * @param withTokenResult Specifies whether to include the result of morphological analysis in the returned `SentenceSplitResult` object.
     * @returns A `SentenceSplitResult` object.
     */
    splitIntoSents: (
        str: string,
        matchOptions?: Match,
        withTokenResult?: boolean
    ) => SentenceSplitResult;
    /**
     * Combines morphemes and restores them to a sentence. Endings are changed to the appropriate form to match the preceding morpheme.
     * @param morphs List of morphemes to combine
     * @param lmSearch When there is an ambiguous morpheme that can be restored in more than one form, if this value is `true`, the language model is explored to select the best form. If `false`, no exploration is performed, but restoration is faster.
     * @param withRanges Wehther to include the ranges of the morphemes in the returned `SentenceJoinResult` object.
     * @returns 
     */
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
    /**
     * Creates a reusable morpheme set from a list of morphemes. This is intended to be used as the `blockList` parameter for the analyse and tokenize methods.
     * NOTE: The morpheme set must be destroyed using `destroyMorphemeSet` when it is no longer needed. Otherwise, it will cause a memory leak.
     * If you are using the morpheme set only once, you can pass the morpheme list directly to the `blockList` parameter instead of creating a morpheme set.
     * @param morphs List of morphemes to create a set from
     * @returns an handle to the created morpheme set
     */
    createMorphemeSet: (morphs: Morph[]) => MorphemeSet;
    /**
     * Destroys a morpheme set created by `createMorphemeSet`.
     * @param id Handle to the morpheme set to destroy
     */
    destroyMorphemeSet: (id: MorphemeSet) => void;
}

/**
 * Interface that performs the actual morphological analysis.
 * Same as `Kiwi`, but with all methods returning promises. This can be used when the original `Kiwi` object is constructed with a Web Worker.
 * Cannot be constructed directly.
 */
export type KiwiAsync = AsyncMethods<Kiwi>;
