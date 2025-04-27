/**
 * A single file to be loaded. The key is the name of the file and the value is the file data.
 * The file data can be a string representing a URL or an ArrayBufferView directly containing the file data.
 */
export type ModelFiles = { [name: string]: ArrayBufferView | string };

/**
 * A single user word to add.
 */
export interface UserWord {
    /**
     * The word to add.
     */
    word: string;
    /**
     * Part-of-speech tag. Defaults to 'NNP'.
     */
    tag?: string;
    /**
     * The weighted score of the morpheme to add.
     * If there are multiple morpheme combinations that match the form,the word with the higher score will be prioritized.
     * Defaults to 0.
     */
    score?: number;
    /**
     * The original morpheme of the morpheme to be added.
     * If the morpheme to be added is a variant of a particular morpheme, the original morpheme can be passed as this argument.
     * If it is not present, it can be omitted.
     */
    origWord?: string;
};

export interface PreanalyzedToken {
    /**
     * Form of the token.
     */
    form: string;
    /**
     * Part-of-speech tag of the token.
     */
    tag: string;
    /**
     * Start position of the token in the preanalyzed word. If omitted, all token positions are automatically calculated.
     */
    start?: number;
    /**
     * Ebd position of the token in the preanalyzed word. If omitted, all token positions are automatically calculated.
     */
    end?: number;
}

export interface PreanalyzedWord {
    /**
     * Form to add.
     */
    form: string;
    /**
     * The result of the morphological analysis of form.
     */
    analyzed: PreanalyzedToken[];
    /**
     * The weighted score of the morpheme sequence to add.
     * If there are multiple morpheme combinations that match the form, the word with the higher score will be prioritized.
     */
    score?: number;
}

export interface TypoDefinition {
    /**
     * Source strings
     */
    orig: string[];
    /**
     * The typos to be replaced
     */
    error: string[];
    /**
     * Replacement cost. Defaults to 1.
     */
    cost?: number;
    /**
     * Conditions under which typos can be replaced.
     * One of `none`, `any` (after any letter), `vowel` (after a vowel), or `applosive` (after an applosive).
     * Defaults to `none` when omitted.
     */
    condition?: "none" | "any" | "vowel" | "applosive";
}

export interface TypoTransformer {
    /**
     * A list of {@link TypoDefinition} that define typo generation rules.
     */
    defs: TypoDefinition[];
    /**
     * The cost of continual typos. Defaults to 1.
     */
    continualTypoCost?: number;
}

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
     * Additional user dictionaries to load. Used files must appear in the `modelFiles` object.
     */
    userDicts?: string[];
    /**
     * Additional user words to load.
     */
    userWords?: UserWord[];
    /**
     * Preanalyzed words to load.
     */
    preanalyzedWords?: PreanalyzedWord[];
    /**
     * Specifies the language model to use for morphological analysis. Defaults to 'none'.
     * - `none`: Kiwi selects the model type automatically. If there are multiple models available, the fastest model is selected.
     * - `largest`: Kiwi selects the model type automatically. If there are multiple models available, the largest model is selected.
     * - `knlm`: Fast and can model the relationships between morphemes within a short distance (usually two or three) with high accuracy. However, it has the limitation that it cannot take into account the relationships between morphemes over a long distance.
     * - `sbg`: Driven by internally calibrating the results of SkipBigram to the results of KNLM. At a processing time increase of about 30% compared to KNLM, it is able to model relationships between morphemes over large distances (up to 8 real morphemes) with moderate accuracy.
     * - `cong`: (experimental) Contextual N-gram embedding Language Model.  It consists of lightweighted neural networks that can estimate the relationships between morphemes.
     * - `cong-global`: (experimental) Contextual N-gram embedding Language Model. It consists of lightweighted neural networks that can estimate the relationships between morphemes over large distances (up to 7 real morphemes) with high accuracy.
     */
    modelType?: 'none' | 'largest' | 'knlm' | 'sbg' | 'cong' | 'cong-global';
    /**
     * The typo information to use for correction.
     * Can be one of the built in `none`, `basic`, `continual`, `basicWithContinual` typo sets, or a custom {@link TypoTransformer}.
     * Defaults to `none`, which disables typo correction.
     */
    typos?: 'none' | 'basic' | 'continual' | 'basicWithContinual' | TypoTransformer;
    /**
     * The maximum typo cost to consider when correcting typos. Typos beyond this cost will not be explored. Defaults to 2.5.
     */
    typoCostThreshold?: number;
};
