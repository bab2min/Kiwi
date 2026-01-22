import Foundation
import CKiwi

/// Main Kiwi morphological analyzer class
public final class Kiwi {
    private var wrapper: HandleWrapper<kiwi_h>?
    
    internal init(handle: kiwi_h) {
        self.wrapper = HandleWrapper(handle) { kiwi_close($0) }
    }
    
    /// Get Kiwi version string
    public static var version: String {
        if let versionPtr = kiwi_version() {
            return String(cString: versionPtr)
        }
        return "unknown"
    }
    
    /// Analyze text and return morphological analysis results
    /// - Parameters:
    ///   - text: Text to analyze
    ///   - topN: Number of top results to return (default: 1)
    ///   - options: Match options (default: .allWithNormalizing)
    /// - Returns: Array of token result candidates
    /// - Throws: KiwiError if analysis fails
    public func analyze(
        _ text: String,
        topN: Int = 1,
        options: MatchOptions = .allWithNormalizing
    ) throws -> [TokenResult] {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        var analyzeOption = kiwi_analyze_option_t()
        analyzeOption.match_options = options.rawValue
        analyzeOption.blocklist = nil
        analyzeOption.open_ending = 0
        analyzeOption.allowed_dialects = 0
        analyzeOption.dialect_cost = 3.0
        
        guard let result = kiwi_analyze(handle, text, Int32(topN), analyzeOption, nil) else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Analysis failed")
        }
        
        defer { kiwi_res_close(result) }
        
        let resultSize = kiwi_res_size(result)
        guard resultSize >= 0 else {
            throw KiwiError.operationFailed("Invalid result size")
        }
        
        var results: [TokenResult] = []
        results.reserveCapacity(Int(resultSize))
        
        for i in 0..<resultSize {
            let prob = kiwi_res_prob(result, i)
            let wordNum = kiwi_res_word_num(result, i)
            guard wordNum >= 0 else {
                continue
            }
            
            var tokens: [Token] = []
            tokens.reserveCapacity(Int(wordNum))
            
            for j in 0..<wordNum {
                if let formPtr = kiwi_res_form(result, i, j),
                   let tokenInfo = kiwi_res_token_info(result, i, j) {
                    let form = String(cString: formPtr)
                    let token = Token(form: form, tokenInfo: tokenInfo.pointee)
                    tokens.append(token)
                }
            }
            
            results.append(TokenResult(score: prob, tokens: tokens))
        }
        
        return results
    }
    
    /// Tokenize text and return simple token array (uses best analysis result)
    /// - Parameters:
    ///   - text: Text to tokenize
    ///   - options: Match options (default: .allWithNormalizing)
    /// - Returns: Array of tokens
    /// - Throws: KiwiError if tokenization fails
    public func tokenize(
        _ text: String,
        options: MatchOptions = .allWithNormalizing
    ) throws -> [Token] {
        let results = try analyze(text, topN: 1, options: options)
        return results.first?.tokens ?? []
    }
    
    /// Split text into sentences
    /// - Parameters:
    ///   - text: Text to split
    ///   - options: Match options (default: .all)
    /// - Returns: Array of sentences
    /// - Throws: KiwiError if splitting fails
    public func splitIntoSentences(
        _ text: String,
        options: MatchOptions = .all
    ) throws -> [Sentence] {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        guard let result = kiwi_split_into_sents(handle, text, options.rawValue, nil) else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Sentence splitting failed")
        }
        
        defer { kiwi_ss_close(result) }
        
        let sentenceCount = kiwi_ss_size(result)
        guard sentenceCount >= 0 else {
            throw KiwiError.operationFailed("Invalid sentence count")
        }
        
        var sentences: [Sentence] = []
        sentences.reserveCapacity(Int(sentenceCount))
        
        for i in 0..<sentenceCount {
            let start = kiwi_ss_begin_position(result, i)
            let end = kiwi_ss_end_position(result, i)
            
            if start >= 0 && end >= start {
                let startIdx = text.utf8.index(text.utf8.startIndex, offsetBy: Int(start))
                let endIdx = text.utf8.index(text.utf8.startIndex, offsetBy: Int(end))
                let sentenceText = String(text[startIdx..<endIdx])
                
                sentences.append(Sentence(
                    text: sentenceText,
                    start: Int(start),
                    length: Int(end - start)
                ))
            }
        }
        
        return sentences
    }
    
    /// Create a new Joiner for combining morphemes into text
    /// - Parameter useLMSearch: Use language model search for optimal POS selection (default: true)
    /// - Returns: A new Joiner instance
    public func createJoiner(useLMSearch: Bool = true) -> Joiner {
        guard let handle = wrapper?.handle else {
            fatalError("Invalid Kiwi handle")
        }
        
        guard let joinerHandle = kiwi_new_joiner(handle, useLMSearch ? 1 : 0) else {
            fatalError("Failed to create joiner")
        }
        
        return Joiner(handle: joinerHandle)
    }
    
    /// Create a new MorphemeSet (for use as blacklist in analysis)
    /// - Returns: A new MorphemeSet instance
    /// - Throws: KiwiError if creation fails
    public func createMorphemeSet() throws -> MorphemeSet {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        guard let morphsetHandle = kiwi_new_morphset(handle) else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to create morpheme set")
        }
        
        return MorphemeSet(handle: morphsetHandle)
    }
}
