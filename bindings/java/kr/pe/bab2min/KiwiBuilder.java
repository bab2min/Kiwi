package kr.pe.bab2min;

public class KiwiBuilder implements AutoCloseable  {
	private long _inst;

	public static class BuildOption	{
		final static public int none = 0,
		integrateAllomorph = 1 << 0,
		loadDefaultDict = 1 << 1,
		loadTypoDict = 1 << 2,
		default_ = integrateAllomorph | loadDefaultDict | loadTypoDict;
	}

	public static class AnalyzedMorph {
		public String form;
		public byte tag = Kiwi.POSTag.nng;
		public int start = -1, end = -1;
	}

	public KiwiBuilder(long _inst) {
		this._inst = _inst;
	}

	public KiwiBuilder(String modelPath, int numWorkers, int buildOptions, boolean useSBG) {
		ctor(modelPath, numWorkers, buildOptions, useSBG);
	}

	public KiwiBuilder(String modelPath, int numWorkers, int buildOptions) {
		ctor(modelPath, numWorkers, buildOptions, false);
	}

	public KiwiBuilder(String modelPath, int numWorkers) {
		ctor(modelPath, numWorkers, BuildOption.default_, false);
	}

	public KiwiBuilder(String modelPath) {
		ctor(modelPath, 1, BuildOption.default_, false);
	}

	protected void finalize() throws Exception {
		close();
	}

	public boolean isAlive() {
		return _inst != 0;
	}

	private native void ctor(String modelPath, int numWorkers, int buildOptions, boolean useSBG);
	
	@Override
	public native void close() throws Exception;
	
	public native Kiwi build();
	public native boolean addWord(String form, byte tag, float score);
	public native boolean addWord(String form, byte tag, float score, String origForm);
	public native boolean addPreAnalyzedWord(String form, AnalyzedMorph[] analyzed, float score);
	public native int loadDictionary(String path);

	static {
		System.loadLibrary("KiwiJava");
	}
}
