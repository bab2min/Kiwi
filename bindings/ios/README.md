# KiwiSwift, 한국어 형태소 분석기 Kiwi의 iOS 바인딩 (개발 예정)

> **⚠️ 주의**: iOS 바인딩은 현재 개발 계획 단계에 있으며, 실제 구현은 아직 완료되지 않았습니다.

## 개발 계획

iOS용 Kiwi 바인딩은 다음과 같은 방향으로 개발될 예정입니다:

### 목표
- **Swift/Objective-C 지원**: 네이티브 iOS 개발 언어 완전 지원
- **CocoaPods/SPM 배포**: 표준 iOS 패키지 매니저를 통한 쉬운 설치
- **iOS 성능 최적화**: 모바일 환경에 최적화된 메모리 사용량과 배터리 효율성
- **동일한 API**: 다른 플랫폼과 일관된 API 제공

### 기술적 요구사항
- **최소 iOS 버전**: iOS 12.0+
- **아키텍처**: ARM64 (iPhone 5s 이후 모든 기기)
- **언어**: Swift 5.0+, Objective-C 호환성
- **프레임워크**: C++ 코어와 Swift/Objective-C 간의 브릿지 구현

### 예상 API 구조

```swift
import KiwiSwift

// Kiwi 인스턴스 생성
let kiwi = try Kiwi(modelPath: "path/to/model")

// 형태소 분석
let tokens = try kiwi.tokenize("안녕하세요!", options: [.normalizeAll])
for token in tokens {
    print("\(token.form) / \(token.tag)")
}

// 문장 분리
let sentences = try kiwi.splitSentences("첫 번째 문장입니다. 두 번째 문장입니다.")
```

### 패키지 배포 계획

#### CocoaPods
```ruby
pod 'KiwiSwift', '~> 0.21.0'
```

#### Swift Package Manager
```swift
dependencies: [
    .package(url: "https://github.com/bab2min/Kiwi.git", from: "0.21.0")
]
```

### 개발 일정

개발 일정은 현재 미정이며, 다음 요소들에 따라 결정될 예정입니다:

1. **커뮤니티 수요**: iOS 개발자들의 관심도와 요청
2. **개발자 참여**: iOS 네이티브 개발 경험이 있는 기여자 참여
3. **기술적 과제**: C++ 코어와 Swift 간 효율적인 브릿지 구현

### 기여 방법

iOS 바인딩 개발에 관심이 있으시다면:

1. **Issue 참여**: [GitHub Issues](https://github.com/bab2min/Kiwi/issues)에서 iOS 관련 논의에 참여
2. **기술 검토**: C++/Objective-C++/Swift 브릿지 구현 방안 제안
3. **프로토타입 개발**: 초기 iOS 바인딩 프로토타입 구현
4. **문서화**: iOS 개발자를 위한 가이드 작성

### 기술적 고려사항

#### 메모리 관리
- iOS의 ARC(Automatic Reference Counting)와 C++ 객체 생명주기 관리
- 대용량 모델 파일의 효율적인 메모리 사용

#### 성능 최적화
- 모바일 CPU에 최적화된 컴파일 옵션
- 배터리 효율성을 고려한 연산 최적화

#### 앱 스토어 정책
- App Store 배포 시 정적 라이브러리 요구사항 준수
- bitcode 지원 고려

## 연락처

iOS 바인딩 개발에 대한 문의나 기여 의사가 있으시면:

- GitHub Issues에 iOS 라벨로 이슈 생성
- 메인테이너(@bab2min)에게 직접 연락
- [기여 가이드](../../CONTRIBUTING.md) 참조

## 관련 링크

- [Kiwi 메인 프로젝트](https://github.com/bab2min/Kiwi)
- [Android 바인딩](../java/README.md)
- [Web Assembly 바인딩](../wasm/README.md)