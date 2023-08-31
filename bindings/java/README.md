# KiwiJava, 한국어 형태소 분석기 Kiwi의 Java 바인딩
Kiwi v0.16.0에서부터는 자체적으로 Kiwi의 Java 바인딩인 KiwiJava를 제공합니다. KiwiJava는 [최신 Release](https://github.com/bab2min/Kiwi/releases/)에서 `kiwi-java-*.jar`라는 이름으로 제공됩니다.
jar 파일 내부에 OS 종속적인 바이너리가 포함되어 있는 관계로 JVM이 구동되는 가상머신의 환경(win(Windows), lnx(Linux), mac(macOS))에 맞춰서 적합한 jar파일을 받아야 합니다.

* KiwiJava는 Java 1.8 이상과 호환됩니다.

## 시작하기
KiwiJava의 jar 파일은 자체적으로 실행 가능한 main함수를 가지고 있습니다. 따라서 jar 파일과 모델 파일을 받고 다음과 같이 jar 파일을 구동하여 KiwiJava가 정상적으로 작동하는지 확인할 수 있습니다.

```bash
# Linux x86-64 환경을 가정

# 모델 파일 다운로드
$ wget https://github.com/bab2min/Kiwi/releases/download/v0.16.0/kiwi_model_v0.16.0_base.tgz
$ tar -zxvf kiwi_model_v0.16.0_base.tgz # 압축 해제. 모델을 포함한 ModelGenerator라는 폴더가 생성됨

# KiwiJava 다운로드
$ wget https://github.com/bab2min/Kiwi/releases/download/v0.16.0/kiwi-java-v0.16.0-lnx-x86-64.jar

# jar 구동. 구동 인자로 모델 경로를 입력해주어야 함
$ java -jar kiwi-java-v0.16.0-lnx-x86-64.jar ModelGenerator/
Kiwi 0.16.0 is loaded!
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
    Kiwi kiwi = Kiwi.init("some model path");
    Kiwi.Token[] tokens = kiwi.tokenize("분석할 텍스트", Kiwi.Match.allWithNormalizing);
    System.out.println(Arrays.deepToString(tokens));
  }
}
```

자세한 예시는 [kr/pe/bab2min/](kr/pe/bab2min/) 내의 Kiwi.java, KiwiBuilder.java 및 KiwiTest.java 파일을 참조해주세요.
