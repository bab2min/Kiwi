from konlpy.tag import Kkma
from konlpy.tag import Komoran

import time

class Test:
    def __init__(self, testFile, parser):
        self.totalCount = 0
        self.wrongList = []

        with open(testFile, encoding='utf-8') as o:
            for line in o:
                fd = line.strip().split('\t')
                q = fd[0]
                a = fd[1:]
                r = parser(q)
                if a != r: self.wrongList.append((q, r, a))
                self.totalCount += 1

    def getScore(self):
        return 1 - len(self.wrongList) / self.totalCount


def posTest(s):
    return ['/'.join(i) for i in p.pos(s)]

start = time.time()
p = Kkma()
p.pos("")
print("Loading: " + str(time.time() - start))

start = time.time()
t = Test("../TestSets/01.txt", posTest)
print("Score: " + str(t.getScore()))
print("Total: " + str(time.time() - start))