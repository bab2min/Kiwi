import Foundation
import CKiwi

/// Represents a morphological token in the analyzed text
public struct Token: Codable {
    /// The surface form of the token
    public let form: String
    
    /// Part-of-speech tag
    public let tag: POSTag
    
    /// Character position in the original text (UTF-16 based)
    public let position: Int
    
    /// Length of the token (UTF-16 based)
    public let length: Int
    
    /// Language model score for this token
    public let score: Float
    
    /// Word position (space-delimited)
    public let wordPosition: Int
    
    /// Sentence position
    public let sentencePosition: Int
    
    /// Line number
    public let lineNumber: Int
    
    /// Typo cost (0 if not corrected)
    public let typoCost: Float
    
    /// Paired token index for SSO/SSC tags (-1 if none)
    public let pairedToken: Int
    
    /// Sub-sentence position (0 if not in sub-sentence)
    public let subSentencePosition: Int
    
    /// Dialect information
    public let dialect: Dialect
    
    internal init(form: String, tokenInfo: kiwi_token_info_t) {
        self.form = form
        self.tag = POSTag(rawValue: tokenInfo.tag) ?? .unknown
        self.position = Int(tokenInfo.chr_position)
        self.length = Int(tokenInfo.length)
        self.score = tokenInfo.score
        self.wordPosition = Int(tokenInfo.word_position)
        self.sentencePosition = Int(tokenInfo.sent_position)
        self.lineNumber = Int(tokenInfo.line_number)
        self.typoCost = tokenInfo.typo_cost
        self.pairedToken = Int(tokenInfo.paired_token)
        self.subSentencePosition = Int(tokenInfo.sub_sent_position)
        self.dialect = Dialect(rawValue: Int32(tokenInfo.dialect))
    }
    
    public init(
        form: String,
        tag: POSTag,
        position: Int = 0,
        length: Int = 0,
        score: Float = 0.0,
        wordPosition: Int = 0,
        sentencePosition: Int = 0,
        lineNumber: Int = 0,
        typoCost: Float = 0.0,
        pairedToken: Int = -1,
        subSentencePosition: Int = 0,
        dialect: Dialect = .standard
    ) {
        self.form = form
        self.tag = tag
        self.position = position
        self.length = length
        self.score = score
        self.wordPosition = wordPosition
        self.sentencePosition = sentencePosition
        self.lineNumber = lineNumber
        self.typoCost = typoCost
        self.pairedToken = pairedToken
        self.subSentencePosition = subSentencePosition
        self.dialect = dialect
    }
}

extension Token: CustomStringConvertible {
    public var description: String {
        return "\(form)/\(tag.description)"
    }
}

/// Result from analysis containing multiple token candidates
public struct TokenResult: Codable {
    /// Probability score for this analysis result
    public let score: Float
    
    /// Array of tokens in this analysis
    public let tokens: [Token]
    
    public init(score: Float, tokens: [Token]) {
        self.score = score
        self.tokens = tokens
    }
}

/// Represents a sentence in the split result
public struct Sentence: Codable {
    /// The sentence text
    public let text: String
    
    /// Starting position in original text
    public let start: Int
    
    /// Length of the sentence
    public let length: Int
    
    public init(text: String, start: Int, length: Int) {
        self.text = text
        self.start = start
        self.length = length
    }
}
