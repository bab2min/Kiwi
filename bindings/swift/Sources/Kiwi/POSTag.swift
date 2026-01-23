import Foundation

/// Part-of-Speech tag enumeration
public enum POSTag: UInt8, CaseIterable, Codable {
    case unknown = 0
    
    // Nouns
    case nng = 1
    case nnp = 2
    case nnb = 3
    
    // Verbs
    case vv = 4
    case va = 5
    
    // Adverbs
    case mag = 6
    
    // Numerals
    case nr = 7
    case np = 8
    
    // Auxiliary
    case vx = 9
    
    // Determiners
    case mm = 10
    case maj = 11
    
    // Interjections
    case ic = 12
    
    // Prefixes/Suffixes
    case xpn = 13
    case xsn = 14
    case xsv = 15
    case xsa = 16
    case xsm = 17
    case xr = 18
    
    // Copulas
    case vcp = 19
    case vcn = 20
    
    // Symbols
    case sf = 21
    case sp = 22
    case ss = 23
    case sso = 24
    case ssc = 25
    case se = 26
    case so = 27
    case sw = 28
    case sb = 29
    case sl = 30
    case sh = 31
    case sn = 32
    
    // Web entities
    case w_url = 33
    case w_email = 34
    case w_mention = 35
    case w_hashtag = 36
    case w_serial = 37
    case w_emoji = 38
    
    // Particles
    case jks = 39
    case jkc = 40
    case jkg = 41
    case jko = 42
    case jkb = 43
    case jkv = 44
    case jkq = 45
    case jx = 46
    case jc = 47
    
    // Endings
    case ep = 48
    case ef = 49
    case ec = 50
    case etn = 51
    case etm = 52
    
    // Special
    case z_coda = 53
    case z_siot = 54
    
    // User defined
    case user0 = 55
    case user1 = 56
    case user2 = 57
    case user3 = 58
    case user4 = 59
    
    // Irregular conjugation tags (base tag | 0x80)
    case vvi = 132
    case vai = 133
    case vxi = 137
    case xsai = 144

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
        case .vvi: return "VV-I"
        case .vai: return "VA-I"
        case .vxi: return "VX-I"
        case .xsai: return "XSA-I"
        }
    }
    
    /// Initialize from string tag name
    public init?(string: String) {
        switch string.uppercased() {
        case "UNK", "UNKNOWN": self = .unknown
        case "NNG": self = .nng
        case "NNP": self = .nnp
        case "NNB": self = .nnb
        case "VV", "VV-R": self = .vv
        case "VA", "VA-R": self = .va
        case "MAG": self = .mag
        case "NR": self = .nr
        case "NP": self = .np
        case "VX", "VX-R": self = .vx
        case "MM": self = .mm
        case "MAJ": self = .maj
        case "IC": self = .ic
        case "XPN": self = .xpn
        case "XSN": self = .xsn
        case "XSV", "XSV-R": self = .xsv
        case "XSA", "XSA-R": self = .xsa
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
        case "VV-I", "VVI": self = .vvi
        case "VA-I", "VAI": self = .vai
        case "VX-I", "VXI": self = .vxi
        case "XSA-I", "XSAI": self = .xsai
        default: return nil
        }
    }

    /// Whether this tag represents an irregular conjugation
    public var isIrregular: Bool {
        return rawValue & 0x80 != 0
    }

    /// Returns the base tag without the irregular flag
    public var baseTag: POSTag {
        if isIrregular {
            return POSTag(rawValue: rawValue & 0x7F) ?? self
        }
        return self
    }

    /// Returns the irregular version of this tag (for VV, VA, VX, XSA, P, PA)
    public var irregularTag: POSTag? {
        switch self {
        case .vv: return .vvi
        case .va: return .vai
        case .vx: return .vxi
        case .xsa: return .xsai
        default: return nil
        }
    }
}

extension POSTag: CustomStringConvertible {}
