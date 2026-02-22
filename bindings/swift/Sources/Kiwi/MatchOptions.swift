import Foundation

/// Options for matching patterns in text analysis
public struct MatchOptions: OptionSet, Codable {
    public let rawValue: Int32
    
    public init(rawValue: Int32) {
        self.rawValue = rawValue
    }
    
    /// Match URL patterns
    public static let url = MatchOptions(rawValue: 1 << 0)
    
    /// Match email addresses
    public static let email = MatchOptions(rawValue: 1 << 1)
    
    /// Match hashtags
    public static let hashtag = MatchOptions(rawValue: 1 << 2)
    
    /// Match mentions (@username)
    public static let mention = MatchOptions(rawValue: 1 << 3)
    
    /// Match serial numbers
    public static let serial = MatchOptions(rawValue: 1 << 4)
    
    /// Normalize coda
    public static let normalizeCoda = MatchOptions(rawValue: 1 << 16)
    
    /// Join noun prefix
    public static let joinNounPrefix = MatchOptions(rawValue: 1 << 17)
    
    /// Join noun suffix
    public static let joinNounSuffix = MatchOptions(rawValue: 1 << 18)
    
    /// Join verb suffix
    public static let joinVerbSuffix = MatchOptions(rawValue: 1 << 19)
    
    /// Join adjective suffix
    public static let joinAdjSuffix = MatchOptions(rawValue: 1 << 20)
    
    /// Join adverb suffix
    public static let joinAdvSuffix = MatchOptions(rawValue: 1 << 21)
    
    /// Join verb and adjective suffixes
    public static let joinVSuffix: MatchOptions = [.joinVerbSuffix, .joinAdjSuffix]
    
    /// Join all affixes
    public static let joinAffix: MatchOptions = [.joinNounPrefix, .joinNounSuffix, .joinVerbSuffix, .joinAdjSuffix, .joinAdvSuffix]
    
    /// Split complex morphemes
    public static let splitComplex = MatchOptions(rawValue: 1 << 22)
    
    /// Match Z coda
    public static let zCoda = MatchOptions(rawValue: 1 << 23)
    
    /// Match compatible jamo
    public static let compatibleJamo = MatchOptions(rawValue: 1 << 24)
    
    /// Split saisiot
    public static let splitSaisiot = MatchOptions(rawValue: 1 << 25)
    
    /// Merge saisiot
    public static let mergeSaisiot = MatchOptions(rawValue: 1 << 26)
    
    /// All basic matching options
    public static let all: MatchOptions = [.url, .email, .hashtag, .mention, .serial, .zCoda]
    
    /// All matching options with normalization
    public static let allWithNormalizing: MatchOptions = [.all, .normalizeCoda]
}
