import Foundation

/// Part-of-Speech tag enumeration
public enum POSTag: UInt8, CaseIterable, Codable {
    case unknown = 0
    
    // Nouns
    case nng = 1      // General Noun
    case nnp = 2      // Proper Noun
    case nnb = 3      // Bound Noun
    
    // Verbs
    case vv = 4       // Verb
    case va = 5       // Adjective
    
    // Adverbs
    case mag = 6      // General Adverb
    
    // Numerals
    case nr = 7       // Numeral
    case np = 8       // Pronoun
    
    // Auxiliary
    case vx = 9       // Auxiliary Verb/Adjective
    
    // Determiners
    case mm = 10      // Determiner
    case maj = 11     // Conjunctive Adverb
    
    // Interjections
    case ic = 12      // Interjection
    
    // Prefixes/Suffixes
    case xpn = 13     // Noun Prefix
    case xsn = 14     // Noun Suffix
    case xsv = 15     // Verb Suffix
    case xsa = 16     // Adjective Suffix
    case xsm = 17     // Modifier Suffix
    case xr = 18      // Root
    
    // Copulas
    case vcp = 19     // Positive Copula
    case vcn = 20     // Negative Copula
    
    // Symbols
    case sf = 21      // Final Punctuation (. ! ?)
    case sp = 22      // Pause Punctuation (, · … :)
    case ss = 23      // Quote/Bracket
    case sso = 24     // Opening Quote/Bracket
    case ssc = 25     // Closing Quote/Bracket
    case se = 26      // Ellipsis
    case so = 27      // Other Symbol
    case sw = 28      // Other Symbol
    case sb = 29      // Other Symbol
    case sl = 30      // Foreign Language
    case sh = 31      // Chinese Character
    case sn = 32      // Number
    
    // Web entities
    case w_url = 33   // URL
    case w_email = 34 // Email
    case w_mention = 35 // Mention (@username)
    case w_hashtag = 36 // Hashtag (#tag)
    case w_serial = 37  // Serial Number
    case w_emoji = 38   // Emoji
    
    // Particles
    case jks = 39     // Subject Particle
    case jkc = 40     // Complement Particle
    case jkg = 41     // Adnominal Particle
    case jko = 42     // Object Particle
    case jkb = 43     // Adverbial Particle
    case jkv = 44     // Vocative Particle
    case jkq = 45     // Quotative Particle
    case jx = 46      // Auxiliary Particle
    case jc = 47      // Conjunctive Particle
    
    // Endings
    case ep = 48      // Prefinal Ending
    case ef = 49      // Final Ending
    case ec = 50      // Conjunctive Ending
    case etn = 51     // Nominalizing Ending
    case etm = 52     // Adnominalizing Ending
    
    // Special
    case z_coda = 53  // Coda
    case z_siot = 54  // Siot
    
    // User defined
    case user0 = 55
    case user1 = 56
    case user2 = 57
    case user3 = 58
    case user4 = 59
    
    // Special predicate
    case p = 60
    
    /// String representation of the POS tag
    public var description: String {
        switch self {
        case .unknown: return "UNK"
        case .nng: return "NNG"
        case .nnp: return "NNP"
        case .nnb: return "NNB"
        case .vv: return "VV"
        case .va: return "VA"
        case .mag: return "MAG"
        case .nr: return "NR"
        case .np: return "NP"
        case .vx: return "VX"
        case .mm: return "MM"
        case .maj: return "MAJ"
        case .ic: return "IC"
        case .xpn: return "XPN"
        case .xsn: return "XSN"
        case .xsv: return "XSV"
        case .xsa: return "XSA"
        case .xsm: return "XSM"
        case .xr: return "XR"
        case .vcp: return "VCP"
        case .vcn: return "VCN"
        case .sf: return "SF"
        case .sp: return "SP"
        case .ss: return "SS"
        case .sso: return "SSO"
        case .ssc: return "SSC"
        case .se: return "SE"
        case .so: return "SO"
        case .sw: return "SW"
        case .sb: return "SB"
        case .sl: return "SL"
        case .sh: return "SH"
        case .sn: return "SN"
        case .w_url: return "W_URL"
        case .w_email: return "W_EMAIL"
        case .w_mention: return "W_MENTION"
        case .w_hashtag: return "W_HASHTAG"
        case .w_serial: return "W_SERIAL"
        case .w_emoji: return "W_EMOJI"
        case .jks: return "JKS"
        case .jkc: return "JKC"
        case .jkg: return "JKG"
        case .jko: return "JKO"
        case .jkb: return "JKB"
        case .jkv: return "JKV"
        case .jkq: return "JKQ"
        case .jx: return "JX"
        case .jc: return "JC"
        case .ep: return "EP"
        case .ef: return "EF"
        case .ec: return "EC"
        case .etn: return "ETN"
        case .etm: return "ETM"
        case .z_coda: return "Z_CODA"
        case .z_siot: return "Z_SIOT"
        case .user0: return "USER0"
        case .user1: return "USER1"
        case .user2: return "USER2"
        case .user3: return "USER3"
        case .user4: return "USER4"
        case .p: return "P"
        }
    }
    
    /// Initialize from string tag name
    public init?(string: String) {
        switch string.uppercased() {
        case "UNK", "UNKNOWN": self = .unknown
        case "NNG": self = .nng
        case "NNP": self = .nnp
        case "NNB": self = .nnb
        case "VV": self = .vv
        case "VA": self = .va
        case "MAG": self = .mag
        case "NR": self = .nr
        case "NP": self = .np
        case "VX": self = .vx
        case "MM": self = .mm
        case "MAJ": self = .maj
        case "IC": self = .ic
        case "XPN": self = .xpn
        case "XSN": self = .xsn
        case "XSV": self = .xsv
        case "XSA": self = .xsa
        case "XSM": self = .xsm
        case "XR": self = .xr
        case "VCP": self = .vcp
        case "VCN": self = .vcn
        case "SF": self = .sf
        case "SP": self = .sp
        case "SS": self = .ss
        case "SSO": self = .sso
        case "SSC": self = .ssc
        case "SE": self = .se
        case "SO": self = .so
        case "SW": self = .sw
        case "SB": self = .sb
        case "SL": self = .sl
        case "SH": self = .sh
        case "SN": self = .sn
        case "W_URL": self = .w_url
        case "W_EMAIL": self = .w_email
        case "W_MENTION": self = .w_mention
        case "W_HASHTAG": self = .w_hashtag
        case "W_SERIAL": self = .w_serial
        case "W_EMOJI": self = .w_emoji
        case "JKS": self = .jks
        case "JKC": self = .jkc
        case "JKG": self = .jkg
        case "JKO": self = .jko
        case "JKB": self = .jkb
        case "JKV": self = .jkv
        case "JKQ": self = .jkq
        case "JX": self = .jx
        case "JC": self = .jc
        case "EP": self = .ep
        case "EF": self = .ef
        case "EC": self = .ec
        case "ETN": self = .etn
        case "ETM": self = .etm
        case "Z_CODA": self = .z_coda
        case "Z_SIOT": self = .z_siot
        case "USER0": self = .user0
        case "USER1": self = .user1
        case "USER2": self = .user2
        case "USER3": self = .user3
        case "USER4": self = .user4
        case "P": self = .p
        default: return nil
        }
    }
}

extension POSTag: CustomStringConvertible {}
