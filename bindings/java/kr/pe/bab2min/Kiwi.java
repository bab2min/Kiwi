package kr.pe.bab2min;

import java.util.Arrays;

public class Kiwi implements AutoCloseable  {
	private long _inst;

	public static class Match {
		final static public int none = 0,
		url = 1 << 0,
		email = 1 << 1,
		hashtag = 1 << 2,
		mention = 1 << 3,
		serial = 1 << 4,
		normalizeCoda = 1 << 16,
		joinNounPrefix = 1 << 17,
		joinNounSuffix = 1 << 18,
		joinVerbSuffix = 1 << 19,
		joinAdjSuffix = 1 << 20,
		joinAdvSuffix = 1 << 21,
		splitComplex = 1 << 22,
		zCoda = 1 << 23,
		joinVSuffix = joinVerbSuffix | joinAdjSuffix,
		joinAffix = joinNounPrefix | joinNounSuffix | joinVerbSuffix | joinAdjSuffix | joinAdvSuffix,
		all = url | email | hashtag | mention | serial | zCoda,
		allWithNormalizing = all | normalizeCoda;
	}
	
	public static class POSTag {
		final static public byte unknown = 0,
		nng = 1, nnp = 2, nnb = 3,
		vv = 4, va = 5,
		mag = 6,
		nr = 7, np = 8,
		vx = 9,
		mm = 10, maj = 11,
		ic = 12,
		xpn = 13, xsn = 14, xsv = 15, xsa = 16, xsm = 17, xr = 18,
		vcp = 19, vcn = 20,
		sf = 21, sp = 22, ss = 23, sso = 24, ssc = 25, se = 26, so = 27, sw = 28,
		sl = 29, sh = 30, sn = 31,
		w_url = 32, w_email = 33, w_mention = 34, w_hashtag = 35, w_serial = 36,
		jks = 37, jkc = 38, jkg = 39, jko = 40, jkb = 41, jkv = 42, jkq = 43, jx = 44, jc = 45,
		ep = 46, ef = 47, ec = 48, etn = 49, etm = 50,
		z_coda = 51,
		p = 52,
		max = 53,
		pv = p,
		pa = (byte)(p + 1),
		irregular = - 128,

		vvi = (byte)(vv | irregular),
		vai = (byte)(va | irregular),
		vxi = (byte)(vx | irregular),
		xsai = (byte)(xsa | irregular),
		pvi = (byte)(pv | irregular),
		pai = (byte)(pa | irregular);

		static String toString(byte tag) {
			switch(tag) {
				case unknown: return "UNK";
				case nng: return "NNG";
				case nnp: return "NNP";
				case nnb: return "NNB";
				case vv: return "VV";
				case va: return "VA";
				case mag: return "MAG";
				case nr: return "NR";
				case np: return "NP";
				case vx: return "VX";
				case mm: return "MM";
				case maj: return "MAJ";
				case ic: return "IC";
				case xpn: return "XPN";
				case xsn: return "XSN";
				case xsv: return "XSV";
				case xsa: return "XSA";
				case xsm: return "XSM";
				case xr: return "XR";
				case vcp: return "VCP";
				case vcn: return "VCN";
				case sf: return "SF";
				case sp: return "SP";
				case ss: return "SS";
				case sso: return "SSO";
				case ssc: return "SSC";
				case se: return "SE";
				case so: return "SO";
				case sw: return "SW";
				case sl: return "SL";
				case sh: return "SH";
				case sn: return "SN";
				case w_url: return "W_URL";
				case w_email: return "W_EMAIL";
				case w_mention: return "W_MENTION";
				case w_hashtag: return "W_HASHTAG";
				case w_serial: return "W_SERIAL";
				case jks: return "JKS";
				case jkc: return "JKC";
				case jkg: return "JKG";
				case jko: return "JKO";
				case jkb: return "JKB";
				case jkv: return "JKV";
				case jkq: return "JKQ";
				case jx: return "JX";
				case jc: return "JC";
				case ep: return "EP";
				case ef: return "EF";
				case ec: return "EC";
				case etn: return "ETN";
				case etm: return "ETM";
				case z_coda: return "Z_CODA";
				
				case vvi: return "VV-I";
				case vai: return "VA-I";
				case vxi: return "VX-I";
				case xsai: return "XSA-I";
			}
			return null;
		}
	}

	public static class Space {
		final public static byte none = 0,
		no_space = 1,
		insert_space = 2;
	}

	public static class Token {
		public String form;
		public int position;
		public int wordPosition;
		public int sentPosition;
		public int lineNumber;
		public short length;
		public byte senseId;
		public byte tag;
		public float score;
		public float typoCost;
		public int typoFormId;
		public int pairedToken;
		public int subSentPosition;

		public String toString() {
			return String.format("Token(form=%s, tag=%s, position=%d, length=%d)", form, POSTag.toString(tag), position, length);
		}
	}

	public static class TokenResult {
		public Token[] tokens;
		public float score;
	}

	public static class Sentence {
		public String text;
		public int start;
		public int end;
		public Sentence[] subSents;
		public Token[] tokens;

		public String toString() {
			return String.format("Sentence(text=%s, start=%d, end=%d, subSents=%s)", text, start, end, Arrays.toString(subSents));
		}
	}

	public static class JoinableToken {
		public String form;
		public byte tag;
		public boolean inferRegularity = true;
		public byte space = 0;

		public JoinableToken() {
		}

		public JoinableToken(String form, byte tag) {
			this.form = form;
			this.tag = tag;
		}

		public JoinableToken(String form, byte tag, boolean inferRegularity) {
			this.form = form;
			this.tag = tag;
			this.inferRegularity = inferRegularity;
		}

		public JoinableToken(String form, byte tag, boolean inferRegularity, byte space) {
			this.form = form;
			this.tag = tag;
			this.inferRegularity = inferRegularity;
			this.space = space;
		}

		public JoinableToken(Token token) {
			this.form = token.form;
			this.tag = token.tag;
			this.inferRegularity = false;
			this.space = 0;
		}
	}

	public Kiwi(long _inst) {
		this._inst = _inst;
	}

	public static Kiwi init(String modelPath, int numWorkers, int buildOptions, boolean useSBG) throws Exception {
		try(KiwiBuilder b = new KiwiBuilder(modelPath, numWorkers, buildOptions, useSBG)) {
			return b.build();
		}
	}

	public static Kiwi init(String modelPath, int numWorkers, int buildOptions) throws Exception {
		try(KiwiBuilder b = new KiwiBuilder(modelPath, numWorkers, buildOptions)) {
			return b.build();
		}
	}

	public static Kiwi init(String modelPath, int numWorkers) throws Exception {
		try(KiwiBuilder b = new KiwiBuilder(modelPath, numWorkers)) {
			return b.build();
		}
	}

	public static Kiwi init(String modelPath) throws Exception {
		try(KiwiBuilder b = new KiwiBuilder(modelPath)) {
			return b.build();
		}
	}

	protected void finalize() throws Exception {
		close();
	}

	@Override
	public native void close() throws Exception;

	public boolean isAlive() {
		return _inst != 0;
	}

	public native TokenResult[] analyze(String text, int topN, int matchOption);
	public native Sentence[] splitIntoSents(String text, int matchOption, boolean returnTokens);
	public native String join(JoinableToken[] tokens);

	public Token[] tokenize(String text, int matchOption) {
		return analyze(text, 1, matchOption)[0].tokens;
	}

	public Sentence[] splitIntoSents(String text, int matchOption) {
		return splitIntoSents(text, matchOption, false);
	}

	static {
		System.loadLibrary("KiwiJava");
	}
}
