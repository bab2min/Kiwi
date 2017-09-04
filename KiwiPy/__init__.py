import kiwiPyRaw

class Kiwi:
    def __init__(self, modelPath = '', cacheSize = -1, userDicts = []):
        self.inst = kiwiPyRaw.initKiwi(modelPath, cacheSize)
        if type(userDicts) == str: userDicts = [userDicts]
        for ud in userDicts:
            kiwiPyRaw.loadUserDictKiwi(self.inst, ud)
        kiwiPyRaw.prepareKiwi(self.inst)

    def analyzeN(self, text, topN):
        return kiwiPyRaw.analyzeKiwi(self.inst, text, topN)

    def analyze(self, text):
        return kiwiPyRaw.analyzeKiwi(self.inst, text, 1)[0][0]

    def __del__(self):
        kiwiPyRaw.closeKiwi(self.inst)
