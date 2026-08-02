#include <gtest/gtest.h>
#include <kiwi/BpeTokenizer.h>
#include <kiwi/Kiwi.h>
#include <vector>
#include <string>
#include <set>
#include <sstream>
#include <tuple>
#include <iostream>
#include "common.h"

using namespace kiwi;

TEST(BpeTokenizerTest, ReadyTest)
{
	BpeTokenizer tok;
	EXPECT_FALSE(tok.ready());
}

TEST(BpeTokenizerTest, BasicTrainingAndEncodeDecode)
{
	BpeTrainerConfig config;
	config.vocabSize = 300;
	config.minPairFrequency = 2;
	config.addPrefixSpace = false;

	BpeTokenizerTrainer trainer(config);

	std::vector<std::string> sentences = {
		"low low low low low",
		"lower lower",
		"newest newest newest newest newest newest",
		"wider wider wider",
		"안녕하세요 안녕하세요 안녕하세요"
	};

	size_t idx = 0;
	size_t added = trainer.addSentences([&]() -> std::string
	{
		if (idx < sentences.size())
		{
			return sentences[idx++];
		}
		return {};
	});

	EXPECT_EQ(added, sentences.size());

	BpeTokenizer tokenizer = trainer.build();
	EXPECT_TRUE(tokenizer.ready());
	EXPECT_GE(tokenizer.getVocab().size(), 256);

	std::string input = u8"lower";
	std::vector<std::pair<uint32_t, uint32_t>> offset;
	auto ids = tokenizer.encode(input, &offset, false);

	EXPECT_FALSE(ids.empty());
	EXPECT_EQ(offset.size(), ids.size());

	std::string decoded = tokenizer.decode(ids);
	EXPECT_EQ(decoded, input);

	// Test decoding with u16string / Korean
	std::string korInput = u8"안녕하세요";
	std::vector<std::pair<uint32_t, uint32_t>> korOffsetByte;
	std::vector<std::pair<uint32_t, uint32_t>> korOffsetChr;

	auto korIds = tokenizer.encode(korInput, &korOffsetByte, false);
	EXPECT_EQ(tokenizer.decode(korIds), korInput);
	EXPECT_EQ(korOffsetByte.size(), korIds.size());

	auto korIds2 = tokenizer.encode(korInput, &korOffsetChr, true);
	EXPECT_EQ(korIds, korIds2);
	EXPECT_EQ(korOffsetChr.size(), korIds.size());
	EXPECT_EQ(korOffsetChr.front().first, 0);
	EXPECT_EQ(korOffsetChr.back().second, 5); // 5 characters in "안녕하세요"
}

TEST(BpeTokenizerTest, U16FeederTest)
{
	BpeTrainerConfig config;
	config.vocabSize = 300;
	config.minPairFrequency = 2;
	config.addPrefixSpace = true;

	BpeTokenizerTrainer trainer(config);

	std::vector<std::u16string> sentences = {
		u"테스트 문장입니다.",
		u"테스트 문장입니다.",
		u"BPE 토크나이저 트레이너 테스트"
	};

	size_t idx = 0;
	size_t added = trainer.addSentences([&]() -> std::u16string 
	{
		if (idx < sentences.size())
		{
			return sentences[idx++];
		}
		return {};
	});

	EXPECT_EQ(added, sentences.size());

	BpeTokenizer tokenizer = trainer.build();
	EXPECT_TRUE(tokenizer.ready());

	std::string testStr = u8"테스트 문장입니다.";
	auto ids = tokenizer.encode(testStr);
	EXPECT_EQ(tokenizer.decode(ids), " " + testStr); // addPrefixSpace prepends space
}

namespace
{
	std::set<std::string> trainVocab(const BpeTrainerConfig& config, const Kiwi* kiwi,
		const std::vector<std::string>& sentences)
	{
		BpeTokenizerTrainer trainer(config, kiwi);
		size_t idx = 0;
		trainer.addSentences([&]() -> std::string
		{
			return idx < sentences.size() ? sentences[idx++] : std::string{};
		});
		auto vocab = trainer.build().getVocab();
		return std::set<std::string>(vocab.begin(), vocab.end());
	}
}

// The morpheme-aware pre-tokenizer must cut the stem/ending seam, so no merge may
// ever span it — while a run of consecutive endings stays in one chunk.
TEST(BpeTokenizerTest, PretokenizeSplitsAtMorphemeBoundary)
{
	Kiwi kiwi = KiwiBuilder(MODEL_PATH).build();

	BpeTrainerConfig config;
	config.vocabSize = 400;
	config.minPairFrequency = 2;

	// Only the first eojeol starts a chunk on its own; the rest keep the space that
	// precedes them, hence the leading blanks in the expected tokens below.
	const std::vector<std::string> sentences(8, u8"사람이 사람을 먹었다 먹겠다");

	// Baseline: without pre-tokenization, whole eojeols are merged.
	const auto plain = trainVocab(config, nullptr, sentences);
	EXPECT_TRUE(plain.count(u8"사람이"));
	EXPECT_TRUE(plain.count(u8" 사람을"));
	EXPECT_TRUE(plain.count(u8" 먹었다"));

	// 조사만 분리
	config.pretokenizeOption = PretokenizeOption::jClass;
	const auto jSplit = trainVocab(config, &kiwi, sentences);
	EXPECT_TRUE(jSplit.count(u8"사람"));
	EXPECT_FALSE(jSplit.count(u8"사람이"));
	EXPECT_FALSE(jSplit.count(u8" 사람을"));
	EXPECT_TRUE(jSplit.count(u8" 먹었다")); // 어미는 분리하지 않음

	// 어미만 분리
	config.pretokenizeOption = PretokenizeOption::eClass;
	const auto eSplit = trainVocab(config, &kiwi, sentences);
	EXPECT_FALSE(eSplit.count(u8" 먹었다"));
	EXPECT_FALSE(eSplit.count(u8" 먹겠다"));
	EXPECT_TRUE(eSplit.count(u8"었다")); // 연속한 어미는 한 덩어리로 유지
	EXPECT_TRUE(eSplit.count(u8"겠다"));
	EXPECT_TRUE(eSplit.count(u8"사람이")); // 조사는 분리하지 않음
}

// Where an eojeol continues past its ending, the end of the run is a boundary too.
TEST(BpeTokenizerTest, PretokenizeSplitsAtEndOfMorphemeRun)
{
	Kiwi kiwi = KiwiBuilder(MODEL_PATH).build();

	BpeTrainerConfig config;
	config.vocabSize = 400;
	config.minPairFrequency = 2;

	const std::vector<std::string> sentences(8, u8"책을읽다");

	const auto plain = trainVocab(config, nullptr, sentences);
	EXPECT_TRUE(plain.count(u8"책을읽다"));

	config.pretokenizeOption = PretokenizeOption::jClass;
	const auto jSplit = trainVocab(config, &kiwi, sentences);
	EXPECT_TRUE(jSplit.count(u8"읽다"));  // 조사 뒤가 다음 청크로 넘어감
	EXPECT_FALSE(jSplit.count(u8"을읽")); // 조사와 뒤따르는 어간이 붙지 않음
	EXPECT_FALSE(jSplit.count(u8"책을"));
}

namespace
{
	// Longest run of consecutive ASCII digits anywhere in `s`.
	size_t longestDigitRun(const std::string& s)
	{
		size_t best = 0, cur = 0;
		for (char c : s)
		{
			cur = ('0' <= c && c <= '9') ? cur + 1 : 0;
			best = std::max(best, cur);
		}
		return best;
	}
}

TEST(BpeTokenizerTest, CapsDigitRunsAtMaxDigitLength)
{
	BpeTrainerConfig config;
	config.vocabSize = 400;
	config.minPairFrequency = 2;

	const std::vector<std::string> sentences(8, "12345 67890");

	// Without a cap, each whole digit run becomes a token of its own.
	const auto plain = trainVocab(config, nullptr, sentences);
	EXPECT_TRUE(plain.count("12345"));
	EXPECT_TRUE(plain.count(" 67890"));

	config.maxDigitLength = 3;
	const auto capped = trainVocab(config, nullptr, sentences);
	EXPECT_TRUE(capped.count("123"));
	EXPECT_TRUE(capped.count("45"));
	EXPECT_TRUE(capped.count(" 678")); // 선행 공백은 자릿수에 포함되지 않음
	EXPECT_TRUE(capped.count("90"));

	// The cap holds for the whole vocabulary, not just the tokens checked above.
	for (const auto& token : capped)
		EXPECT_LE(longestDigitRun(token), config.maxDigitLength) << "token: " << token;
}

// A pinned alphabet entry must be rebuilt from its bytes before any learned merge
// applies, so it can never be encoded as a split-up byte sequence.
TEST(BpeTokenizerTest, PinsAdditionalAlphabet)
{
	BpeTrainerConfig config;
	config.vocabSize = 400;
	config.minPairFrequency = 2;
	config.additionalAlphabet = { u8"ㄱ", u8"ㄲ", u8"ㅏ" };

	// The jamo appear once each, far below minPairFrequency, so nothing here would be
	// learned from the corpus: only the pinning can keep them whole.
	std::vector<std::string> sentences(8, u8"가나다 라마바");
	sentences.push_back(u8"ㄱㄲㅏ");

	BpeTokenizerTrainer trainer(config);
	size_t idx = 0;
	trainer.addSentences([&]() -> std::string
	{
		return idx < sentences.size() ? sentences[idx++] : std::string{};
	});
	BpeTokenizer tokenizer = trainer.build();

	const auto& vocab = tokenizer.getVocab();
	std::set<std::string> vocabSet(vocab.begin(), vocab.end());
	for (const auto& entry : config.additionalAlphabet)
		EXPECT_TRUE(vocabSet.count(entry)) << "missing pinned entry: " << entry;

	// "ㄱ"(E3 84 B1) and "ㄲ"(E3 84 B2) share the intermediate token E3 84; "ㅏ"(E3 85 8F)
	// brings its own.  Pinning the three therefore costs five tokens, not six.
	const std::string prefix8384 = { (char)0xE3, (char)0x84 };
	const std::string prefix8385 = { (char)0xE3, (char)0x85 };
	EXPECT_TRUE(vocabSet.count(prefix8384));
	EXPECT_TRUE(vocabSet.count(prefix8385));

	// Sized so that the merge loop has no room left, the vocabulary is exactly the
	// byte alphabet plus those five.
	BpeTrainerConfig exactConfig = config;
	exactConfig.vocabSize = 261;
	BpeTokenizerTrainer exactTrainer(exactConfig);
	idx = 0;
	exactTrainer.addSentences([&]() -> std::string
	{
		return idx < sentences.size() ? sentences[idx++] : std::string{};
	});
	EXPECT_EQ(exactTrainer.build().getVocab().size(), 261u);

	// Each jamo encodes to exactly one id, and round-trips.
	for (const auto& entry : config.additionalAlphabet)
	{
		const auto ids = tokenizer.encode(entry);
		EXPECT_EQ(ids.size(), 1u) << "entry was split: " << entry;
		EXPECT_EQ(tokenizer.decode(ids), entry);
	}

	// Pinned merges live in the saved merge list, so a reloaded model behaves the same.
	std::stringstream buffer;
	tokenizer.save(buffer);
	buffer.seekg(0);
	BpeTokenizer reloaded = BpeTokenizer::load(buffer);
	EXPECT_EQ(reloaded.encode(u8"ㄱㄲㅏ"), tokenizer.encode(u8"ㄱㄲㅏ"));
	EXPECT_EQ(reloaded.encode(u8"ㄱㄲㅏ").size(), 3u);
}

// useJamoAlphabet trains on decomposed Hangul and pins the conjoining jamo, so an
// unseen syllable falls back to jamo instead of to raw bytes.
TEST(BpeTokenizerTest, UseJamoAlphabetDecomposesAndPinsJamo)
{
	BpeTrainerConfig config;
	config.vocabSize = 500;
	config.minPairFrequency = 2;
	config.useJamoAlphabet = true;

	const std::vector<std::string> sentences(8, u8"한글 자모 학습");
	const auto vocab = trainVocab(config, nullptr, sentences);

	// All 67 conjoining jamo are present, whether or not the corpus used them.
	size_t jamoPresent = 0;
	std::vector<std::pair<char32_t, size_t>> ranges = { {0x1100, 19}, {0x1161, 21}, {0x11A8, 27} };
	for (const auto& r : ranges)
		for (size_t i = 0; i < r.second; ++i)
			if (vocab.count(utf8FromCode(r.first + (char32_t)i))) ++jamoPresent;
	EXPECT_EQ(jamoPresent, 67u);

	// Nothing is counted in precomposed form any more, so no token may hold a whole
	// syllable.  The byte alphabet still carries EA..ED on their own, which is why this
	// decodes rather than just scanning for lead bytes.
	auto holdsPrecomposedSyllable = [](const std::string& t)
	{
		for (size_t i = 0; i + 2 < t.size(); ++i)
		{
			const auto b0 = (unsigned char)t[i], b1 = (unsigned char)t[i + 1], b2 = (unsigned char)t[i + 2];
			if (b0 < 0xEA || b0 > 0xED) continue;
			if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) continue;
			const char32_t c = ((char32_t)(b0 & 0x0F) << 12) | ((char32_t)(b1 & 0x3F) << 6) | (b2 & 0x3F);
			if (0xAC00 <= c && c <= 0xD7A3) return true;
		}
		return false;
	};
	for (const auto& token : vocab)
		EXPECT_FALSE(holdsPrecomposedSyllable(token)) << "token holds a precomposed syllable: " << token;

	// "학" decomposes to U+1112 U+1161 U+11A8, and each of those is its own token, so
	// the syllable is reachable as three ids even though it was never seen as bytes.
	EXPECT_TRUE(vocab.count(utf8FromCode(0x1112)));
	EXPECT_TRUE(vocab.count(utf8FromCode(0x1161)));
	EXPECT_TRUE(vocab.count(utf8FromCode(0x11A8)));

	// Sized so the merge loop has no room: the byte alphabet plus 67 jamo and the four
	// two-byte prefixes (E1 84/85/86/87) they share.
	BpeTrainerConfig exactConfig = config;
	exactConfig.vocabSize = 256 + 71;
	EXPECT_EQ(trainVocab(exactConfig, nullptr, sentences).size(), 256u + 71u);
}

// With jamo the boundary can fall inside a syllable, which byte spans cannot express:
// an ending that is nothing but a coda (ᆫ of "그건", ᆯ of "할") separates from its stem.
TEST(BpeTokenizerTest, SplitsCodaMorphemeUnderJamoAlphabet)
{
	Kiwi kiwi = KiwiBuilder(MODEL_PATH).build();

	auto jamo = [](std::initializer_list<char32_t> codes)
	{
		std::string s;
		for (auto c : codes) s += utf8FromCode(c);
		return s;
	};
	const std::string geugeo  = jamo({ 0x1100, 0x1173, 0x1100, 0x1165 });          // 그거
	const std::string geugeon = jamo({ 0x1100, 0x1173, 0x1100, 0x1165, 0x11AB });  // 그건
	const std::string ha      = jamo({ 0x1112, 0x1161 });                          // 하
	const std::string hal     = jamo({ 0x1112, 0x1161, 0x11AF });                  // 할

	BpeTrainerConfig config;
	config.vocabSize = 1000;
	config.minPairFrequency = 2;
	config.useJamoAlphabet = true;

	const std::vector<std::string> sentences(8, u8"그건 할 일");

	// Decomposition alone changes nothing about where the eojeol is cut.
	const auto plain = trainVocab(config, nullptr, sentences);
	EXPECT_TRUE(plain.count(geugeon));
	EXPECT_TRUE(plain.count(" " + hal));

	// 그건 = 그거/NP + ᆫ/JX, 할 = 하/VV + ᆯ/ETM: both seams sit inside a syllable.
	config.pretokenizeOption = PretokenizeOption::jClass | PretokenizeOption::eClass;
	const auto split = trainVocab(config, &kiwi, sentences);
	EXPECT_FALSE(split.count(geugeon));
	EXPECT_TRUE(split.count(geugeo));
	EXPECT_FALSE(split.count(" " + hal));
	EXPECT_TRUE(split.count(" " + ha));

	// The same corpus without jamo cannot express either seam, so both stay whole.
	BpeTrainerConfig byteConfig = config;
	byteConfig.useJamoAlphabet = false;
	const auto byteSplit = trainVocab(byteConfig, &kiwi, sentences);
	EXPECT_TRUE(byteSplit.count(u8"그건"));
	EXPECT_TRUE(byteSplit.count(u8" 할"));
}

// A contraction makes two morphemes share a syllable, so the coda boundary — placed
// from the current token's own position — lands behind one placed from the previous
// token's end.  The boundaries feed a left-to-right cursor, and an out-of-order one
// used to underflow the span length and read far outside the text.
TEST(BpeTokenizerTest, KeepsBoundariesOrderedAcrossContractedSyllables)
{
	Kiwi kiwi = KiwiBuilder(MODEL_PATH).build();

	auto jamo = [](std::initializer_list<char32_t> codes)
	{
		std::string s;
		for (auto c : codes) s += utf8FromCode(c);
		return s;
	};

	BpeTrainerConfig config;
	config.vocabSize = 1000;
	config.minPairFrequency = 2;
	config.useJamoAlphabet = true;
	config.pretokenizeOption = PretokenizeOption::all;

	// 합니다 = 하/VV(p,1) + ᆸ니다/EF(p,3): 하's end names the far side of 합 while the
	// seam belongs before its ᆸ.  Z_CODA in "…엄" reaches the same shape from an
	// ending run instead.
	const std::vector<std::string> sentences(8, u8"이렇게 합니다 그러엄");
	const auto vocab = trainVocab(config, &kiwi, sentences);

	// 합 is cut open: 하 keeps the stem chunk and ᆸ leads the ending, so no token may
	// hold 합 whole, and none may hold ᆸ alone as the tail of a longer chunk either.
	const std::string ha  = jamo({ 0x1112, 0x1161 });          // 하
	const std::string hap = jamo({ 0x1112, 0x1161, 0x11B8 });  // 합
	EXPECT_TRUE(vocab.count(" " + ha));
	EXPECT_FALSE(vocab.count(" " + hap));
	EXPECT_TRUE(vocab.count(jamo({ 0x11B8, 0x1102, 0x1175, 0x1103, 0x1161 }))); // ᆸ니다

	// Every chunk that reached the counter was a real substring, so decoding the whole
	// vocabulary must stay within the corpus alphabet rather than showing wild bytes.
	for (const auto& token : vocab)
		EXPECT_LE(token.size(), (size_t)64) << "implausibly long token, span arithmetic slipped";
}

TEST(BpeTokenizerTest, RejectsEmptyAdditionalAlphabetEntry)
{
	BpeTrainerConfig config;
	config.vocabSize = 300;
	config.additionalAlphabet = { "" };
	EXPECT_THROW(BpeTokenizerTrainer{ config }, std::invalid_argument);
}

TEST(BpeTokenizerTest, RetainsPairsWhoseFrequencyDecreases)
{
	BpeTrainerConfig config;
	config.vocabSize = 258;
	config.minPairFrequency = 5;

	BpeTokenizerTrainer trainer(config);
	std::vector<std::string> sentences;
	sentences.insert(sentences.end(), 10, "ab");
	sentences.push_back("abcd");
	sentences.insert(sentences.end(), 5, "bc");

	size_t index = 0;
	trainer.addSentences([&]() -> std::string {
		return index < sentences.size() ? sentences[index++] : "";
	});

	// "ab" is merged first (11 occurrences).  This reduces "bc" from 6 to 5,
	// which must still leave it eligible for the second merge.
	EXPECT_EQ(trainer.build().getVocab().size(), 258);
}

TEST(BpeTokenizerTest, ValidatesTrainerConfiguration)
{
	BpeTrainerConfig config;
	config.vocabSize = 255;
	EXPECT_THROW(BpeTokenizerTrainer{ config }, std::invalid_argument);

	config.vocabSize = 256;
	config.maxTokenLength = 0;
	EXPECT_THROW(BpeTokenizerTrainer{ config }, std::invalid_argument);

	config.maxTokenLength = std::numeric_limits<size_t>::max();
	config.batchSize = 0;
	EXPECT_THROW(BpeTokenizerTrainer{ config }, std::invalid_argument);
}

TEST(BpeTokenizerTest, DoesNotJoinUnicodePunctuationWithLetters)
{
	BpeTrainerConfig config;
	config.vocabSize = 259;
	config.minPairFrequency = 2;

	BpeTokenizerTrainer trainer(config);
	std::vector<std::string> sentences = { "a—b", "a—b" };
	size_t index = 0;
	trainer.addSentences([&]() -> std::string 
	{
		return index < sentences.size() ? sentences[index++] : "";
	});

	// The three UTF-8 bytes of the em dash can merge, but the punctuation must
	// remain a separate pre-tokenization chunk from the surrounding letters.
	EXPECT_EQ(trainer.build().getVocab().size(), 258);
}

TEST(BpeTokenizerTest, ParallelCollectionMatchesSingleThreadedCollection)
{
	std::vector<std::string> sentences = {
		"low lower lowest", "new newer newest", "안녕하세요, Kiwi!",
		"low lower lowest", "new newer newest", "안녕하세요, Kiwi!",
		"BPE trainers should be deterministic.", "BPE trainers should be deterministic."
	};

	BpeTrainerConfig singleConfig;
	singleConfig.vocabSize = 300;
	singleConfig.minPairFrequency = 2;
	singleConfig.numThreads = 1;
	singleConfig.batchSize = 2;
	BpeTokenizerTrainer singleTrainer(singleConfig);

	BpeTrainerConfig parallelConfig = singleConfig;
	parallelConfig.numThreads = 3;
	parallelConfig.batchSize = 3;
	BpeTokenizerTrainer parallelTrainer(parallelConfig);

	auto addAll = [&](BpeTokenizerTrainer& trainer)
	{
		size_t index = 0;
		return trainer.addSentences([&]() -> std::string {
			return index < sentences.size() ? sentences[index++] : "";
		});
	};

	EXPECT_EQ(addAll(singleTrainer), sentences.size());
	EXPECT_EQ(addAll(parallelTrainer), sentences.size());

	auto singleTokenizer = singleTrainer.build();
	auto parallelTokenizer = parallelTrainer.build();
	EXPECT_EQ(parallelTokenizer.getVocab(), singleTokenizer.getVocab());
	EXPECT_EQ(parallelTokenizer.encode("안녕하세요, newer Kiwi!"),
		singleTokenizer.encode("안녕하세요, newer Kiwi!"));
}

TEST(BpeTokenizerTest, StopsCallingFeederAfterEndOfInput)
{
	// An empty string is the end-of-input sentinel; the feeder must never be polled
	// past it, whatever the input length is relative to batchSize.
	for (size_t count : { size_t(1), size_t(3), size_t(4), size_t(5), size_t(9) })
	{
		BpeTrainerConfig config;
		config.vocabSize = 300;
		config.minPairFrequency = 1;
		config.numThreads = 1;
		config.batchSize = 4;
		BpeTokenizerTrainer trainer(config);

		size_t index = 0, calls = 0;
		const size_t added = trainer.addSentences([&]() -> std::string {
			++calls;
			return index < count ? "low lower lowest " + std::to_string(index++) : "";
		});

		EXPECT_EQ(added, count) << "count=" << count;
		EXPECT_EQ(calls, count + 1) << "count=" << count;
	}
}

TEST(BpeTokenizerTest, CountsChunksContainingNulBytes)
{
	// Chunk keys must carry an explicit length: terminating them with NUL would
	// truncate "@\0@@@" to "@", leaving no pair to count and no merge to learn.
	BpeTrainerConfig config;
	config.vocabSize = 300;
	config.minPairFrequency = 1;
	config.numThreads = 1;
	BpeTokenizerTrainer trainer(config);

	const std::string longChunk("@\0@@@", 5), shortChunk("@\0@", 3);
	size_t index = 0;
	trainer.addSentences([&]() -> std::string {
		if (index >= 40) return {};
		return (index++ % 2) ? longChunk : shortChunk;
	});

	auto tokenizer = trainer.build();
	const auto& vocab = tokenizer.getVocab();
	ASSERT_GT(vocab.size(), 256u);

	bool spansNul = false;
	for (size_t i = 256; i < vocab.size(); ++i)
		if (vocab[i].find('\0') != std::string::npos) spansNul = true;
	EXPECT_TRUE(spansNul);

	EXPECT_EQ(tokenizer.decode(tokenizer.encode(longChunk)), longChunk);
	EXPECT_EQ(tokenizer.decode(tokenizer.encode(shortChunk)), shortChunk);
}

TEST(BpeTokenizerTest, RespectsMaxTokenLengthAndKeepsVocabUnique)
{
	std::vector<std::string> sentences;
	for (int i = 0; i < 40; ++i)
	{
		sentences.push_back("low lower lowest newest widest");
		sentences.push_back("안녕하세요 감사합니다 테스트 abc123");
	}

	for (size_t maxTokenLength : { size_t(2), size_t(3), size_t(4), size_t(6) })
	{
		BpeTrainerConfig config;
		config.vocabSize = 600;
		config.minPairFrequency = 2;
		config.maxTokenLength = maxTokenLength;
		config.numThreads = 1;
		BpeTokenizerTrainer trainer(config);

		size_t index = 0;
		trainer.addSentences([&]() -> std::string {
			return index < sentences.size() ? sentences[index++] : "";
		});

		auto tokenizer = trainer.build();
		const auto& vocab = tokenizer.getVocab();

		std::set<std::string> distinct;
		for (size_t i = 0; i < vocab.size(); ++i)
		{
			if (i >= 256)
				EXPECT_LE(vocab[i].size(), maxTokenLength) << "maxTokenLength=" << maxTokenLength;
			// save() keys the vocab JSON by token string, so duplicate entries would
			// collapse on write and make the emitted file unloadable.
			EXPECT_TRUE(distinct.insert(vocab[i]).second)
				<< "duplicate vocab entry at id " << i;
		}

		const std::string input = "안녕하세요 newest lower abc123";
		EXPECT_EQ(tokenizer.decode(tokenizer.encode(input)), input);
	}
}

TEST(BpeTokenizerTest, SavedModelReloadsIdentically)
{
	BpeTrainerConfig config;
	config.vocabSize = 500;
	config.minPairFrequency = 2;
	config.maxTokenLength = 5;
	BpeTokenizerTrainer trainer(config);

	std::vector<std::string> sentences;
	for (int i = 0; i < 60; ++i)
	{
		sentences.push_back("low lower lowest newest widest hello world");
		sentences.push_back("안녕하세요 감사합니다 테스트 abc123");
	}
	size_t index = 0;
	trainer.addSentences([&]() -> std::string {
		return index < sentences.size() ? sentences[index++] : "";
	});

	auto tokenizer = trainer.build();
	std::stringstream stream;
	tokenizer.save(stream);
	stream.seekg(0);
	auto reloaded = BpeTokenizer::load(stream);

	EXPECT_EQ(reloaded.getVocab(), tokenizer.getVocab());
	const std::string input = "안녕하세요 newest lower abc123 hello world";
	EXPECT_EQ(reloaded.encode(input), tokenizer.encode(input));
}

TEST(BpeTokenizerTest, PreTokenizationIsLosslessForAwkwardInputs)
{
	BpeTrainerConfig config;
	config.vocabSize = 256;              // no merges: one token per byte
	config.minPairFrequency = 1000000;
	BpeTokenizerTrainer trainer(config);

	size_t index = 0;
	trainer.addSentences([&]() -> std::string { return index++ ? "" : "x"; });
	auto tokenizer = trainer.build();

	const std::vector<std::string> inputs = {
		"hello world", "  double  spaces  ", "trailing   ", "\t\n mixed   spaces 　end",
		"don't can't we're I've they'll it's", "'s at start", "''s doubled quote",
		"123 456.789 abc123", "안녕하세요, 반갑습니다! 123", "!!!???...", " ", "  ",
		std::string("\xff\xfe invalid utf8"),
	};
	for (const auto& input : inputs)
		EXPECT_EQ(tokenizer.decode(tokenizer.encode(input)), input) << "input=[" << input << "]";
}

TEST(BpeTokenizerTest, ReportsProgressThroughEventCallback)
{
	using Event = BpeTokenizerTrainerEvent;
	std::vector<std::tuple<Event, size_t, size_t>> events;

	BpeTrainerConfig config;
	config.vocabSize = 400;
	config.minPairFrequency = 2;
	config.batchSize = 4;

	BpeTokenizerTrainer trainer(config, nullptr, [&](Event event, size_t current, size_t total)
	{
		events.emplace_back(event, current, total);
	});

	const std::vector<std::string> sentences = {
		"low low low low low", "lower lower lower", "newest newest newest newest",
		"widest widest widest", "안녕하세요 안녕하세요 안녕하세요", "감사합니다 감사합니다",
		"hello world hello world", "abc123 abc123 abc123", "테스트 테스트 테스트",
		"low newest widest lower",
	};
	size_t index = 0;
	trainer.addSentences([&]() -> std::string
	{
		return index < sentences.size() ? sentences[index++] : std::string{};
	});

	// addSentences emits begin, one progress per batch (10 sentences / 4), then end.
	ASSERT_EQ(events.size(), 5u);
	EXPECT_EQ(events[0], std::make_tuple(Event::pretokenizeBegin, 0u, 0u));
	EXPECT_EQ(events[1], std::make_tuple(Event::pretokenizeProgress, 4u, 0u));
	EXPECT_EQ(events[2], std::make_tuple(Event::pretokenizeProgress, 8u, 0u));
	EXPECT_EQ(events[3], std::make_tuple(Event::pretokenizeProgress, 10u, 0u));
	EXPECT_EQ(events[4], std::make_tuple(Event::pretokenizeEnd, 10u, 10u));

	events.clear();
	auto tokenizer = trainer.build();

	ASSERT_GE(events.size(), 2u);
	EXPECT_EQ(std::get<0>(events.front()), Event::mergeBegin);
	EXPECT_EQ(std::get<1>(events.front()), 256u);          // the byte alphabet
	EXPECT_EQ(std::get<0>(events.back()), Event::mergeEnd);
	EXPECT_EQ(std::get<1>(events.back()), tokenizer.getVocab().size());
	EXPECT_EQ(std::get<2>(events.back()), tokenizer.getVocab().size());

	size_t previous = 0;
	for (size_t i = 0; i + 1 < events.size(); ++i)
	{
		if (i) EXPECT_EQ(std::get<0>(events[i]), Event::mergeProgress);
		EXPECT_EQ(std::get<2>(events[i]), config.vocabSize);   // total is the target
		EXPECT_GE(std::get<1>(events[i]), previous);           // current never regresses
		EXPECT_LE(std::get<1>(events[i]), config.vocabSize);
		previous = std::get<1>(events[i]);
	}
}

TEST(BpeTokenizerTest, ReportsIndeterminateTotalWhenVocabSizeIsUnbounded)
{
	using Event = BpeTokenizerTrainerEvent;
	std::vector<std::tuple<Event, size_t, size_t>> events;

	BpeTrainerConfig config;
	config.vocabSize = 0;               // unbounded: merge until pairs run out
	config.minPairFrequency = 2;

	BpeTokenizerTrainer trainer(config, nullptr, [&](Event event, size_t current, size_t total)
	{
		events.emplace_back(event, current, total);
	});

	const std::vector<std::string> sentences = { "low lower newest widest", "low lower newest widest" };
	size_t index = 0;
	trainer.addSentences([&]() -> std::string
	{
		return index < sentences.size() ? sentences[index++] : std::string{};
	});
	events.clear();
	auto tokenizer = trainer.build();

	ASSERT_GE(events.size(), 2u);
	EXPECT_EQ(std::get<0>(events.front()), Event::mergeBegin);
	EXPECT_EQ(std::get<2>(events.front()), 0u);   // zero means "indeterminate"
	EXPECT_EQ(std::get<0>(events.back()), Event::mergeEnd);
	EXPECT_EQ(std::get<1>(events.back()), tokenizer.getVocab().size());
}
