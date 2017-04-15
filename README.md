# Kiwi
## 지능형 한국어 형태소 분석기(Korean Intelligent Word Identifier)

### 개요
Kiwi는 C++기반의 한국어 형태소 분석기 라이브러리입니다. 
입력한 단어나 문장을 세종 품사 태그에 따라 분석하고 그 태그를 붙여줍니다. 
현재는 개발 초기 단계로 분석기는 약 80%의 정확도로 해당 텍스트를 분석해 낼 수 있습니다.

### 성능

* 비문학(신문기사): 0.781
* 문학작품: 0.811

오답사례

    프랑스의 세계적인 의상 디자이너 엠마누엘 웅가로가 실내 장식용 직물 디자이너로 나섰다.
    (정답) 프랑스/NNP	의/JKG	세계/NNG	적/XSN	이/VCP	ㄴ/ETM	의상/NNG	디자이너/NNG	엠마누엘/NNP	웅가로/NNP	가/JKS	실내/NNG	장식/NNG	용/XSN	직물/NNG	디자이너/NNG	로/JKB	나서/VV	었/EP	다/EF	./SF
    (Kiwi) 프랑스/NNP	의/JKG	세계/NNG	적/XSN	이/VCP	ㄴ/ETM	의상/NNG	디자이너/NNG	엠마누에/UN	ㄹ/ETM	웅/NNP	가로/NNG	가/JKS	실내/NNG	장식/NNG	용/XSN	직물/NNG	디자이너/NNG	로/JKB	나서/VV	었/EP	다/EF	./SF
    
    둥글둥글한 돌은 아무리 굴러도 흔적이 남지 않습니다.
    (정답) 둥글둥글/MAG	하/XSA	ㄴ/ETM	돌/NNG	은/JX	아무리/MAG	구르/VV	어도/EC	흔적/NNG	이/JKS	남/VV	지/EC	않/VX	습니다/EF	./SF
    (Kiwi) 둥글둥글/MAG	하/XSV	ㄴ/ETM	돌/VV	은/ETM	아무리/MAG	구르/VV	어도/EC	흔적/NNG	이/JKS	남/VV	지/EC	않/VX	습니다/EF	./SF


### 라이센스
Kiwi는 LGPL v3 라이센스로 배포됩니다. 

이메일: bab2min@gmail.com

블로그: http://bab2min.tistory.com/560
