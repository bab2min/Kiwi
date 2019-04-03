# Kiwi
## 지능형 한국어 형태소 분석기(Korean Intelligent Word Identifier)

### 개요
Kiwi는 C++기반의 한국어 형태소 분석기 라이브러리입니다. 
입력한 단어나 문장을 세종 품사 태그에 따라 분석하고 그 태그를 붙여줍니다. 
분석기는 문어 텍스트의 경우 평균 94%의 정확도로 해당 텍스트를 분석해 낼 수 있습니다.

Kiwi는 C++기반으로 최적화되었으며 멀티스레딩을 지원하기에,
대량의 텍스트를 분석해야하는 경우 멀티코어를 활용하여 좀 더 빠르게 분석이 가능합니다.

### 사용

#### C++
오픈소스로 제공되는 C++ 라이브러리를 컴파일하여 사용하실 수 있습니다. 
Visual Studio 2015, 2017 환경에서 개발되었고 gcc 5.0 이상에서 컴파일되는 것을 확인했습니다.
컴파일에 필요한 별도의 의존 관계는 없으나, 컴파일러가 C++14 이상을 지원해야 합니다.

#### C API
https://github.com/bab2min/Kiwi/wiki 를 참고하시길 바랍니다.

#### Windows용 DLL
DLL로 컴파일된 바이너리를 이용하여 다른 프로그램에 Kiwi를 적용할 수도 있습니다. 
최신 버전의 DLL은 https://github.com/bab2min/Kiwi/releases/tag/GUI_v0.61 에서 구할 수 있습니다.

#### C# API
https://github.com/bab2min/Kiwi/blob/master/KiwiGui/KiwiCS.cs

#### Python3 API
또한 Python3용 API인 Kiwipiepy가 제공됩니다. 이에 대해서는 https://github.com/bab2min/Kiwi/tree/master/KiwiPy 를 참조하시길 바랍니다.

#### 응용 프로그램
Kiwi는 C# 기반의 GUI 형태로도 제공됩니다.
형태소 분석기는 사용해야하지만 별도의 프로그래밍 지식이 없는 경우 이 프로그램을 사용하시면 됩니다.
다음 프로그램은 Windows에서만 구동 가능합니다.
https://github.com/bab2min/Kiwi/releases/tag/GUI_v0.6


### 업데이트 내역

* v0.1: 첫 릴리즈. 약 80% 정확도
* v0.2: 정확도 85%까지 향상.

  상호정보량 맵을 이용하여 분석 모호성 감소

  서술격 조사 생략 추적 가능해짐
  
  (분석 속도: 0.08MB/s)

* v0.3 : 알고리즘 및 메모리 관리 최적화

  실행속도 약 86% 향상 (분석 속도: 0.14MB/s)
  
* v0.4 : 알고리즘 개선

  실행속도 약 101% 향상 (분석 속도: 0.28MB/s)

* v0.5 : 언어 모형 개선(Kneser-Ney 3-gram LM)

  전반적인 정확도 상승 (최소 89%에서 94%까지)
  
  코퍼스에서 미등록 단어 추출 기능 추가
  
  멀티스레딩 지원

* v0.6 : 검색 알고리즘 최적화로 인한 속도 향상 (분석 속도: 0.33MB/s)

  전반적인 정확도 상승 (92%~96%까지)


### 다른 형태소 분석기와의 비교
다음의 성능 평가는 konlpy-0.5.1에 포함된 Hannanum, Kkma, Komoran, Okt를 Kiwi와 비교한 것입니다.

평가는 Intel i5-6600 @3.3GHz, RAM 16GB, Windows 10(64bit) 환경에서 진행되었습니다.

![형태소 분석기 실행 속도 비교](/KiwiChart.PNG)

| | Loading | 1 | 10 | 100 | 1000 | 10000 | 100000 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
**Hannanum** | 0.621 | 0.003 | 0.008 | 0.030 | 0.101 | 0.517 | 6.463
**Kkma** | 3.995 | 0.004 | 0.075 | 0.466 | 0.561 | 2.974 | 28.625
**Komoran** | 1.369 | 0.005 | 0.003 | 0.009 | 0.060 | 0.874 | 39.815
**Okt** | 1.522 | 0.006 | 0.027 | 0.057 | 0.150 | 0.634 | 3.054
**Kiwi** | **2.073** | **0.004** | **0.001** | **0.005** | **0.042** | **0.301** | **2.967**

Kiwi의 초기 로딩 시간은 2초 정도로 느린 편에 속하지만, 
로딩 이후의 처리 속도는 기존의 분석기들과 비교할 때 매우 빠른 편임을 확인할 수 있습니다.

위의 성능 평가는
https://github.com/bab2min/Kiwi/blob/master/evaluate.py 를 통해 직접 실시해볼 수 있습니다.

### 성능

* 비문학(신문기사): 0.928
* 문학작품: 0.960

결과 예시

    프랑스의 세계적인 의상 디자이너 엠마누엘 웅가로가 실내 장식용 직물 디자이너로 나섰다.
    (정답) 프랑스/NNP	의/JKG	세계/NNG	적/XSN	이/VCP	ㄴ/ETM	의상/NNG	디자이너/NNG	엠마누엘/NNP	웅가로/NNP	가/JKS	실내/NNG	장식/NNG	용/XSN	직물/NNG	디자이너/NNG	로/JKB	나서/VV	었/EP	다/EF	./SF
    (Kiwi) 프랑스/NNP	의/JKG	세계/NNG	적/XSN	이/VCP	ᆫ/ETM	의상/NNG	디자이너/NNG	엠마누/NNP	에/JKB	ᆯ/JKO	웅가로/NNP	가/JKS	실내/NNG	장식/NNG	용/XSN	직물/NNG	디자이너/NNG	로/JKB	나서/VV	었/EP	다/EF	./SF
    
    둥글둥글한 돌은 아무리 굴러도 흔적이 남지 않습니다.
    (정답) 둥글둥글/MAG	하/XSA	ㄴ/ETM	돌/NNG	은/JX	아무리/MAG	구르/VV	어도/EC	흔적/NNG	이/JKS	남/VV	지/EC	않/VX	습니다/EF	./SF
    (Kiwi) 둥글둥글/MAG	하/XSA	ᆫ/ETM	돌/NNG	은/JX	아무리/MAG	구르/VV	어도/EC	흔적/NNG	이/JKS	남/VV	지/EC	않/VX	습니다/EF	./SF

	하늘을 훨훨 나는 새처럼
	(정답) 하늘/NNG	을/JKO	훨훨/MAG	날/VV	는/ETM	새/NNG	처럼/JKB
	(Kiwi) 하늘/NNG	을/JKO	훨훨/MAG	날/VV	는/ETM	새/NNG	처럼/JKB

	아버지가방에들어가신다
	(정답) 아버지/NNG	가/JKS	방/NNG	에/JKB	들어가/VV	시/EP	ㄴ다/EF
	(Kiwi) 아버지/NNG	가/JKS	방/NNG	에/JKB	들어가/VV	시/EP	ᆫ다/EC

### 데모

https://lab.bab2min.pe.kr/kiwi 에서 데모를 실행해 볼 수 있습니다.


### 라이센스
Kiwi는 LGPL v3 라이센스로 배포됩니다. 

이메일: bab2min@gmail.com

블로그: http://bab2min.tistory.com/560
