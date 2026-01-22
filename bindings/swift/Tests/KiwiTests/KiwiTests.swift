import XCTest
@testable import Kiwi

final class KiwiTests: XCTestCase {
    
    func testPOSTagDescription() {
        XCTAssertEqual(POSTag.nng.description, "NNG")
        XCTAssertEqual(POSTag.nnp.description, "NNP")
        XCTAssertEqual(POSTag.vv.description, "VV")
        XCTAssertEqual(POSTag.jks.description, "JKS")
    }
    
    func testPOSTagFromString() {
        XCTAssertEqual(POSTag(string: "NNG"), .nng)
        XCTAssertEqual(POSTag(string: "VV"), .vv)
        XCTAssertEqual(POSTag(string: "nng"), .nng)
        XCTAssertNil(POSTag(string: "INVALID"))
    }
    
    func testMatchOptionsBasic() {
        let options: MatchOptions = [.url, .email]
        XCTAssertTrue(options.contains(.url))
        XCTAssertTrue(options.contains(.email))
        XCTAssertFalse(options.contains(.hashtag))
    }
    
    func testMatchOptionsAll() {
        let options = MatchOptions.all
        XCTAssertTrue(options.contains(.url))
        XCTAssertTrue(options.contains(.email))
        XCTAssertTrue(options.contains(.hashtag))
        XCTAssertTrue(options.contains(.mention))
    }
    
    func testDialectOptions() {
        let dialects: Dialect = [.gyeonggi, .jeju]
        XCTAssertTrue(dialects.contains(.gyeonggi))
        XCTAssertTrue(dialects.contains(.jeju))
        XCTAssertFalse(dialects.contains(.gangwon))
    }
    
    func testTokenInitialization() {
        let token = Token(
            form: "테스트",
            tag: .nng,
            position: 0,
            length: 3,
            score: 1.0
        )
        
        XCTAssertEqual(token.form, "테스트")
        XCTAssertEqual(token.tag, .nng)
        XCTAssertEqual(token.position, 0)
        XCTAssertEqual(token.length, 3)
        XCTAssertEqual(token.score, 1.0)
    }
    
    func testTokenDescription() {
        let token = Token(form: "테스트", tag: .nng)
        XCTAssertEqual(token.description, "테스트/NNG")
    }
    
    // Note: Integration tests that require model files are not included
    // as they would require access to actual Kiwi model files
    
    // testVersion() requires the C library to be linked, which is not available
    // in CI without building the C++ library first. For CI, we skip this test.
    // Uncomment when running with a built C library:
    // func testVersion() {
    //     let version = Kiwi.version
    //     XCTAssertFalse(version.isEmpty)
    //     XCTAssertNotEqual(version, "unknown")
    // }
}
