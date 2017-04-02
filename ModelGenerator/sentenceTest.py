def convert(input, output):
    with open(input, 'r', encoding='utf-8') as i:
        with open(output, 'w', encoding='utf-8') as o:
            q = []
            a = []
            for line in i:
                t = line.strip().split('\t')
                q.append(t[0])
                a += t[1:]
                if a and a[-1].endswith('/SF'):
                    o.write(' '.join(q) + '\t' + '\t'.join(a) + '\n')
                    q = []
                    a = []
            if a:
                o.write(' '.join(q) + '\t' + '\t'.join(a) + '\n')

convert('../TestSets/01.txt', '../TestSets/01s.txt')
convert('../TestSets/02.txt', '../TestSets/02s.txt')
convert('../TestSets/03.txt', '../TestSets/03s.txt')