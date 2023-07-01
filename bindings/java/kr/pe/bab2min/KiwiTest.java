package kr.pe.bab2min;

import java.util.Arrays;

import org.junit.Test;
import static org.junit.Assert.*;

public class KiwiTest {

	private static String modelPath = "../../ModelGenerator";

	@Test
	public void testInit() {
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		Kiwi kiwi = builder.build();
		System.out.println(Arrays.deepToString(kiwi.tokenize("자바에서도 Kiwi를!", Kiwi.Match.allWithNormalizing)));
	}

	@Test
	public void testLoadDictionaryException() {
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
	public void testLoadDictionary() {
		KiwiBuilder builder = new KiwiBuilder(modelPath, 1, KiwiBuilder.BuildOption.none);
		long added = builder.loadDictionary(modelPath + "/default.dict");
		assertTrue(added > 0);

		System.out.println("Success testLoadDictionary");
	}

	@Test
	public void testAddWord() {
		KiwiBuilder builder = new KiwiBuilder(modelPath);
		assertEquals(builder.addWord("키위자바", Kiwi.POSTag.nnp, 0.f), true);
		assertEquals(builder.addWord("좌봐", Kiwi.POSTag.nnp, 0.f, "자바"), true);
		Kiwi kiwi = builder.build();
		Kiwi.Token[] tokens = kiwi.tokenize("좌봐에서도 키위자바를!", Kiwi.Match.allWithNormalizing);
		assertEquals(tokens[0].form, "좌봐");
		assertEquals(tokens[3].form, "키위자바");
	}
}
