# KiwiJava, 한국어 형태소 분석기 Kiwi의 Java 바인딩
Kiwi v0.16.0에서부터는 자체적으로 Kiwi의 Java 바인딩인 KiwiJava를 제공합니다. KiwiJava는 [최신 Release](https://github.com/bab2min/Kiwi/releases/)에서 `kiwi-java-*.jar`라는 이름으로 제공됩니다.
jar 파일 내부에 OS에 종속적인 바이너리가 포함되어 있으므로 JVM이 구동되는 가상머신의 환경(win(Windows), lnx(Linux), mac(macOS))에 맞춰서 적합한 jar파일을 받아야 합니다.

* KiwiJava는 Java 1.8 이상과 호환됩니다.

## 시작하기
KiwiJava의 jar 파일은 자체적으로 실행 가능한 main함수를 가지고 있습니다. 따라서 jar 파일과 모델 파일을 받고 다음과 같이 jar 파일을 구동하여 KiwiJava가 정상적으로 작동하는지 확인할 수 있습니다.

```bash
# Linux x86-64 환경을 가정

# 모델 파일 다운로드
$ wget https://github.com/bab2min/Kiwi/releases/download/v0.19.0/kiwi_model_v0.19.0_base.tgz
$ tar -zxvf kiwi_model_v0.19.0_base.tgz # 압축 해제. 모델을 포함한 models/base라는 폴더가 생성됨

# KiwiJava 다운로드
$ wget https://github.com/bab2min/Kiwi/releases/download/v0.19.0/kiwi-java-v0.19.0-lnx-x86-64.jar

# 모델 파일은 Patch가 다르더라도 Major, Minor 버전이 동일하면 호환됩니다.
# 즉, kiwi_model_v0.19.0은 kiwi-java-v0.19.* 전부에서 사용가능합니다.

# jar 구동. 구동 인자로 모델 경로를 입력해주어야 함
$ java -jar kiwi-java-v0.19.0-lnx-x86-64.jar models/base/
Kiwi 0.19.0 is loaded!
>> 안녕하세요?
[Token(form=안녕, tag=NNG, position=0, length=2),
 Token(form=하, tag=XSA, position=2, length=1),
 Token(form=세요, tag=EF, position=3, length=2),
 Token(form=?, tag=SF, position=5, length=1)]
```

## Java API
```java
import java.util.Arrays;

import kr.pe.bab2min.Kiwi;
import kr.pe.bab2min.KiwiBuilder;

public class KiwiExample {
  public static void main(String[] args) throws Exception {
    // Kiwi 인스턴스 생성 방법 (1)
    Kiwi kiwi = Kiwi.init("path_to_kiwi_model");

    /*
    // Kiwi 인스턴스 생성 방법 (2)
    try(KiwiBuilder builder = new KiwiBuilder("path_to_kiwi_model")) {
      // 기본 옵션으로 Kiwi 인스턴스 생성
      kiwi = builder.build();
        
      // 오타 교정 기능을 사용하여 Kiwi 인스턴스 생성
      kiwi = builder.build(KiwiBuilder.basicTypoSet, 2.0f);
    }
    */

    // 형태소 분석
    // Kiwi.Match에 대한 자세한 설명은
    // https://github.com/bab2min/Kiwi/blob/c849ee06f788ca07b6c924f3497bcce89e9dfa9a/include/kiwi/PatternMatcher.h
    // 를 참고하세요.
    Kiwi.Token[] tokens = kiwi.tokenize("분석할 텍스트", Kiwi.Match.allWithNormalizing);
    System.out.println(Arrays.deepToString(tokens));
    /* Output:
       [Token(form=분석, tag=NNG, position=0, length=2), 
        Token(form=하, tag=XSV, position=2, length=1), 
        Token(form=ᆯ, tag=ETM, position=2, length=1), 
        Token(form=텍스트, tag=NNG, position=4, length=3)]*/

    // 문장 분할
    Kiwi.Sentence[] sents = kiwi.splitIntoSents("텍스트를 문장별로 분할합니다. 잘 분할됩니까?", Kiwi.Match.allWithNormalizing);
    System.out.println(Arrays.deepToString(sents));
    /* Output:
       [Sentence(text=텍스트를 문장별로 분할합니다., start=0, end=16, subSents=[]), 
        Sentence(text=잘 분할됩니까?, start=17, end=25, subSents=[])]*/

    // 형태소 결합
    Kiwi.JoinableToken[] joinableTokens = new Kiwi.JoinableToken[]{
      new Kiwi.JoinableToken("키위", Kiwi.POSTag.nnp),
      new Kiwi.JoinableToken("을", Kiwi.POSTag.jko),
      new Kiwi.JoinableToken("사용", Kiwi.POSTag.nng),
      new Kiwi.JoinableToken("하", Kiwi.POSTag.xsv),
      new Kiwi.JoinableToken("었", Kiwi.POSTag.ep),
      new Kiwi.JoinableToken("다", Kiwi.POSTag.ef),
    };
    String joined = kiwi.join(joinableTokens);
    System.out.println(joined);
    /* Output:
       키위를 사용했다*/
  }
}

```

자세한 예시는 [src/kr/pe/bab2min/](src/kr/pe/bab2min/) 내의 Kiwi.java, KiwiBuilder.java 및 [test/](test/) 내의 KiwiTest.java 파일을 참조해주세요.

## Android에서 사용하기

### 1. AAR 파일 사용

GitHub Releases에서 `kiwi-android-VERSION.aar` 파일을 다운로드하고 프로젝트에 추가:

1. 다운로드한 AAR 파일을 `app/libs/` 폴더에 복사
2. `app/build.gradle`에 다음 추가:

```gradle
android {
    ...
}

dependencies {
    implementation files('libs/kiwi-android-VERSION.aar')
    ...
}
```

### 2. 소스에서 빌드

이 디렉토리에서 직접 빌드할 수도 있습니다:

```bash
cd bindings/java
./gradlew assembleRelease
```

빌드된 AAR 파일은 `build/outputs/aar/` 디렉토리에 생성됩니다.

## 사용 방법
```kotlin
package example

import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity

import kr.pe.bab2min.Kiwi
import kr.pe.bab2min.KiwiBuilder

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        try {
            // 모델 파일을 assets 내의 kiwi_model/ 폴더에 넣었다고 가정
            val streamProvider = KiwiBuilder.StreamProvider { filename ->
                try {
                    // assets 내의 kiwi_model/ 폴더 내의 파일을 연다.
                    assets.open("kiwi_model/$filename")
                } catch (e: Exception) {
                    null // 파일이 없으면 null 반환
                }
            }

            val kiwi = KiwiBuilder(streamProvider).build()
            val res = kiwi.tokenize("안드로이드에서도 Kiwi를!", Kiwi.Match.allWithNormalizing)
            for (token in res) {
                Log.d("Kiwi", "${token.form} / ${Kiwi.POSTag.toString(token.tag)}")
            }
            /* 다음과 같이 출력됨
            안드로이드 / NNG
            에서 / JKB
            도 / JX
            Kiwi / SL
            를 / JKO
            ! / SF
            */
        } catch (e: Exception) {
            Log.e("Kiwi", "Error", e)
        }
    }
}
```

## 요구사항

- Android API Level 21+ (Android 5.0)
- ARM64-v8a 아키텍처
- Java 8+

## 모델 파일

모델 파일은 별도로 다운로드해야 합니다:
1. GitHub Releases에서 `kiwi_model_VERSION_base.tgz` 다운로드
2. 압축 해제 후 Android 앱의 assets 폴더에 복사
3. 런타임에 내부 저장소로 복사한 뒤 파일 경로로 접근하거나 KiwiBuilder.StreamProvider를 사용하여 assets에서 직접 읽도록 설정
