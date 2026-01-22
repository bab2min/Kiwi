import Foundation

/// Korean dialect flags
public struct Dialect: OptionSet, Codable {
    public let rawValue: Int32
    
    public init(rawValue: Int32) {
        self.rawValue = rawValue
    }
    
    /// Standard Korean (표준어)
    public static let standard = Dialect(rawValue: 0)
    
    /// Gyeonggi dialect (경기 방언)
    public static let gyeonggi = Dialect(rawValue: 1 << 0)
    
    /// Chungcheong dialect (충청 방언)
    public static let chungcheong = Dialect(rawValue: 1 << 1)
    
    /// Gangwon dialect (강원 방언)
    public static let gangwon = Dialect(rawValue: 1 << 2)
    
    /// Gyeongsang dialect (경상 방언)
    public static let gyeongsang = Dialect(rawValue: 1 << 3)
    
    /// Jeolla dialect (전라 방언)
    public static let jeolla = Dialect(rawValue: 1 << 4)
    
    /// Jeju dialect (제주 방언)
    public static let jeju = Dialect(rawValue: 1 << 5)
    
    /// Hwanghae dialect (황해 방언)
    public static let hwanghae = Dialect(rawValue: 1 << 6)
    
    /// Hamgyeong dialect (함경 방언)
    public static let hamgyeong = Dialect(rawValue: 1 << 7)
    
    /// Pyeongan dialect (평안 방언)
    public static let pyeongan = Dialect(rawValue: 1 << 8)
    
    /// Archaic Korean (고어)
    public static let archaic = Dialect(rawValue: 1 << 9)
    
    /// All dialects
    public static let all = Dialect(rawValue: (1 << 10) - 1)
}
