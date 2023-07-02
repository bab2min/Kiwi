package kr.pe.bab2min;

import java.util.Arrays;

import org.junit.Test;
import static org.junit.Assert.*;

public class KiwiTest {

	private static String modelPath = "../../ModelGenerator";
	private static Kiwi reusableInst = null;

	Kiwi getReusableKiwi() throws Exception {
		if (reusableInst == null) {
			reusableInst = Kiwi.init(modelPath);
		}
		return reusableInst;
	}

	@Test
	public void testInit() throws Exception {
		Kiwi kiwi = Kiwi.init(modelPath);
		System.out.println(Arrays.deepToString(kiwi.tokenize("자바에서도 Kiwi를!", Kiwi.Match.allWithNormalizing)));
	}

	@Test
	public void testLoadDictionaryException() throws Exception {
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
		KiwiBuilder builder = new KiwiBuilder(modelPath, 1, KiwiBuilder.BuildOption.none);
		long added = builder.loadDictionary(modelPath + "/default.dict");
		assertTrue(added > 0);

		System.out.println("Success testLoadDictionary");
	}

	@Test
	public void testAddWord() throws Exception {
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		assertEquals(builder.addWord("키위자바", Kiwi.POSTag.nnp, 0.f), true);
		assertEquals(builder.addWord("좌봐", Kiwi.POSTag.nnp, 0.f, "자바"), true);
		Kiwi kiwi = builder.build();
		Kiwi.Token[] tokens = kiwi.tokenize("좌봐에서도 키위자바를!", Kiwi.Match.allWithNormalizing);
		assertEquals(tokens[0].form, "좌봐");
		assertEquals(tokens[3].form, "키위자바");
	}
	
	@Test
	public void testSplitIntoSents() throws Exception {
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
		String text = "맞혔습니까";
		Kiwi.Token[] tokens = getReusableKiwi().tokenize(text, Kiwi.Match.allWithNormalizing);
		Kiwi.JoinableToken[] jtokens = new Kiwi.JoinableToken[tokens.length];
		for(int i = 0; i < tokens.length; ++i) {
			jtokens[i] = new Kiwi.JoinableToken(tokens[i]);
		}
		String restored = getReusableKiwi().join(jtokens);
		assertEquals(text, restored);
	}
}
