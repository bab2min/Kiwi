import codecs
import re
import os

def getFileContent(filename):
    f = codecs.open(filename, 'r', 'utf-16')
    cont = f.read()
    f.close()
    return cont


def extract1():
    latxml = []

    for dirname, dirnames, filenames in os.walk(r"e:/세종계획말뭉치/현대문어_형태분석_말뭉치"):
        for filename in filenames:
            path = os.path.join(dirname, filename)
            if path.endswith(".txt"): latxml.append(path)


    pat = re.compile('<p>(.+?)</p>', re.S)
    
    out = codecs.open('ML_lit.txt', 'w', 'utf-8')
    nnn = 0
    for file in latxml[:]:
        nnn += 1
        print(file)
        filename = re.search(r"\\([^.]+)", file)
        try:
            m = pat.findall(getFileContent(file))
            for l in m:
                for n in l.split('\n'):
                    if not n.strip(): continue
                    sp = n.split('\t')
                    if len(sp) < 3: continue
                    #out.write('\t'.join(n.split('\t')[1:]) + '\n')
                    mid = 1
                    if not sp[1].strip(): continue
                    out.write(sp[1] + "\t")
                    sp[2] = sp[2].replace('+/SW', '&p/SW')
                    ms = map(lambda a: a.strip(), sp[2].split(' + '))
                    for mp in ms:
                        smp = mp.replace('&p', '+').split('/')
                        if len(smp) == 3: smp = ['/', smp[2]]
                        out.write("%s\t%s\t" % (smp[0], smp[1]))
                    out.write("\n")
                out.write('\n')
        except Exception as e:
            f = codecs.open('error.xml', 'w', 'utf-8')
            f.write(getFileContent(file))
            f.close()
            out.close()
            raise e
    out.close()


def extract2():
    latxml = []

    for dirname, dirnames, filenames in os.walk(r"e:/세종계획말뭉치/현대구어_형태분석_말뭉치"):
        for filename in filenames:
            path = os.path.join(dirname, filename)
            if path.endswith(".txt"): latxml.append(path)


    pat = re.compile('<s [^>]*>(.+?)</s>', re.S)
    out = codecs.open('ML_spo.txt', 'w', 'utf-8')
    for file in latxml:
        print(file)
        filename = re.search(r"\\([^.]+)", file)
        try:
            m = pat.findall(getFileContent(file))
            sid = 1
            for l in m:
                wid = 1
                for n in l.split('\n'):
                    if not n.strip(): continue
                    sp = n.split('\t')
                    if len(sp) < 3: continue
                    #out.write('\t'.join(sp[1:]) + '\n')
                    mid = 1
                    sp[1] = re.sub("<phon>[^>]+</phon>", '', sp[1])
                    sp[1] = re.sub("<[^>]+>", '', sp[1]).strip()
                    if not sp[1]: continue
                    out.write(sp[1] + '\t')
                    ms = sp[2].split('+')
                    for mp in ms:
                        smp = mp.replace('\r', '').split('/')
                        out.write("%s\t%s\t" % (smp[0], smp[1]))
                    out.write('\n')
                out.write('\n')
        except Exception as e:
            f = codecs.open('error.xml', 'w', 'utf-8')
            f.write(getFileContent(file))
            f.close()
            raise e
    out.close()

extract1()
extract2()
