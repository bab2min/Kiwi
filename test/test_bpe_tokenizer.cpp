#include <gtest/gtest.h>
#include <kiwi/BpeTokenizer.h>
#include <vector>
#include <string>
#include <set>
#include <sstream>
#include <iostream>

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

