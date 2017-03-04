d = {}
for line in open('sform.txt', encoding='utf-8'):
    ch = line.strip().split('\t')
    if len(ch) < 2: continue
    ords = map(ord, ch[0])
    if ch[1] in d: [d[ch[1]].add(i) for i in ords]
    else: d[ch[1]] = set(ords)

for k, v in d.items():
    print(k, sorted(v))

for i in sorted(d['SW']):
    if i < 128: print("case '%s':" % chr(i))
    else: print("case %s:" % hex(i))

