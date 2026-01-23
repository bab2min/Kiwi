# Kiwi Swift 바인딩

한국어 형태소 분석기 Kiwi의 Swift 바인딩입니다. iOS 및 macOS 앱에서 한국어 자연어 처리를 수행할 수 있습니다.

## 목차

- [요구 사항](#요구-사항)
- [설치](#설치)
- [모델 파일 설정](#모델-파일-설정)
- [기본 사용법](#기본-사용법)
- [고급 기능](#고급-기능)
- [API 레퍼런스](#api-레퍼런스)
- [품사 태그](#품사-태그)

## 요구 사항

- iOS 12.0+ / macOS 10.14+
- Swift 5.7+
- Xcode 14.0+

## 설치

### Swift Package Manager (권장)

#### 방법 1: Xcode에서 추가

1. Xcode에서 **File → Add Package Dependencies...** 선택
2. 저장소 URL 입력: `https://github.com/bab2min/Kiwi.git`
3. 버전 선택 후 **Add Package** 클릭

#### 방법 2: Package.swift에 직접 추가

```swift
// Package.swift
dependencies: [
    .package(url: "https://github.com/bab2min/Kiwi.git", from: "0.22.0")
],
targets: [
    .target(
        name: "YourApp",
        dependencies: ["Kiwi"]
    )
]
```

## 모델 파일 설정

Kiwi를 사용하려면 모델 파일이 필요합니다. 모델 파일은 [Kiwi 릴리즈 페이지](https://github.com/bab2min/Kiwi/releases)에서 다운로드할 수 있습니다.

### iOS/macOS 앱에서 모델 번들링

1. 모델 폴더를 Xcode 프로젝트에 드래그하여 추가
2. **Copy items if needed** 체크
3. **Create folder references** 선택 (중요!)
4. 타겟에 추가되었는지 확인

```
YourApp/
├── Resources/
│   └── KiwiModels/          ← 모델 폴더
│       ├── combiningRule.txt
│       ├── default.dict
│       ├── extract.mdl
│       └── ...
```

## 기본 사용법

### 형태소 분석

```swift
import Kiwi

do {
    // 1. KiwiBuilder 생성 (번들에서 모델 로드)
    let builder = try KiwiBuilder(
        bundle: .main,
        modelDirectory: "KiwiModels"
    )

    // 2. Kiwi 인스턴스 빌드
    let kiwi = try builder.build()

    // 3. 형태소 분석
    let tokens = try kiwi.tokenize("안녕하세요, 키위 형태소 분석기입니다!")

    for token in tokens {
        print("\(token.form)/\(token.tag.description)")
    }
    // 출력:
    // 안녕/NNG
    // 하/XSA
    // 시/EP
    // 어요/EF
    // ,/SP
    // 키위/NNG
    // 형태소/NNG
    // 분석기/NNG
    // 이/VCP
    // ㅂ니다/EF
    // !/SF

} catch {
    print("오류: \(error)")
}
```

### 경로로 모델 로드

```swift
// 직접 경로 지정
let builder = try KiwiBuilder(
    modelPath: "/path/to/models",
    numThreads: 4  // 스레드 수 지정 (-1: 자동)
)
```

### 다중 분석 결과 얻기

```swift
// topN 개의 분석 후보 반환
let results = try kiwi.analyze("감기는 감기다", topN: 3)

for (index, result) in results.enumerated() {
    print("후보 \(index + 1) (점수: \(result.score)):")
    for token in result.tokens {
        print("  \(token.form)/\(token.tag.description)")
    }
}
```

### 문장 분리

```swift
let text = "안녕하세요. 키위입니다. 형태소 분석을 합니다."
let sentences = try kiwi.splitIntoSentences(text)

for sentence in sentences {
    print("문장: \(sentence.text)")
    print("  시작: \(sentence.start), 길이: \(sentence.length)")
}
// 출력:
// 문장: 안녕하세요.
//   시작: 0, 길이: 18
// 문장: 키위입니다.
//   시작: 19, 길이: 16
// 문장: 형태소 분석을 합니다.
//   시작: 36, 길이: 28
```

## 고급 기능

### 사용자 사전 추가

```swift
let builder = try KiwiBuilder(bundle: .main, modelDirectory: "KiwiModels")

// 단어 직접 추가
try builder.addWord("키위피", tag: .nnp, score: 0.0)  // 고유명사로 추가
try builder.addWord("딥러닝", tag: .nng, score: 0.0)  // 일반명사로 추가

// 사전 파일 로드 (탭으로 구분된 형식: 단어\t품사\t점수)
try builder.loadDict("/path/to/user_dict.txt")

let kiwi = try builder.build()
```

### 분석 옵션 설정

```swift
// 기본 옵션으로 분석
let tokens1 = try kiwi.tokenize("www.example.com", options: .all)

// URL, 이메일 등 패턴 매칭 + 정규화
let tokens2 = try kiwi.tokenize("www.example.com", options: .allWithNormalizing)

// 개별 옵션 조합
let customOptions: MatchOptions = [.url, .email, .normalizeCoda]
let tokens3 = try kiwi.tokenize("test@test.com", options: customOptions)
```

**MatchOptions 목록:**

| 옵션 | 설명 |
|------|------|
| `.url` | URL 패턴 인식 |
| `.email` | 이메일 패턴 인식 |
| `.hashtag` | 해시태그 인식 |
| `.mention` | 멘션(@) 인식 |
| `.serial` | 일련번호 인식 |
| `.normalizeCoda` | 받침 정규화 (잇다 → 있다) |
| `.joinNounPrefix` | 체언 접두사 결합 |
| `.joinNounSuffix` | 체언 접미사 결합 |
| `.joinVerbSuffix` | 동사 접미사 결합 |
| `.joinAdjSuffix` | 형용사 접미사 결합 |
| `.splitComplex` | 복합 형태소 분리 |
| `.all` | 기본 전체 옵션 |
| `.allWithNormalizing` | 전체 + 정규화 |

### 방언 지원

```swift
let builder = try KiwiBuilder(
    bundle: .main,
    modelDirectory: "KiwiModels",
    enabledDialects: [.standard, .gyeongsang, .jeolla]
)

let kiwi = try builder.build()
```

**Dialect 목록:**

| 옵션 | 설명 |
|------|------|
| `.standard` | 표준어 (기본) |
| `.gyeonggi` | 경기 방언 |
| `.chungcheong` | 충청 방언 |
| `.gangwon` | 강원 방언 |
| `.gyeongsang` | 경상 방언 |
| `.jeolla` | 전라 방언 |
| `.jeju` | 제주 방언 |
| `.hwanghae` | 황해 방언 |
| `.hamgyeong` | 함경 방언 |
| `.pyeongan` | 평안 방언 |
| `.archaic` | 고어 |

### 오타 교정

```swift
// 기본 오타 교정기 사용
let typoTransformer = try TypoTransformer.basic()

let kiwi = try builder.build(
    typoTransformer: typoTransformer,
    typoCostThreshold: 2.5
)

let tokens = try kiwi.tokenize("장례희망이 뭐야?")  // 오타 자동 교정
```

**TypoTransformer 유형:**

```swift
// 빈 트랜스포머
let empty = try TypoTransformer()

// 기본 오타 세트
let basic = try TypoTransformer.basic()

// 다양한 오타 세트
let continual = try TypoTransformer.default(.continualTypoSet)
let withLengthening = try TypoTransformer.default(.basicTypoSetWithContinualAndLengthening)
```

### 형태소 결합 (Joiner)

형태소를 결합하여 자연스러운 문장을 생성합니다.

```swift
let joiner = try kiwi.createJoiner()

try joiner.add(form: "먹", tag: .vv)      // 동사 어간
try joiner.add(form: "었", tag: .ep)      // 선어말 어미
try joiner.add(form: "다", tag: .ef)      // 종결 어미

let text = try joiner.join()
print(text)  // "먹었다"
```

```swift
// 불규칙 활용 자동 처리
let joiner = try kiwi.createJoiner()

try joiner.add(form: "듣", tag: .vvi)      // ㄷ불규칙 동사
try joiner.add(form: "어", tag: .ec)

let text = try joiner.join()
print(text)  // "들어" (ㄷ → ㄹ 불규칙 적용)
```

### 형태소 블랙리스트

특정 형태소를 분석에서 제외합니다.

```swift
let morphset = try kiwi.createMorphemeSet()
try morphset.add(form: "가", tag: .jks)  // 주격조사 '가' 제외

// analyze 시 blocklist로 사용 (향후 지원 예정)
```

### Token 정보 활용

```swift
let tokens = try kiwi.tokenize("서울에서 부산까지")

for token in tokens {
    print("""
    형태: \(token.form)
    품사: \(token.tag.description)
    위치: \(token.position) (길이: \(token.length))
    어절 번호: \(token.wordPosition)
    문장 번호: \(token.sentencePosition)
    점수: \(token.score)
    오타 비용: \(token.typoCost)
    """)
}
```

### JSON 직렬화

`Token`, `TokenResult`, `Sentence`는 모두 `Codable`을 준수합니다.

```swift
let tokens = try kiwi.tokenize("안녕하세요")

let encoder = JSONEncoder()
encoder.outputFormatting = .prettyPrinted

let jsonData = try encoder.encode(tokens)
let jsonString = String(data: jsonData, encoding: .utf8)!
print(jsonString)
```

## API 레퍼런스

### KiwiBuilder

| 메서드 | 설명 |
|--------|------|
| `init(modelPath:numThreads:options:enabledDialects:)` | 경로로 초기화 |
| `init(bundle:modelDirectory:numThreads:options:enabledDialects:)` | 번들로 초기화 |
| `addWord(_:tag:score:)` | 사용자 단어 추가 |
| `loadDict(_:)` | 사전 파일 로드 |
| `build(typoTransformer:typoCostThreshold:)` | Kiwi 인스턴스 생성 |

### Kiwi

| 메서드/프로퍼티 | 설명 |
|----------------|------|
| `version` (static) | Kiwi 버전 문자열 |
| `analyze(_:topN:options:)` | 형태소 분석 (다중 결과) |
| `tokenize(_:options:)` | 형태소 분석 (최상위 결과만) |
| `splitIntoSentences(_:options:)` | 문장 분리 |
| `createJoiner(useLMSearch:)` | Joiner 생성 |
| `createMorphemeSet()` | MorphemeSet 생성 |

### Token

| 프로퍼티 | 타입 | 설명 |
|----------|------|------|
| `form` | `String` | 형태소 문자열 |
| `tag` | `POSTag` | 품사 태그 |
| `position` | `Int` | 원문에서의 위치 (UTF-16) |
| `length` | `Int` | 길이 (UTF-16) |
| `score` | `Float` | 언어 모델 점수 |
| `wordPosition` | `Int` | 어절 번호 |
| `sentencePosition` | `Int` | 문장 번호 |
| `typoCost` | `Float` | 오타 교정 비용 (0이면 교정 안 됨) |

### Joiner

| 메서드 | 설명 |
|--------|------|
| `add(form:tag:autoDetectIrregular:)` | 형태소 추가 |
| `join()` | 결합된 텍스트 반환 |

## 에러 처리

```swift
do {
    let builder = try KiwiBuilder(modelPath: "/invalid/path")
} catch KiwiError.modelNotFound(let path) {
    print("모델을 찾을 수 없습니다: \(path)")
} catch KiwiError.operationFailed(let message) {
    print("작업 실패: \(message)")
} catch KiwiError.invalidHandle {
    print("잘못된 핸들")
} catch {
    print("알 수 없는 오류: \(error)")
}
```
