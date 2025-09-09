# Kiwi Android Library

한국어 형태소 분석기 Kiwi의 Android 라이브러리입니다.

## 설치 방법

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
cd bindings/android
./gradlew assembleRelease
```

빌드된 AAR 파일은 `build/outputs/aar/` 디렉토리에 생성됩니다.

## 사용 방법

```java
import kr.pe.bab2min.Kiwi;
import kr.pe.bab2min.KiwiBuilder;

public class MainActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        try {
            // 모델 파일이 assets 폴더에 있다고 가정
            String modelPath = copyModelFromAssets();
            
            // Kiwi 인스턴스 생성
            Kiwi kiwi = Kiwi.init(modelPath);
            
            // 형태소 분석
            Kiwi.Token[] tokens = kiwi.tokenize("안녕하세요, 반갑습니다!", Kiwi.Match.allWithNormalizing);
            
            for (Kiwi.Token token : tokens) {
                Log.d("Kiwi", "Form: " + token.form + ", Tag: " + token.tag);
            }
            
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
    
    private String copyModelFromAssets() {
        // assets에서 모델 파일을 내부 저장소로 복사하는 코드
        // 실제 구현은 사용자의 앱 구조에 따라 달라집니다
        return getFilesDir().getAbsolutePath() + "/kiwi_model";
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
3. 런타임에 내부 저장소로 복사하여 사용

## API 문서

자세한 API 사용법은 [Java 바인딩 문서](../java/README.md)를 참조하세요. Android 버전은 동일한 API를 제공합니다.