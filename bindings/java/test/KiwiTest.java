package test;

import java.util.Arrays;
import java.util.concurrent.Future;

import org.junit.Test;

import kr.pe.bab2min.KiwiBuilder.TypoTransformer;

import static org.junit.Assert.*;

import kr.pe.bab2min.Kiwi;
import kr.pe.bab2min.Kiwi.AnalyzeOption;
import kr.pe.bab2min.KiwiBuilder;

public class KiwiTest {

	private static String modelPath = "../../models/cong/base";
	private static Kiwi reusableInst = null;

	Kiwi getReusableKiwi() throws Exception {
		if (reusableInst == null) {
			reusableInst = Kiwi.init(modelPath);
		}
		return reusableInst;
	}

	@Test
	public void testVersion() {
		System.gc();
		System.out.println(Kiwi.getVersion());
	}

	@Test
	public void testInit() throws Exception {
		System.gc();
		Kiwi kiwi = Kiwi.init(modelPath);
		System.out.println(Arrays.deepToString(kiwi.tokenize("자바에서도 Kiwi를!", new AnalyzeOption(Kiwi.Match.allWithNormalizing))));
	}

	@Test
	public void testLoadDictionaryException() throws Exception {
		System.gc();
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		
		boolean exception = false;
		try {
			builder.loadDictionary("NotFoundFile");
		} catch(Exception e) {
			exception = true;
		}
		assertEquals(exception, true);
		System.out.println("Success testLoadDictionaryException");
	}

	@Test
	public void testLoadDictionary() throws Exception {
		System.gc();
		KiwiBuilder builder = new KiwiBuilder(modelPath, 1, KiwiBuilder.BuildOption.none);
		int added = builder.loadDictionary(modelPath + "/default.dict");
		assertTrue(added > 0);

		System.out.println("Success testLoadDictionary");
	}

	@Test
	public void testMultiThread() throws Exception {
		System.gc();
		Kiwi kiwi = Kiwi.init(modelPath, 3);

		String[] texts = new String[] {
			"첫번째 문장",
			"두번째 문장",
			"세번째 문장",
			"네번째 문장",
		};
		Kiwi.FutureTokenResult[] futures = new Kiwi.FutureTokenResult[texts.length];

		for(int i = 0; i < texts.length; ++i) {
			futures[i] = kiwi.asyncAnalyze(texts[i], 1, new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		}
		
		for(Kiwi.FutureTokenResult future : futures) {
			System.out.println(Arrays.deepToString(future.get()[0].tokens));
		}

		Kiwi.MultipleTokenResult results = kiwi.analyze(Arrays.stream(texts).iterator(), 1, new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		while(results.hasNext()) {
			Kiwi.TokenResult[] result = results.next();
			System.out.println(Arrays.deepToString(result[0].tokens));
		}
	}

	@Test
	public void testAddWord() throws Exception {
		System.gc();
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		assertEquals(builder.addWord("키위자바", Kiwi.POSTag.nnp, 0.f), true);
		assertEquals(builder.addWord("좌봐", Kiwi.POSTag.nnp, 0.f, "자바"), true);
		Kiwi kiwi = builder.build();
		Kiwi.Token[] tokens = kiwi.tokenize("좌봐에서도 키위자바를!", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		assertEquals(tokens[0].form, "좌봐");
		assertEquals(tokens[3].form, "키위자바");
	}
	
	@Test
	public void testAddPreAnalyzedWord() throws Exception {
		System.gc();
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		KiwiBuilder.AnalyzedMorph[] morphs = new KiwiBuilder.AnalyzedMorph[] {
			new KiwiBuilder.AnalyzedMorph("너무", Kiwi.POSTag.mag),
			new KiwiBuilder.AnalyzedMorph("하", Kiwi.POSTag.xsv),
			new KiwiBuilder.AnalyzedMorph("어", Kiwi.POSTag.ef),
		};
		assertEquals(builder.addPreAnalyzedWord("넘해", morphs, 0.f), true);
		Kiwi kiwi = builder.build();
		Kiwi.Token[] tokens = kiwi.tokenize("그건좀넘해", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
	}
	
	@Test
	public void testTypos() throws Exception {
		System.gc();
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		Kiwi kiwi = builder.build(KiwiBuilder.basicTypoSet);
		Kiwi.Token[] tokens = kiwi.tokenize("나 죰 도와죠.", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[1].form, "좀");
		assertEquals(tokens[4].form, "주");
		assertEquals(tokens[5].form, "어");
	}

	@Test
	public void testContinualTypos() throws Exception {
		System.gc();
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		Kiwi kiwi = builder.build(KiwiBuilder.continualTypoSet);

		Kiwi.Token[] tokens = kiwi.tokenize("프로그래미", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[0].form, "프로그램");
		assertEquals(tokens[1].form, "이");

		tokens = kiwi.tokenize("프로그래믈", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[0].form, "프로그램");
		assertEquals(tokens[1].form, "을");

		tokens = kiwi.tokenize("오늘사무시레서", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[1].form, "사무실");
		assertEquals(tokens[2].form, "에서");

		tokens = kiwi.tokenize("법원이 기가캤다.", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[2].form, "기각");
		assertEquals(tokens[3].form, "하");

		tokens = kiwi.tokenize("하나도 업써.", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[2].form, "없");
		assertEquals(tokens[3].form, "어");
	}

	@Test
	public void testCustomTypos() throws Exception {
		System.gc();
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		TypoTransformer typoSet = KiwiBuilder.basicTypoSet.copy()
									.update(KiwiBuilder.continualTypoSet)
									.update(KiwiBuilder.lengtheningTypoSet);
		Kiwi kiwi = builder.build(typoSet);
		
		Kiwi.Token[] tokens = kiwi.tokenize("프로그래미", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[0].form, "프로그램");
		assertEquals(tokens[1].form, "이");

		tokens = kiwi.tokenize("지인짜?", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[0].form, "진짜");
		assertEquals(tokens[1].form, "?");

		tokens = kiwi.tokenize("맗은 물", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[0].form, "맑");
	}

	@Test
	public void testBlocklist() throws Exception {
		System.gc();
		Kiwi kiwi = getReusableKiwi();
		Kiwi.Token[] tokens = kiwi.tokenize("좋아하다.", new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[0].form, "좋아하");
		
		Kiwi.MorphemeSet morphSet = kiwi.newMorphemeSet();
		assertTrue(morphSet.add("좋아하") > 0);
		tokens = kiwi.tokenize("좋아하다.", new AnalyzeOption(Kiwi.Match.allWithNormalizing, morphSet));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[0].form, "좋");
	}

	@Test
	public void testPretokenized() throws Exception {
		System.gc();
		Kiwi kiwi = getReusableKiwi();
		String str = "드디어패트와 매트가 2017년에 국내 개봉했다. 패트와매트는 2016년...";
		Kiwi.PretokenizedSpan[] pretokenized = new Kiwi.PretokenizedSpan[]{
			new Kiwi.PretokenizedSpan(3, 9),
			new Kiwi.PretokenizedSpan(11, 16),
			new Kiwi.PretokenizedSpan(34, 39),
		};
		Kiwi.Token[] tokens = kiwi.tokenize(str, new AnalyzeOption(Kiwi.Match.allWithNormalizing), Arrays.stream(pretokenized).iterator());
		assertEquals(tokens[1].form, "패트와 매트");
		assertEquals(tokens[3].form, "2017년");
		assertEquals(tokens[13].form, "2016년");

		pretokenized = new Kiwi.PretokenizedSpan[]{
			new Kiwi.PretokenizedSpan(27, 29, new Kiwi.BasicToken[]{ new Kiwi.BasicToken("페트", 0, 2, Kiwi.POSTag.nnb) }),
			new Kiwi.PretokenizedSpan(30, 32),
			new Kiwi.PretokenizedSpan(21, 24,  new Kiwi.BasicToken[]{ new Kiwi.BasicToken("개봉하", 0, 3, Kiwi.POSTag.vv), new Kiwi.BasicToken("었", 2, 3, Kiwi.POSTag.ep) }),
		};
		tokens = kiwi.tokenize(str, new AnalyzeOption(Kiwi.Match.allWithNormalizing), Arrays.stream(pretokenized).iterator());
		
		assertEquals(tokens[7].form, "개봉하");
		assertEquals(tokens[7].tag, Kiwi.POSTag.vv);
		assertEquals(tokens[7].position, 21);
		assertEquals(tokens[7].length, 3);
		assertEquals(tokens[8].form, "었");
		assertEquals(tokens[8].tag, Kiwi.POSTag.ep);
		assertEquals(tokens[8].position, 23);
		assertEquals(tokens[8].length, 1);
		assertEquals(tokens[11].form, "페트");
		assertEquals(tokens[11].tag, Kiwi.POSTag.nnb);
		assertEquals(tokens[13].form, "매트");
		assertEquals(tokens[13].tag, Kiwi.POSTag.nng);
	}
	
	@Test
	public void testSplitIntoSents() throws Exception {
		System.gc();
		Kiwi.Sentence[] sents = getReusableKiwi().splitIntoSents("안녕하십니까 이건 총 몇 문장일까", Kiwi.Match.allWithNormalizing);
		System.out.println(Arrays.deepToString(sents));
		assertEquals(sents.length, 2);
		assertEquals(sents[0].text, "안녕하십니까");
		assertEquals(sents[1].text, "이건 총 몇 문장일까");
		assertEquals(sents[0].tokens, null);
		assertEquals(sents[1].tokens, null);

		sents = getReusableKiwi().splitIntoSents("안녕하십니까 이건 총 몇 문장일까", Kiwi.Match.allWithNormalizing, true);
		assertEquals(sents.length, 2);
		assertEquals(sents[0].text, "안녕하십니까");
		assertEquals(sents[1].text, "이건 총 몇 문장일까");
		assertNotEquals(sents[0].tokens, null);
		assertEquals(sents[0].tokens.length, 4);
		assertNotEquals(sents[1].tokens, null);
		assertEquals(sents[1].tokens.length, 7);
		System.out.println(Arrays.deepToString(sents[0].tokens));
		System.out.println(Arrays.deepToString(sents[1].tokens));
	}

	@Test
	public void testJoin() throws Exception {
		System.gc();
		String text = "맞혔습니까";
		Kiwi.Token[] tokens = getReusableKiwi().tokenize(text, new AnalyzeOption(Kiwi.Match.allWithNormalizing));
		Kiwi.JoinableToken[] jtokens = new Kiwi.JoinableToken[tokens.length];
		for(int i = 0; i < tokens.length; ++i) {
			jtokens[i] = new Kiwi.JoinableToken(tokens[i]);
		}
		String restored = getReusableKiwi().join(jtokens);
		assertEquals(text, restored);
	}

	@Test
	public void testDialect() throws Exception {
		System.gc();
		Kiwi kiwi = Kiwi.init(modelPath, 1, KiwiBuilder.BuildOption.default_, KiwiBuilder.ModelType.none, Kiwi.Dialect.chungcheong);
		Kiwi.Token[] tokens = kiwi.tokenize("남자 손금은 오약손으루 보는 겨.", new AnalyzeOption(Kiwi.Match.allWithNormalizing, null, Kiwi.Dialect.chungcheong, 6.f));
		System.out.println(Arrays.deepToString(tokens));
		assertEquals(tokens[3].form, "오약손");
		assertTrue((tokens[3].dialect & Kiwi.Dialect.chungcheong) != 0);
		assertEquals(tokens[7].form, "기");
		assertTrue((tokens[7].dialect & Kiwi.Dialect.chungcheong) != 0);
	}
}
