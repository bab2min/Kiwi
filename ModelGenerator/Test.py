from konlpy.tag import Kkma
from konlpy.tag import Komoran
#from kiwiPy import Kiwi

import time

class Test:
    def __init__(self, testFile, parser):
        self.totalCount = 0
        self.totalScore = 0
        self.wrongList = []
        start = time.time()
        with open(testFile, encoding='utf-8') as o:
            for line in o:
                fd = line.strip().split('\t')
                q = fd[0]
                a = fd[1:]
                r = parser(q)
                if a != r:
                    self.wrongList.append([q, r, a])
                else:
                    self.totalScore += 1
                self.totalCount += 1
        self.totalTime = time.time() - start

    def refineScore(self):
        for wl in self.wrongList:
            intersect = len([x for x in wl[1] if x in wl[2]])
            wl.append(intersect / (len(wl[1]) + len(wl[2]) - intersect))
            self.totalScore += wl[-1]

    def getScore(self):
        return self.totalScore / self.totalCount

    def writeToFile(self, output):
        with open(output, 'w', encoding='utf-8') as o:
            o.write('%g\n' % self.getScore())
            o.write('Total (%d) Time: %g ms\n' % (self.totalCount, self.totalTime * 1000))
            o.write('Time per Unit: %g ms\n\n' % (self.totalTime / self.totalCount * 1000))
            for wl in self.wrongList:
                o.write('%s\t%g\n' % (wl[0], wl[3]))
                o.write('\t'.join(wl[2]) + '\n')
                o.write('\t'.join(wl[1]) + '\n')
                o.write('\n')

def doTest(testFiles, posFunc, outPrefix):
    for f in testFiles:
        print('\n' + f)
        t = Test("../TestSets/" + f, posFunc)
        print("Total: %gs" % t.totalTime)
        t.refineScore()
        print("Score: %g" % t.getScore())
        t.writeToFile(outPrefix + 'wrong' + f)

testFiles = ['01s.txt', '02s.txt', '03s.txt', '17s.txt', '18s.txt']

start = time.time()
p = Komoran()
p.pos("")
print("Loading: %gs" % (time.time() - start))
doTest(testFiles, lambda s:['/'.join(i) for i in p.pos(s)], 'kmr_')
p = None
print()
#start = time.time()
#p = Kiwi()
#print("Loading: %gs" % (time.time() - start))
#doTest(testFiles, lambda s:['/'.join(i) for i in p.analyze(s)], 'kiwi_')
