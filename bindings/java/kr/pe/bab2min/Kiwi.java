package kr.pe.bab2min;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.Iterator;
import java.util.Scanner;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

public class Kiwi implements AutoCloseable  {
	private long _inst;
	final private static String _version = "0.16.0";

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
		sf = 21, sp = 22, ss = 23, sso = 24, ssc = 25, se = 26, so = 27, sw = 28, sb = 29,
		sl = 30, sh = 31, sn = 32,
		w_url = 33, w_email = 34, w_mention = 35, w_hashtag = 36, w_serial = 37,
		jks = 38, jkc = 39, jkg = 40, jko = 41, jkb = 42, jkv = 43, jkq = 44, jx = 45, jc = 46,
		ep = 47, ef = 48, ec = 49, etn = 50, etm = 51,
		z_coda = 52,
		user0 = 53, user1 = 54, user2 = 55, user3 = 56, user4 = 57,
		p = 58,
		max = 59,
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
				case sb: return "SB";
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
				case user0: return "USER0";
				case user1: return "USER1";
				case user2: return "USER2";
				case user3: return "USER3";
				case user4: return "USER4";
				
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

	public static class FutureTokenResult implements Future<TokenResult[]>, AutoCloseable {
		private long _inst;

		public FutureTokenResult(long _inst) {
			this._inst = _inst;
		}

		public boolean cancel(boolean mayInterruptIfRunning) {
			return false;
		}

		public boolean isCancelled() {
			return false;
		}

		public native boolean isDone();

		public native TokenResult[] get();

		public TokenResult[] get(long timeout, TimeUnit unit) {
			// not supported yet
			return get();
		}

		protected void finalize() throws Exception {
			close();
		}

		public native void close() throws Exception;
	}

	public static class MultipleTokenResult implements Iterator<TokenResult[]>, AutoCloseable {
		private long _inst;

		public MultipleTokenResult(long _inst) {
			this._inst = _inst;
		}

		public native boolean hasNext();
		public native TokenResult[] next();

		protected void finalize() throws Exception {
			close();
		}

		public boolean isAlive() {
			return _inst != 0;
		}

		@Override
		public native void close() throws Exception;
	}

	public static class Sentence {
		public String text;
		public int start;
		public int end;
		public Sentence[] subSents; // not supported yet
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
			this(form, tag);
			this.inferRegularity = inferRegularity;
		}

		public JoinableToken(String form, byte tag, boolean inferRegularity, byte space) {
			this(form, tag, inferRegularity);
			this.space = space;
		}

		public JoinableToken(Token token) {
			this.form = token.form;
			this.tag = token.tag;
			this.inferRegularity = false;
			this.space = 0;
		}
	}

	public static class MorphemeSet implements AutoCloseable {
		private long _inst;
		private Kiwi kiwi = null;

		public MorphemeSet(long _inst) {
			this._inst = _inst;
		}

		public MorphemeSet(Kiwi kiwi) {
			this.kiwi = kiwi;
			ctor(kiwi);
		}

		protected void finalize() throws Exception {
			close();
		}

		public boolean isAlive() {
			return _inst != 0;
		}

		private native void ctor(Kiwi kiwi);

		@Override
		public native void close() throws Exception;

		public native int add(String form, byte tag);

		public int add(String form) {
			return add(form, POSTag.unknown);
		}
	}

	public static class BasicToken {
		public String form;
		public int begin = -1, end = -1;
		public byte tag = POSTag.unknown;

		public BasicToken() {
		}

		public BasicToken(String form, int begin, int end) {
			this.form = form;
			this.begin = begin;
			this.end = end;
		}

		public BasicToken(String form, int begin, int end, byte tag) {
			this(form, begin, end);
			this.tag = tag;
		}
	}

	public static class PretokenizedSpan {
		public int begin = 0, end = 0;
		public BasicToken[] tokenization = null;

		public PretokenizedSpan() {
		}

		public PretokenizedSpan(int begin, int end) {
			this.begin = begin;
			this.end = end;
		}

		public PretokenizedSpan(int begin, int end, BasicToken[] tokenization) {
			this(begin, end);
			this.tokenization = tokenization;
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

	public native TokenResult[] analyze(String text, int topN, int matchOption, MorphemeSet blocklist, Iterator<PretokenizedSpan> pretokenized);
	public native FutureTokenResult asyncAnalyze(String text, int topN, int matchOption, MorphemeSet blocklist, Iterator<PretokenizedSpan> pretokenized);
	public native MultipleTokenResult analyze(Iterator<String> texts, int topN, int matchOption, MorphemeSet blocklist, Iterator<Iterator<PretokenizedSpan>> pretokenized);
	public native Sentence[] splitIntoSents(String text, int matchOption, boolean returnTokens);
	public native String join(JoinableToken[] tokens);

	public static native String getVersion();

	public TokenResult[] analyze(String text, int topN, int matchOption, MorphemeSet blocklist) {
		return analyze(text, topN, matchOption, blocklist, null);
	}

	public TokenResult[] analyze(String text, int topN, int matchOption) {
		return analyze(text, topN, matchOption, null);
	}

	public FutureTokenResult asyncAnalyze(String text, int topN, int matchOption, MorphemeSet blocklist) {
		return asyncAnalyze(text, topN, matchOption, blocklist, null);
	}

	public FutureTokenResult asyncAnalyze(String text, int topN, int matchOption) {
		return asyncAnalyze(text, topN, matchOption, null);
	}

	public MultipleTokenResult analyze(Iterator<String> texts, int topN, int matchOption, MorphemeSet blocklist) {
		return analyze(texts, topN, matchOption, blocklist, null);
	}

	public MultipleTokenResult analyze(Iterator<String> texts, int topN, int matchOption) {
		return analyze(texts, topN, matchOption, null);
	}

	public Token[] tokenize(String text, int matchOption, MorphemeSet blocklist, Iterator<PretokenizedSpan> pretokenized) {
		return analyze(text, 1, matchOption, blocklist, pretokenized)[0].tokens;
	}

	public Token[] tokenize(String text, int matchOption, MorphemeSet blocklist) {
		return analyze(text, 1, matchOption, blocklist)[0].tokens;
	}

	public Token[] tokenize(String text, int matchOption) {
		return analyze(text, 1, matchOption)[0].tokens;
	}

	public Sentence[] splitIntoSents(String text, int matchOption) {
		return splitIntoSents(text, matchOption, false);
	}

	public MorphemeSet newMorphemeSet() {
		return new MorphemeSet(this);
	}

	public static void loadLibrary() throws SecurityException, UnsatisfiedLinkError, NullPointerException {
		try {
			System.loadLibrary("KiwiJava-" + _version);
		} catch (UnsatisfiedLinkError e) {
			InputStream in = null;
			String foundName = null;
			for (String name : new String[]{"KiwiJava-" + _version + ".dll", "libKiwiJava-" + _version + ".so", "libKiwiJava-" + _version + ".dylib"}) {
				in = Kiwi.class.getResourceAsStream("/" + name);
				if (in != null) {
					foundName = name;
					break;
				}
			}
			if (in == null) throw new UnsatisfiedLinkError("Cannot find a library named KiwiJava-" + _version);
			byte[] buffer = new byte[4096];
			int read = -1;
			try {
				File temp = File.createTempFile(foundName, "");
				try(FileOutputStream fos = new FileOutputStream(temp)) {
					while((read = in.read(buffer)) != -1) {
						fos.write(buffer, 0, read);
					}
					fos.close();
				} catch(IOException e2) {
					throw new UnsatisfiedLinkError(e2.getMessage());
				}
				in.close();

				System.load(temp.getAbsolutePath());
			} catch(IOException e2) {
				throw new UnsatisfiedLinkError(e2.getMessage());
			}
		}
		
	}

	static {
		loadLibrary();
	}

	public static void main(String[] args) throws Exception {
		if (args.length <= 0) {
			System.out.println(String.format("java -jar kiwi-java-%s.jar <model_path>", getVersion()));
			System.out.println("Error: model_path is not given!");
			return;
		}
		Kiwi kiwi = init(args[0]);
		System.out.println(String.format("Kiwi %s is loaded!", getVersion()));
		try(Scanner input = new Scanner(System.in)) {
			System.out.print(">> ");
			while (input.hasNext()) {
				Token[] tokens = kiwi.tokenize(input.nextLine(), Match.allWithNormalizing);
				System.out.println(Arrays.deepToString(tokens));
				System.out.print(">> ");
			}
		}
	}
}
