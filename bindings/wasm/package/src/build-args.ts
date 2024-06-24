/**
 * A single file to be loaded. The key is the name of the file and the value is the file data.
 * The file data can be a string representing a URL or an ArrayBufferView directly containing the file data.
 */
export type ModelFiles = { [name: string]: ArrayBufferView | string };

export interface BuildArgs {
    /**
     * The model files to load. Required.
     */
    modelFiles: ModelFiles;
    /**
     * If `true`, unify phonological variants.
     * Outputs endings that change form depending on the positivity/negativity of the preceding vowel, such as /아/ and /어/ or /았/ and /었/, as one.
     * Defaults to `true`
     */
    integrateAllomorph?: boolean;
    /**
     * If `true`, the default dictionary is loaded.
     * The default dictionary consists of proper noun headings extracted from Wikipedia and Namuwiki.
     * Defaults to `true`.
     */
    loadDefaultDict?: boolean;
    /**
     * If true, the built-in typo dictionary is loaded.
     * The typo dictionary consists of a subset of common misspellings and variant endings that are commonly used on the internet.
     * Defaults to `true`.
     */
    loadTypoDict?: boolean;
    /**
     * If `true`, the built-in polysemous dictionary is loaded.
     * The polysemous dictionary consists of proper nouns listed in WikiData.
     * Defaults to `true`.
     */
    loadMultiDict?: boolean;
    /**
     * Specifies the language model to use for morphological analysis. Defaults to 'knlm'.
     * - `knlm`: Fast and can model the relationships between morphemes within a short distance (usually two or three) with high accuracy. However, it has the limitation that it cannot take into account the relationships between morphemes over a long distance.
     * - `sbg`: Driven by internally calibrating the results of SkipBigram to the results of KNLM. At a processing time increase of about 30% compared to KNLM, it is able to model relationships between morphemes over large distances (up to 8 real morphemes) with moderate accuracy.
     */
    modelType?: 'knlm' | 'sbg';
    /**
     * The typo information to use for correction. Defaults to none, which disables typo correction.
     */
    typos?: 'basic' | 'continual' | 'basic_with_continual';
    /**
     * The maximum typo cost to consider when correcting typos. Typos beyond this cost will not be explored. Defaults to 2.5.
     */
    typoCostThreshold?: number;
};
