d = {}
n = 0
for line in open('e:\\세종계획말뭉치\\Point.txt', encoding='utf-8'):
    ch = line.strip().split('\t')
    if len(ch) < 2: continue
    if ch[0] not in d: d[ch[0]] = {}
    d[ch[0]][0] = d[ch[0]].get(0, 0) + 1
    t = tuple(ch[1:])
    d[ch[0]][t] = d[ch[0]].get(t, 0) + 1
    #n += 1
    #if n > 1000000: break

with open('ambiguity.txt', 'w', encoding='utf-8') as o, open('ambiguityWords.txt', 'w', encoding='utf-8') as o2:
    ambWords = set()
    for a in sorted(d, key=lambda x:d[x][0], reverse=True):
        if len(d[a]) <= 2: continue
        if sum(map(lambda x:x > 1, d[a].values())) <= 2: continue
        o.write('%d\t%s\n' % (d[a][0], a))
        for b in sorted(d[a], key=d[a].get, reverse=True):
            if b == 0: continue
            if d[a][b] <= 1: continue
            o.write('%d\t%s\n' % (d[a][b], '\t'.join(b)))
            for c in b:
                if '/N' in c or '/V' in c or '/M' in c: ambWords.add(c)
        o.write('\n')
    for a in sorted(ambWords):
        o2.write(a + '\n')