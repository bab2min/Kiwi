# Python3용 Kiwi

Kiwi 0.5버전부터는 Python3용 API를 제공합니다. 이 프로젝트를 빌드하여 Python에 모듈을 import해서 사용하셔도 좋고, 
혹은 더 간편하게 pip를 이용하여 이미 빌드된 kiwipiepy 모듈을 설치하셔도 좋습니다.

    pip install kiwipiepy

또는

    pip3 install kiwipiepy

단, 현재 kiwipiepy 패키지는 Vista 버전 이상의 Windows OS 및 Linux를 지원합니다.
MacOS 경우 대해서는 아직 테스트를 해보지 못했습니다.

## 시작하기

kiwipiepy 패키지 설치가 성공적으로 완료되었다면, 다음과 같이 패키지를 import후 Kiwi 객체를 생성했을때 오류가 발생하지 않습니다.

    from kiwipiepy import Kiwi
    kiwi = Kiwi()

Kiwi 생성자는 numThread 파라미터 하나를 받습니다.

    numThread = 4
    kiwi = Kiwi(numThread)

numThread가 2 이상이면 단어 추출 및 형태소 분석에 멀티 코어를 활용하여 조금 더 빠른 속도로 분석을 진행할 수 있습니다. 
numThread가 1인 경우 단일 코어만 활용합니다. numThread가 0이면 현재 환경에서 사용가능한 모든 코어를 활용합니다. 
생략 시 기본값은 0입니다.

kiwi 객체는 크게 다음 세 종류의 작업을 수행할 수 있습니다.
* 코퍼스로부터 미등록 단어 추출
* 사용자 사전 추가
* 형태소 분석

## 코퍼스로부터 미동록 단어 추출

Kiwi 0.5부터 새로 추가된 기능입니다. 자주 등장하는 문자열의 패턴을 파악하여 단어로 추정되는 문자열을 추출해줍니다. 
이 기능의 기초적인 아이디어는 https://github.com/lovit/soynlp 의 Word Extraction 기법을 바탕으로 하고 있으며, 
이에 문자열 기반의 명사 확률을 조합하여 명사일 것으로 예측되는 단어만 추출합니다.

Kiwi가 제공하는 미등록 단어 추출 관련 메소드는 다음 세 가지입니다.

    kiwi.extractWords(reader, minCount, maxWordLength, minScore)
    kiwi.extractFilterWords(reader, minCount, maxWordLength, minScore, posScoreThreshold)
    kiwi.extractAddWords(reader, minCount, maxWordLength, minScore, posScoreThreshold)

**`extractWords(reader, minCount = 10, maxWordLength = 10, minScore = 0.25)`**

reader가 읽어들인 텍스트로부터 단어 후보를 추출합니다. 
reader는 다음과 같은 호출가능한(Callable) 객체여야합니다.
minCount는 추출할 단어가 입력 텍스트 내에서 최소 몇 번 이상 등장하는 지를 결정합니다. 입력 텍스트가 클 수록 그 값을 높여주시는게 좋습니다.
maxWordLength는 추출할 단어의 최대 길이입니다. 이 값을 너무 크게 설정할 경우 단어를 스캔하는 시간이 길어지므로 적절하게 조절해주시는 게 좋습니다.
minScore는 추출할 단어의 최소 단어 점수입니다. 이 값을 낮출수록 단어가 아닌 형태가 추출될 가능성이 높아지고, 
반대로 이 값을 높일 수록 추출되는 단어의 개수가 줄어들므로 적절한 수치로 설정하실 필요가 있습니다. 기본값은 0.25입니다.

    class ReaderExam:
        def __init__(self, filePath):
            self.file = open(filePath)
        
        def read(self, id):
            if id == 0: self.file.seek(0)
            return self.file.readline()

    reader = ReaderExam('test.txt')
    kiwi.extractWords(reader.read, 10, 10, 0.25)

reader는 첫번째 인자로 id를 받습니다. id는 현재 읽어야할 행의 번호를 알려주며, id == 0인 경우 파일을 처음부터 다시 읽어야합니다. 
extract계열의 함수는 단어 후보를 추출하는 과정에서 입력 텍스트 파일을 여러 번 다시 읽으므로, 
id == 0인 경우를 적절하게 처리해주어야 올바른 결과를 얻으실 수 있습니다.

**`extractFilterWords(reader, minCount = 10, maxWordLength = 10, minScore = 0.25, posScoreThreshold = -3)`**

extractWords와 동일하게 단어 후보를 추출하고, 그 중에서 이미 형태소 분석 사전에 등록된 경우 및 명사가 아닐 것으로 예측되는 단어 후보를 제거하여
실제 명사로 예측되는 단어 목록만 추출해줍니다. 
posScoreThreshold는 추출할 단어의 최소 명사 점수입니다. 이 값을 낮출수록 명사가 아닌 단어들이 추출될 가능성이 높으며, 
반대로 높일수록 추출되는 명사의 개수가 줄어듭니다. 기본값은 -3입니다.
나머지 인자는 extractWords와 동일합니다. 

**`extractAddWords(reader, minCount = 10, maxWordLength = 10, minScore = 0.25, posScoreThreshold = -3)`**

extractFilterWords와 동일하게 명사인 단어만 추출해줍니다. 
다만 이 메소드는 추출된 명사 후보를 자동으로 사용자 사전에 등록하여 형태소 분석에 사용할 수 있게 해줍니다. 
만약 이 메소드를 사용하지 않으신다면 addUserWord 메소드를 사용하여 추출된 미등록 단어를 직접 사용자 사전에 등록하셔야 합니다.

## 사용자 사전 추가

기존의 사전에 등록되지 않은 단어를 제대로 분석하기 위해서는 사용자 사전에 해당 단어를 등록해주어야 합니다. 
이는 extractAddWords를 통해서 자동으로 이뤄질 수도 있고, 수작업으로 직접 추가될 수도 있습니다. 
다음 메소드들은 사용자 사전을 관리하는데 사용되는 메소드들입니다.

    kiwi.addUserWord(word, pos, score)
    kiwi.loadUserDictionary(userDictPath)

**`addUserWord(word, pos = 'NNP', score = 20.0)`**

사용자 사전에 word를 등록합니다. 현재는 띄어쓰기(공백문자)가 포함되지 않는 문자열만 단어로 등록할 수 있습니다.
pos는 등록할 단어의 품사입니다. 세종 품사태그를 따르며, 기본값은 NNP(고유명사)입니다.
score는 등록할 단어의 점수입니다. 
동일한 형태라도 여러 경우로 분석될 가능성이 있는 경우에, 이 값이 클수록 해당 단어가 더 우선권을 가지게 됩니다.


**`loadUserDictionary(userDictPath)`**

파일로부터 사용자 사전을 읽어들입니다. 사용자 사전 파일은 UTF-8로 인코딩되어 있어야하며, 다음과 같은 형태로 구성되어야 합니다.
탭 문자(\t)로 각각의 필드는 분리되어야 하며, 단어 점수는 생략 가능합니다. 

    #주석은 #으로 시작합니다.
    단어1 [탭문자] 품사태그 [탭문자] 단어점수
    단어2 [탭문자] 품사태그 [탭문자] 단어점수
    단어3 [탭문자] 품사태그 [탭문자] 단어점수

실제 예시

    # 스타크래프트 관련 명사 목록
    스타크래프트  NNP 30.0
    저글링 NNP
    울트라리스크 NNP  30.0
    
# 형태소 분석

kiwi을 생성하고, 사용자 사전에 단어를 추가하는 작업이 완료되었으면 prepare를 호출하여 분석 모델을 준비해야합니다.

**`kiwi.prepare()`**

이 메소드는 별다른 파라미터를 필요로하지 않으며, 성공하였을 경우 0, 실패하였을 경우 0이 아닌 값을 돌려줍니다.

실제 형태소를 분석하는 메소드에는 다음이 있습니다.

    kiwi.analyze(text, topN)
    kiwi.analyze(reader, receiver, topN)
    

**`analyze(text, topN = 1)`**

입력된 text를 형태소 분석하여 그 결과를 반환합니다. 총 topN개의 결과를 출력합니다. 반환값은 다음과 같이 구성됩니다.

    [(분석결과1, 점수), (분석결과2, 점수), ... ]

분석결과는 다음과 같이 튜플의 리스트 형태로 반환됩니다.

    [(단어1, 품사태그, 단어 시작 지점, 단어 길이), (단어2, 품사태그, 단어 시작 지점, 단어 길이), ...]

실제 예시는 다음과 같습니다.

    >> kiwi.analyze('테스트입니다.', 5)
    [([('테스트', 'NNG', 0, 3), ('이', 'VCP', 3, 1), ('ᆸ니다', 'EF', 4, 2), ('.', 'SF', 6, 1)], -20.393310546875), 
     ([('테스 트입니', 'NNG', 0, 5), ('다', 'EF', 5, 1), ('.', 'SF', 6, 1)], -29.687143325805664), 
     ([('테스트입니', 'NNG', 0, 5), ('이', 'VCP', 5, 1), ('다', 'EF', 5, 1), ('.', 'SF', 6, 1)], -31.221078872680664), 
     ([('테스트입니', 'NNP', 0, 5), ('이', 'VCP', 5, 1), ('다', 'EF', 5, 1), ('.', 'SF', 6, 1)], -32.20524978637695), 
     ([('테스트', 'NNG', 0, 3), ('이', 'MM', 3, 1), ('ᆸ니다', 'EF', 4, 2), ('.', 'SF', 6, 1)], -32.859375)]
     
 **`analyze(reader, receiver, topN = 1)`**
 
 analyze 메소드는 또 다른 형태로도 호출할 수 있습니다. reader와 receiver를 사용해 호출하는 경우,
 문자열 읽기/쓰기 부분과 분석 부분이 별도에 스레드에서 동작하며, 여분의 스레드가 있을 경우 분석 역시 멀티 코어를 활용하여 성능 향상을 꽤할 수 있습니다.
 대량의 텍스트를 분석하는 경우 이 형태를 사용하는 것을 추천합니다.
 
 reader와 receiver를 사용한 예시는 다음과 같습니다.
 
    from kiwipiepy import Kiwi

    class IOHandler:
        def __init__(self, input, output):
            self.input = open(input, encoding='utf-8')
            self.output = open(output, 'w', encoding='utf-8')

        def read(self, id):
            if id == 0:
                self.input.seek(0)
            return self.input.readline()

        def write(self, id, res):
            print('Analyzed %dth row' % id)
            self.output.write(' '.join(map(lambda x:x[0]+'/'+x[1], res[0][0])) + '\n')

        def __del__(self):
            self.input.close()
            self.output.close()

    kiwi = Kiwi()
    kiwi.loadUserDictionary('userDict.txt')
    kiwi.prepare()
    handle = IOHandler('test.txt', 'result.txt')
    kiwi.analyze(handle.read, handle.write)

