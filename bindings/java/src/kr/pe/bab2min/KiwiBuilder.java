package kr.pe.bab2min;

import java.io.InputStream;

public class KiwiBuilder implements AutoCloseable  {
	private long _inst;

	@FunctionalInterface
	public interface StreamProvider {
		/**
		 * Provides an InputStream for the specified model file.
		 * 
		 * @param filename The name of the model file to read
		 * @return InputStream containing the file data, or null if the file cannot be provided
		 */
		InputStream provide(String filename);
	}

	public static class BuildOption	{
		final static public int none = 0,
		integrateAllomorph = 1 << 0,
		loadDefaultDict = 1 << 1,
		loadTypoDict = 1 << 2,
		loadMultiDict = 1 << 3,
		default_ = integrateAllomorph | loadDefaultDict | loadTypoDict | loadMultiDict;
	}

	public static class ModelType {
		final static public int none = 0,
		largest = 1,
		knlm = 2,
		sbg = 3,
		cong = 4,
		congGlobal = 5;
	}

	public static class AnalyzedMorph {
		public String form;
		public byte tag = Kiwi.POSTag.nng;
		public int start = -1, end = -1;

		public AnalyzedMorph() {	
		}

		public AnalyzedMorph(String form, byte tag) {
			this.form = form;
			this.tag = tag;
		}

		public AnalyzedMorph(String form, byte tag, int start, int end) {
			this.form = form;
			this.tag = tag;
			this.start = start;
			this.end = end;
		}
	}

	public static class CondVowel	{
		final static public byte none = 0,
		any = 1,
		vowel = 2,
		applosive = 8;
	}

	public static class TypoTransformer implements AutoCloseable {
		private long _inst;

		public TypoTransformer(long _inst) {
			this._inst = _inst;
		}

		public TypoTransformer() {
			ctor();
		}

		protected void finalize() throws Exception {
			close();
		}

		public boolean isAlive() {
			return _inst != 0;
		}

		private native void ctor();

		@Override
		public native void close() throws Exception;
		public native TypoTransformer copy();
		public native void _addTypo(String orig, String error, float cost, byte convVowel, short dialect);
		public native void _update(TypoTransformer src);
		public native void _scaleCost(float scale);
		public native void _setContinualTypoCost(float cost);
		public native void _setLengtheningTypoCost(float cost);

		public TypoTransformer addTypo(String orig, String error, float cost, byte convVowel, short dialect) {
			_addTypo(orig, error, cost, convVowel, dialect);
			return this;
		}

		public TypoTransformer addTypo(String orig, String error, float cost, byte convVowel) {
			return addTypo(orig, error, cost, convVowel, (short)0);
		}

		public TypoTransformer addTypo(String[] orig, String[] error, float cost, byte convVowel, short dialect) {
			for (int i = 0; i < orig.length; ++i) {
				for (int j = 0; j < error.length; ++j) {
					_addTypo(orig[i], error[j], cost, convVowel, dialect);
				}
			}
			return this;
		}

		public TypoTransformer addTypo(String[] orig, String[] error, float cost, byte convVowel) {
			return addTypo(orig, error, cost, convVowel, (short)0);
		}

		// Set continual typo cost (inplace)
		public TypoTransformer setContinualTypoCost(float cost) {
			_setContinualTypoCost(cost);
			return this;
		}

		// Set lengthening typo cost (inplace)
		public TypoTransformer setLengtheningTypoCost(float cost) {
			_setLengtheningTypoCost(cost);
			return this;
		}

		// Inplace update
		public TypoTransformer update(TypoTransformer src) {
			_update(src);
			return this;
		}

		// Inplace scaling
		public TypoTransformer scaleCost(float scale) {
			_scaleCost(scale);
			return this;
		}
	}

	public KiwiBuilder(long _inst) {
		this._inst = _inst;
	}

	public KiwiBuilder(String modelPath, int numWorkers, int buildOptions, int modelType, short enabledDialects) {
		ctor(modelPath, numWorkers, buildOptions, modelType, enabledDialects);
	}

	public KiwiBuilder(StreamProvider streamProvider, int numWorkers, int buildOptions, int modelType, short enabledDialects) {
		ctor(streamProvider, numWorkers, buildOptions, modelType, enabledDialects);
	}

	public KiwiBuilder(String modelPath, int numWorkers, int buildOptions, int modelType) {
		ctor(modelPath, numWorkers, buildOptions, modelType, Kiwi.Dialect.standard);
	}

	public KiwiBuilder(StreamProvider streamProvider, int numWorkers, int buildOptions, int modelType) {
		ctor(streamProvider, numWorkers, buildOptions, modelType, Kiwi.Dialect.standard);
	}

	public KiwiBuilder(String modelPath, int numWorkers, int buildOptions) {
		ctor(modelPath, numWorkers, buildOptions, ModelType.none, Kiwi.Dialect.standard);
	}

	public KiwiBuilder(StreamProvider streamProvider, int numWorkers, int buildOptions) {
		ctor(streamProvider, numWorkers, buildOptions, ModelType.none, Kiwi.Dialect.standard);
	}

	public KiwiBuilder(String modelPath, int numWorkers) {
		ctor(modelPath, numWorkers, BuildOption.default_, ModelType.none, Kiwi.Dialect.standard);
	}

	public KiwiBuilder(StreamProvider streamProvider, int numWorkers) {
		ctor(streamProvider, numWorkers, BuildOption.default_, ModelType.none, Kiwi.Dialect.standard);
	}

	public KiwiBuilder(String modelPath) {
		ctor(modelPath, 1, BuildOption.default_, ModelType.none, Kiwi.Dialect.standard);
	}

	public KiwiBuilder(StreamProvider streamProvider) {
		ctor(streamProvider, 1, BuildOption.default_, ModelType.none, Kiwi.Dialect.standard);
	}

	protected void finalize() throws Exception {
		close();
	}

	public boolean isAlive() {
		return _inst != 0;
	}

	private native void ctor(String modelPath, int numWorkers, int buildOptions, int modelType, short enabledDialects);
	private native void ctor(StreamProvider streamProvider, int numWorkers, int buildOptions, int modelType, short enabledDialects);

	@Override
	public native void close() throws Exception;
	
	public native Kiwi build(TypoTransformer typos, float typoCostThreshold);
	public native boolean addWord(String form, byte tag, float score);
	public native boolean addWord(String form, byte tag, float score, String origForm);
	public native boolean addPreAnalyzedWord(String form, AnalyzedMorph[] analyzed, float score);
	public native int loadDictionary(String path);

	public Kiwi build() {
		return build(null, 0);
	}

	public Kiwi build(TypoTransformer typos) {
		return build(typos, 2.5f);
	}

	static {
		Kiwi.loadLibrary();
	}

	final public static TypoTransformer basicTypoSet = new TypoTransformer()
		.addTypo(new String[]{"ㅐ", "ㅔ"}, new String[]{"ㅐ", "ㅔ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ㅐ", "ㅔ"}, new String[]{"ㅒ", "ㅖ"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"ㅒ", "ㅖ"}, new String[]{"ㅐ", "ㅔ"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"ㅒ", "ㅖ"}, new String[]{"ㅒ", "ㅖ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ㅚ", "ㅙ", "ㅞ"}, new String[]{"ㅚ", "ㅙ", "ㅞ", "ㅐ", "ㅔ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ㅝ"}, new String[]{"ㅗ", "ㅓ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ㅟ", "ㅢ"}, new String[]{"ㅣ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"위", "의"}, new String[]{"이"}, Float.POSITIVE_INFINITY, CondVowel.none)
		.addTypo(new String[]{"위", "의"}, new String[]{"이"}, 1.f, CondVowel.any)
		.addTypo(new String[]{"자", "쟈"}, new String[]{"자", "쟈"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"재", "쟤"}, new String[]{"재", "쟤"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"저", "져"}, new String[]{"저", "져"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"제", "졔"}, new String[]{"제", "졔"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"조", "죠", "줘"}, new String[]{"조", "죠", "줘"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"주", "쥬"}, new String[]{"주", "쥬"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"차", "챠"}, new String[]{"차", "챠"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"채", "챼"}, new String[]{"채", "챼"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"처", "쳐"}, new String[]{"처", "쳐"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"체", "쳬"}, new String[]{"체", "쳬"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"초", "쵸", "춰"}, new String[]{"초", "쵸", "춰"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"추", "츄"}, new String[]{"추", "츄"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"유", "류"}, new String[]{"유", "류"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"므", "무"}, new String[]{"므", "무"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"브", "부"}, new String[]{"브", "부"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"프", "푸"}, new String[]{"프", "푸"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"르", "루"}, new String[]{"르", "루"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"러", "뤄"}, new String[]{"러", "뤄"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆩ", "ᆪ"}, new String[]{"ᆨ", "ᆩ", "ᆪ"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"ᆬ", "ᆭ"}, new String[]{"ᆫ", "ᆬ", "ᆭ"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"ᆰ", "ᆱ", "ᆲ", "ᆳ", "ᆴ", "ᆵ", "ᆶ"}, new String[]{"ᆯ", "ᆰ", "ᆱ", "ᆲ", "ᆳ", "ᆴ", "ᆵ", "ᆶ"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"ᆺ", "ᆻ"}, new String[]{"ᆺ", "ᆻ"}, 1.f, CondVowel.none)

		.addTypo(new String[]{"안"}, new String[]{"않"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"맞추", "맞히"}, new String[]{"맞추", "맞히"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"맞춰", "맞혀"}, new String[]{"맞춰", "맞혀"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"받치", "바치", "받히"}, new String[]{"받치", "바치", "받히"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"받쳐", "바쳐", "받혀"}, new String[]{"받쳐", "바쳐", "받혀"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"던", "든"}, new String[]{"던", "든"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"때", "데"}, new String[]{"때", "데"}, 1.5f, CondVowel.none)
		.addTypo(new String[]{"빛", "빚"}, new String[]{"빛", "빚"}, 1.f, CondVowel.none)

		.addTypo(new String[]{"ᆮ이", "지"}, new String[]{"ᆮ이", "지"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆮ여", "져"}, new String[]{"ᆮ여", "져"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᇀ이", "치"}, new String[]{"ᇀ이", "치"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᇀ여", "쳐"}, new String[]{"ᇀ여", "쳐"}, 1.f, CondVowel.none)
		
		.addTypo(new String[]{"ᄀ", "ᄁ"}, new String[]{"ᄀ", "ᄁ"}, 1.f, CondVowel.applosive)
		.addTypo(new String[]{"ᄃ", "ᄄ"}, new String[]{"ᄃ", "ᄄ"}, 1.f, CondVowel.applosive)
		.addTypo(new String[]{"ᄇ", "ᄈ"}, new String[]{"ᄇ", "ᄈ"}, 1.f, CondVowel.applosive)
		.addTypo(new String[]{"ᄉ", "ᄊ"}, new String[]{"ᄉ", "ᄊ"}, 1.f, CondVowel.applosive)
		.addTypo(new String[]{"ᄌ", "ᄍ"}, new String[]{"ᄌ", "ᄍ"}, 1.f, CondVowel.applosive)

		.addTypo(new String[]{"ᇂᄒ", "ᆨᄒ", "ᇂᄀ"}, new String[]{"ᇂᄒ", "ᆨᄒ", "ᇂᄀ"}, 1.f, CondVowel.none)

		.addTypo(new String[]{"ᆨᄂ", "ᆩᄂ", "ᆪᄂ", "ᆿᄂ", "ᆼᄂ"}, new String[]{"ᆨᄂ", "ᆩᄂ", "ᆪᄂ", "ᆿᄂ", "ᆼᄂ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆨᄆ", "ᆩᄆ", "ᆪᄆ", "ᆿᄆ", "ᆼᄆ"}, new String[]{"ᆨᄆ", "ᆩᄆ", "ᆪᄆ", "ᆿᄆ", "ᆼᄆ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆨᄅ", "ᆩᄅ", "ᆪᄅ", "ᆿᄅ", "ᆼᄅ", "ᆼᄂ",}, new String[]{"ᆨᄅ", "ᆩᄅ", "ᆪᄅ", "ᆿᄅ", "ᆼᄅ", "ᆼᄂ",}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆮᄂ", "ᆺᄂ", "ᆻᄂ", "ᆽᄂ", "ᆾᄂ", "ᇀᄂ", "ᆫᄂ"}, new String[]{"ᆮᄂ", "ᆺᄂ", "ᆻᄂ", "ᆽᄂ", "ᆾᄂ", "ᇀᄂ", "ᆫᄂ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆮᄆ", "ᆺᄆ", "ᆻᄆ", "ᆽᄆ", "ᆾᄆ", "ᇀᄆ", "ᆫᄆ"}, new String[]{"ᆮᄆ", "ᆺᄆ", "ᆻᄆ", "ᆽᄆ", "ᆾᄆ", "ᇀᄆ", "ᆫᄆ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆮᄅ", "ᆺᄅ", "ᆻᄅ", "ᆽᄅ", "ᆾᄅ", "ᇀᄅ", "ᆫᄅ", "ᆫᄂ",}, new String[]{"ᆮᄅ", "ᆺᄅ", "ᆻᄅ", "ᆽᄅ", "ᆾᄅ", "ᇀᄅ", "ᆫᄅ", "ᆫᄂ",}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆸᄂ", "ᆹᄂ", "ᇁᄂ", "ᆷᄂ"}, new String[]{"ᆸᄂ", "ᆹᄂ", "ᇁᄂ", "ᆷᄂ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆸᄆ", "ᆹᄆ", "ᇁᄆ", "ᆷᄆ"}, new String[]{"ᆸᄆ", "ᆹᄆ", "ᇁᄆ", "ᆷᄆ"}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆸᄅ", "ᆹᄅ", "ᇁᄅ", "ᆷᄅ", "ᆷᄂ",}, new String[]{"ᆸᄅ", "ᆹᄅ", "ᇁᄅ", "ᆷᄅ", "ᆷᄂ",}, 1.f, CondVowel.none)
		.addTypo(new String[]{"ᆫᄅ", "ᆫᄂ", "ᆯᄅ", "ᆯᄂ"}, new String[]{"ᆫᄅ", "ᆫᄂ", "ᆯᄅ", "ᆯᄂ"}, 1.f, CondVowel.none)
		
		.addTypo(new String[]{"ᆨᄋ", "ᄀ"}, new String[]{"ᆨᄋ", "ᄀ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆩᄋ", "ᄁ"}, new String[]{"ᆩᄋ", "ᄁ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆫᄋ", "ᆫᄒ", "ᄂ"}, new String[]{"ᆫᄋ", "ᆫᄒ", "ᄂ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆬᄋ", "ᆫᄌ"}, new String[]{"ᆬᄋ", "ᆫᄌ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆭᄋ", "ᄂ"}, new String[]{"ᆭᄋ", "ᄂ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆮᄋ", "ᄃ"}, new String[]{"ᆮᄋ", "ᄃ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆯᄋ", "ᆯᄒ", "ᄅ"}, new String[]{"ᆯᄋ", "ᆯᄒ", "ᄅ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆰᄋ", "ᆯᄀ"}, new String[]{"ᆰᄋ", "ᆯᄀ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆰᄒ", "ᆯᄏ"}, new String[]{"ᆰᄒ", "ᆯᄏ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆷᄋ", "ᄆ"}, new String[]{"ᆷᄋ", "ᄆ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆸᄋ", "ᄇ"}, new String[]{"ᆸᄋ", "ᄇ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆺᄋ", "ᄉ"}, new String[]{"ᆺᄋ", "ᄉ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆻᄋ", "ᆺᄉ", "ᄊ"}, new String[]{"ᆻᄋ", "ᆺᄉ", "ᄊ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆽᄋ", "ᄌ"}, new String[]{"ᆽᄋ", "ᄌ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆾᄋ", "ᆾᄒ", "ᆽᄒ", "ᄎ"}, new String[]{"ᆾᄋ", "ᆾᄒ", "ᆽᄒ", "ᄎ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᆿᄋ", "ᆿᄒ", "ᆨᄒ", "ᄏ"}, new String[]{"ᆿᄋ", "ᆿᄒ", "ᆨᄒ", "ᄏ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᇀᄋ", "ᇀᄒ", "ᆮᄒ", "ᄐ"}, new String[]{"ᇀᄋ", "ᇀᄒ", "ᆮᄒ", "ᄐ"}, 1.f, CondVowel.vowel)
		.addTypo(new String[]{"ᇁᄋ", "ᇁᄒ", "ᆸᄒ", "ᄑ"}, new String[]{"ᇁᄋ", "ᇁᄒ", "ᆸᄒ", "ᄑ"}, 1.f, CondVowel.vowel)

		.addTypo(new String[]{"은", "는"}, new String[]{"은", "는"}, 2.f, CondVowel.none)
		.addTypo(new String[]{"을", "를"}, new String[]{"을", "를"}, 2.f, CondVowel.none)

		.addTypo(new String[]{"ㅣ워", "ㅣ어", "ㅕ"}, new String[]{"ㅣ워", "ㅣ어", "ㅕ"}, 1.5f, CondVowel.none);

	final public static TypoTransformer continualTypoSet = new TypoTransformer()
		.setContinualTypoCost(1.f)
		.addTypo(new String[]{"ᆪ"}, new String[]{"ᆨᆺ", "ᆨᆻ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆬ"}, new String[]{"ᆫᆽ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆭ"}, new String[]{"ᆫᇂ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆰ"}, new String[]{"ᆯᆨ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆱ"}, new String[]{"ᆯᆷ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆲ"}, new String[]{"ᆯᆸ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆳ"}, new String[]{"ᆯᆺ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆴ"}, new String[]{"ᆯᇀ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆵ"}, new String[]{"ᆯᇁ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆶ"}, new String[]{"ᆯᇂ"}, 1e-12f, CondVowel.none)
		.addTypo(new String[]{"ᆹ"}, new String[]{"ᆸᆺ", "ᆸᆻ"}, 1e-12f, CondVowel.none);

	final public static TypoTransformer basicTypoSetWithContinual = basicTypoSet.copy().update(continualTypoSet);

	final public static TypoTransformer lengtheningTypoSet = new TypoTransformer().setLengtheningTypoCost(0.5f);
}
