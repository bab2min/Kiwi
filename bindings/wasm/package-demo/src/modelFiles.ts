export const requiredModelFiles = [
    'combiningRule.txt',
    'contraction.dict',
    'default.dict',
    'extract.mdl',
    'multi.dict',
    'sj.knlm',
    'sj.morph',
    'skipbigram.mdl',
    'typo.dict',
];

export const modelFiles = Object.fromEntries(
    requiredModelFiles.map((f) => [f, '/model/' + f])
);
