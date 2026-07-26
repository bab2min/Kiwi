#include <gtest/gtest.h>
#include <kiwi/BpeTokenizer.h>
#include <vector>
#include <string>
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
