# Kiwi
## 지능형 한국어 형태소 분석기(Korean Intelligent Word Identifier)

### 개요
Kiwi는 C++기반의 한국어 형태소 분석기 라이브러리입니다. 
입력한 단어나 문장을 세종 품사 태그에 따라 분석하고 그 태그를 붙여줍니다. 
현재는 개발 초기 단계로 분석기는 약 80%의 정확도로 해당 텍스트를 분석해 낼 수 있습니다.

### 업데이트 내역

* v0.1: 첫 릴리즈. 약 80% 정확도
* v0.2: 정확도 85%까지 향상.

  상호정보량 맵을 이용하여 분석 모호성 감소

  서술격 조사 생략 추적 가능해짐

* v0.3 : 알고리즘 및 메모리 관리 최적화

  실행속도 약 86% 향상

### 성능

* 비문학(신문기사): 0.825
* 문학작품: 0.851

결과 예시

    프랑스의 세계적인 의상 디자이너 엠마누엘 웅가로가 실내 장식용 직물 디자이너로 나섰다.
    (정답) 프랑스/NNP	의/JKG	세계/NNG	적/XSN	이/VCP	ㄴ/ETM	의상/NNG	디자이너/NNG	엠마누엘/NNP	웅가로/NNP	가/JKS	실내/NNG	장식/NNG	용/XSN	직물/NNG	디자이너/NNG	로/JKB	나서/VV	었/EP	다/EF	./SF
    (Kiwi) 프랑스/NNP	의/JKG	세계/NNG	적/XSN	이/VCP	ㄴ/ETM	의상/NNG	디자이너/NNG	엠마누엘/NNP	웅/NNP	가로/NNG	가/JKS	실내/NNG	장식/NNG	용/XSN	직물/NNG	디자이너/NNG	로/JKB	나서/VV	었/EP	다/EF	./SF
    
    둥글둥글한 돌은 아무리 굴러도 흔적이 남지 않습니다.
    (정답) 둥글둥글/MAG	하/XSA	ㄴ/ETM	돌/NNG	은/JX	아무리/MAG	구르/VV	어도/EC	흔적/NNG	이/JKS	남/VV	지/EC	않/VX	습니다/EF	./SF
    (Kiwi) 둥글둥글/MAG	하/XSV	ㄴ/ETM	돌/NNG	은/JX	아무리/MAG	구르/VV	어도/EC	흔적/NNG	이/JKS	남/VV	지/EC	않/VX	습니다/EF	./SF

	하늘을 훨훨 나는 새처럼
	(정답) 하늘/NNG	을/JKO	훨훨/MAG	날/VV	는/ETM	새/NNG	처럼/JKB
	(Kiwi) 하늘/NNG	을/JKO	훨훨/MAG	날/VV	는/ETM	새/NNG	처럼/JKB

	아버지가방에들어가신다
	(정답) 아버지/NNG	가/JKS	방/NNG	에/JKB	들어가/VV	시/EP	ㄴ다/EF
	(Kiwi) 아버지/NNG	가방/NNG	에/JKB	들어가/VV	시/EP	ㄴ다/EF

### 데모

https://lab.bab2min.pe.kr/kiwi 에서 데모를 실행해 볼 수 있습니다.


### 라이센스
Kiwi는 LGPL v3 라이센스로 배포됩니다. 

이메일: bab2min@gmail.com

블로그: http://bab2min.tistory.com/560
