#include "gtest/gtest.h"
#include <kiwi/Kiwi.h>
#include <kiwi/HSDataset.h>
#include "common.h"

class TestInitializer
{
public:
	TestInitializer()
	{
#ifdef _MSC_VER
		SetConsoleOutputCP(CP_UTF8);
#endif
	}
};

TestInitializer _global_initializer;

using namespace kiwi;

inline testing::AssertionResult testTokenization(Kiwi& kiwi, const std::u16string& s)
{
	auto tokens = kiwi.analyze(s, Match::all).first;
	if (tokens.empty()) return testing::AssertionFailure() << "kiwi.analyze(" << testing::PrintToString(s) << ") yields an empty result.";
	if (tokens.back().position + tokens.back().length == s.size())
	{
		return testing::AssertionSuccess();
	}
	else
	{
		return testing::AssertionFailure() << "the result of kiwi.analyze(" << testing::PrintToString(s) << ") ends at " << (tokens.back().position + tokens.back().length);
	}
}

Kiwi& reuseKiwiInstance()
{
	static Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, }.build();
	return kiwi;
}


TEST(KiwiCpp, InitClose)
{
	Kiwi& kiwi = reuseKiwiInstance();
}

TEST(KiwiCpp, EmptyResult)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto testCases = {
		u"보일덱BD2",
		u"5스트릿/7스트릿",
		u"며",
		u"\"오쿠\"(",
		u"키미토나라바킷토ah",
		u"제이플래닛2005년생위주6인조걸그룹",
		u"당장 유튜브에서 '페ㅌ",
		u"스쿠비쿨로",
		u"키블러",
		u"포뮬러",
		u"오리쿨로",
		u"만들어졌다\" 며 여전히 냉정하게 반응한다.",
		u"통과했며",
		u"우걱우걱\"",
		u"네오 플래닛S",
		u"YJ 뭐위 웨이촹GTS",
		u"쮸쮸\"",
		u"스틸블루",
		u"15살이었므로",
		u"타란튤라",
	};
	for (auto s : testCases)
	{
		EXPECT_TRUE(testTokenization(kiwi, s));
	}
}

TEST(KiwiCpp, SingleResult)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto testCases = {
		u"발신광고갑자기연락드려죄송합니다국내규모가장큰세력이며만구독자를보유하고있고주식유튜버입니다무조건큰돈버는세력",
	};
	for (auto s : testCases)
	{
		auto res = kiwi.analyze(s, Match::allWithNormalizing);
		EXPECT_GT(res.first.size(), 1);
	}
}

TEST(KiwiCpp, HSDataset)
{
	KiwiBuilder kw{ MODEL_PATH, 0, BuildOption::default_, };
	std::vector<std::string> data;
	data.emplace_back(MODEL_PATH "/w_email.txt");

	static constexpr size_t batchSize = 32, windowSize = 8;

	std::array<int32_t, batchSize* windowSize> in;
	std::array<int32_t, batchSize> out;
	std::array<float, batchSize> lmLProbs;
	std::array<uint32_t, batchSize> outNgramBase;

	for (size_t w : {1, 2, 4})
	{
		//std::cout << w << std::endl;
		auto dataset = kw.makeHSDataset(data, batchSize, windowSize, w, 0.);
		for (size_t i = 0; i < 2; ++i)
		{
			size_t totalBatchCnt = 0, totalTokenCnt = 0, s;
			dataset.reset();
			while (s = dataset.next(in.data(), out.data(), lmLProbs.data(), outNgramBase.data()))
			{
				EXPECT_LE(s, batchSize);
				totalTokenCnt += s;
				totalBatchCnt++;
			}
			EXPECT_TRUE(std::max(dataset.numEstimBatches(), (size_t)w) - w <= totalBatchCnt && totalBatchCnt <= dataset.numEstimBatches() + w);
			EXPECT_EQ(dataset.numTokens(), totalTokenCnt);
		}
	}

	HSDataset trainset, devset;
	trainset = kw.makeHSDataset(data, batchSize, windowSize, 1, 0., {}, 0.1, &devset);
	for (size_t i = 0; i < 2; ++i)
	{
		{
			size_t totalBatchCnt = 0, totalTokenCnt = 0, s;
			trainset.reset();
			while (s = trainset.next(in.data(), out.data(), lmLProbs.data(), outNgramBase.data()))
			{
				EXPECT_LE(s, batchSize);
				totalTokenCnt += s;
				totalBatchCnt++;
			}
			EXPECT_TRUE(std::max(trainset.numEstimBatches(), (size_t)1) - 1 <= totalBatchCnt && totalBatchCnt <= trainset.numEstimBatches() + 1);
			EXPECT_EQ(trainset.numTokens(), totalTokenCnt);
		}
		{
			size_t totalBatchCnt = 0, totalTokenCnt = 0, s;
			devset.reset();
			while (s = devset.next(in.data(), out.data(), lmLProbs.data(), outNgramBase.data()))
			{
				EXPECT_LE(s, batchSize);
				totalTokenCnt += s;
				totalBatchCnt++;
			}
			EXPECT_TRUE(std::max(devset.numEstimBatches(), (size_t)1) - 1 <= totalBatchCnt && totalBatchCnt <= devset.numEstimBatches() + 1);
			EXPECT_EQ(devset.numTokens(), totalTokenCnt);
		}
	}
}

TEST(KiwiCpp, SentenceBoundaryErrors)
{
	Kiwi& kiwi = reuseKiwiInstance();

	for (auto str : {
		u8"실패할까봐",
		u8"집에 갈까 봐요",
		u8"너무 낮지 싶어요",
		u8"계속 할까 싶다",
		})
	{
		TokenResult res;
		std::vector<std::pair<size_t, size_t>> sentRanges = kiwi.splitIntoSents(str, Match::allWithNormalizing, &res);
		EXPECT_EQ(sentRanges.size(), 1);
	}
}

TEST(KiwiCpp, SplitByPolarity)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto ret = kiwi.analyze(u"흘렀다", Match::all);
	EXPECT_EQ(ret.first.size(), 3);
	ret = kiwi.analyze(u"전류가 흘렀다", Match::all);
	EXPECT_EQ(ret.first.size(), 5);
	ret = kiwi.analyze(u"전류가흘렀다", Match::all);
	EXPECT_EQ(ret.first.size(), 5);
}

TEST(KiwiCpp, SpaceBetweenChunk)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto ret = kiwi.analyze(u"다 갔다.", Match::allWithNormalizing);
	EXPECT_EQ(ret.first[0].str, u"다");
}

TEST(KiwiCpp, SpaceTolerant)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto str = u"띄 어 쓰 기 문 제 가 있 습 니 다";
	auto tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_GE(tokens.size(), 11);

	kiwi.setSpaceTolerance(1);
	kiwi.setSpacePenalty(3);
	tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_EQ(tokens.size(), 9);

	kiwi.setSpaceTolerance(2);
	tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_EQ(tokens.size(), 8);

	kiwi.setSpaceTolerance(3);
	tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_EQ(tokens.size(), 5);

	kiwi.setSpaceTolerance(0);
	kiwi.setSpacePenalty(8);
}

TEST(KiwiCpp, Pattern)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto tokens = kiwi.analyze(u"123.4567", Match::none).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123.4567.", Match::none).first;
	EXPECT_EQ(tokens.size(), 4);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);
	EXPECT_EQ(tokens[1].tag, POSTag::sf);

	tokens = kiwi.analyze(u"123.", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);
	EXPECT_EQ(tokens[1].tag, POSTag::sf);

	tokens = kiwi.analyze(u"1,234,567", Match::none).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123,", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);
	EXPECT_EQ(tokens[1].tag, POSTag::sp);

	tokens = kiwi.analyze(u"123,456.789", Match::none).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123,456.789hz", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123,456.789이다", Match::none).first;
	EXPECT_EQ(tokens.size(), 3);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"1.2%", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"12:34에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"12:3:456:7890에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"12.34에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"12.34.0.1에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"2001/01/02에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"010-1234-5678에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);
}

TEST(KiwiCpp, BuilderAddWords)
{
	KiwiBuilder builder{ MODEL_PATH };
	EXPECT_TRUE(builder.addWord(KWORD, POSTag::nnp, 0.0));
	Kiwi kiwi = builder.build();

	auto res = kiwi.analyze(KWORD, Match::all);
	EXPECT_EQ(res.first[0].str, KWORD);
}

#define TEST_SENT u"이 예쁜 꽃은 독을 품었지만 진짜 아름다움을 가지고 있어요."

TEST(KiwiCpp, AnalyzeWithNone)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::none }.build();
	kiwi.analyze(TEST_SENT, Match::all);
}

TEST(KiwiCpp, AnalyzeWithIntegrateAllomorph)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::integrateAllomorph }.build();
	kiwi.analyze(TEST_SENT, Match::all);
}

TEST(KiwiCpp, AnalyzeWithLoadDefaultDict)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::loadDefaultDict }.build();
	kiwi.analyze(TEST_SENT, Match::all);
}

TEST(KiwiCpp, AnalyzeSBG)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::none, true }.build();
	kiwi.analyze(TEST_SENT, Match::all);
}

TEST(KiwiCpp, AnalyzeMultithread)
{
	auto data = loadTestCorpus();
	std::vector<TokenResult> results;
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 2 }.build();
	size_t idx = 0;
	kiwi.analyze(1, [&]() -> std::u16string
	{
		if (idx >= data.size()) return {};
		return utf8To16(data[idx++]);
	}, [&](std::vector<TokenResult>&& res)
	{
		results.emplace_back(std::move(res[0]));
	}, Match::all);
	EXPECT_EQ(data.size(), results.size());
}

TEST(KiwiCpp, AnalyzeError01)
{
	Kiwi& kiwi = reuseKiwiInstance();
	TokenResult res = kiwi.analyze(u"갔는데", Match::all);
	EXPECT_EQ(res.first[0].str, std::u16string{ u"가" });
	res = kiwi.analyze(u"잤는데", Match::all);
	EXPECT_EQ(res.first[0].str, std::u16string{ u"자" });
}

TEST(KiwiCpp, NormalizeCoda) 
{ 
	Kiwi& kiwi = reuseKiwiInstance(); 
	TokenResult res = kiwi.analyze(u"키윜ㅋㅋ", Match::allWithNormalizing); 
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㅋㅋㅋ" });
	res = kiwi.analyze(u"키윟ㅎ", Match::allWithNormalizing);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㅎㅎ" });
	res = kiwi.analyze(u"키윅ㄱ", Match::allWithNormalizing);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㄱㄱ" });
	res = kiwi.analyze(u"키윈ㄴㄴ", Match::allWithNormalizing);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㄴㄴㄴ" });
	res = kiwi.analyze(u"키윊ㅎㅎ", Match::allWithNormalizing);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㅎㅎ" });
	res = kiwi.analyze(u"키윍ㄱㄱ", Match::allWithNormalizing);
	EXPECT_EQ(res.first.back().str, std::u16string{u"ㄱㄱ"});
} 

TEST(KiwiCpp, AnalyzeWithWordPosition)
{
	std::u16string testSentence = u"나 정말 배불렄ㅋㅋ"; 
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::none }.build();
	TokenResult tokenResult = kiwi.analyze(testSentence, Match::all);
	std::vector<TokenInfo> tokenInfoList = tokenResult.first;

	ASSERT_GE(tokenInfoList.size(), 4);
	EXPECT_EQ(tokenInfoList[0].wordPosition, 0);
	EXPECT_EQ(tokenInfoList[1].wordPosition, 1);
	EXPECT_EQ(tokenInfoList[2].wordPosition, 2);
	EXPECT_EQ(tokenInfoList[3].wordPosition, 2);
}

TEST(KiwiCpp, Issue57_BuilderAddWord)
{
	{
		KiwiBuilder builder{ MODEL_PATH };
		builder.addWord(u"울트라리스크", POSTag::nnp, 3.0);
		builder.addWord(u"파일즈", POSTag::nnp, 0.0);
		Kiwi kiwi = builder.build();
		TokenResult res = kiwi.analyze(u"울트라리스크가 뭐야?", Match::all);
		EXPECT_EQ(res.first[0].str, std::u16string{ u"울트라리스크" });
	}

	{
		KiwiBuilder builder{ MODEL_PATH };
		builder.addWord(u"파일즈", POSTag::nnp, 0.0);
		builder.addWord(u"울트라리스크", POSTag::nnp, 3.0);
		Kiwi kiwi = builder.build();
		TokenResult res = kiwi.analyze(u"울트라리스크가 뭐야?", Match::all);
		EXPECT_EQ(res.first[0].str, std::u16string{ u"울트라리스크" });
	}
}

TEST(KiwiCpp, PositionAndLength)
{
	Kiwi& kiwi = reuseKiwiInstance();

	{
		auto tokens = kiwi.analyze(u"자랑했던", Match::all).first;
		ASSERT_GE(tokens.size(), 4);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 2);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 2);
		EXPECT_EQ(tokens[2].length, 1);
		EXPECT_EQ(tokens[3].position, 3);
		EXPECT_EQ(tokens[3].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"이르렀다", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 2);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 3);
		EXPECT_EQ(tokens[2].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"일렀다", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 1);
		EXPECT_EQ(tokens[1].position, 1);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 2);
		EXPECT_EQ(tokens[2].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"다다랐다", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 3);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 3);
		EXPECT_EQ(tokens[2].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"바다다!", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 2);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 0);
		EXPECT_EQ(tokens[2].position, 2);
		EXPECT_EQ(tokens[2].length, 1);
	}
}

TEST(KiwiCpp, Issue71_SentenceSplit_u16)
{
	Kiwi& kiwi = reuseKiwiInstance();
	
	std::u16string str = u"다녀온 후기\n\n<강남 토끼정에 다녀왔습니다.> 음식도 맛있었어요 다만 역시 토끼정 본점 답죠?ㅎㅅㅎ 그 맛이 크으.. 아주 맛있었음...! ^^";
	std::vector<std::pair<size_t, size_t>> sentRanges = kiwi.splitIntoSents(str);
	std::vector<std::u16string> sents;
	for (auto& p : sentRanges)
	{
		sents.emplace_back(str.substr(p.first, p.second - p.first));
	}

	ASSERT_GE(sents.size(), 6);
	EXPECT_EQ(sents[0], u"다녀온 후기");
	EXPECT_EQ(sents[1], u"<강남 토끼정에 다녀왔습니다.>");
	EXPECT_EQ(sents[2], u"음식도 맛있었어요");
	EXPECT_EQ(sents[3], u"다만 역시 토끼정 본점 답죠?ㅎㅅㅎ");
	EXPECT_EQ(sents[4], u"그 맛이 크으..");
	EXPECT_EQ(sents[5], u"아주 맛있었음...! ^^");

	sentRanges = kiwi.splitIntoSents(u"지도부가 어떻게 구성되느냐에 따라");
	EXPECT_EQ(sentRanges.size(), 1);
}

TEST(KiwiCpp, Issue71_SentenceSplit_u8)
{
	Kiwi& kiwi = reuseKiwiInstance();

	std::string str = u8"다녀온 후기\n\n<강남 토끼정에 다녀왔습니다.> 음식도 맛있었어요 다만 역시 토끼정 본점 답죠?ㅎㅅㅎ 그 맛이 크으.. 아주 맛있었음...! ^^";
	std::vector<std::pair<size_t, size_t>> sentRanges = kiwi.splitIntoSents(str);
	std::vector<std::string> sents;
	for (auto& p : sentRanges)
	{
		sents.emplace_back(str.substr(p.first, p.second - p.first));
	}

	ASSERT_GE(sents.size(), 6);
	EXPECT_EQ(sents[0], u8"다녀온 후기");
	EXPECT_EQ(sents[1], u8"<강남 토끼정에 다녀왔습니다.>");
	EXPECT_EQ(sents[2], u8"음식도 맛있었어요");
	EXPECT_EQ(sents[3], u8"다만 역시 토끼정 본점 답죠?ㅎㅅㅎ");
	EXPECT_EQ(sents[4], u8"그 맛이 크으..");
	EXPECT_EQ(sents[5], u8"아주 맛있었음...! ^^");
}

TEST(KiwiCpp, AddRule)
{
	Kiwi& okiwi = reuseKiwiInstance();
	auto ores = okiwi.analyze(u"했어요! 하잖아요! 할까요? 좋아요!", Match::allWithNormalizing);
	
	{
		KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_ & ~BuildOption::loadTypoDict };
		auto inserted = builder.addRule(POSTag::ef, [](std::u16string input)
		{
			if (input.back() == u'요')
			{
				input.back() = u'용';
			}
			return input;
		}, 0);

		Kiwi kiwi = builder.build();
		auto res = kiwi.analyze(u"했어용! 하잖아용! 할까용? 좋아용!", Match::allWithNormalizing);

		EXPECT_EQ(ores.second, res.second);
	}

	{
		KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_ & ~BuildOption::loadTypoDict };
		auto inserted = builder.addRule(POSTag::ef, [](std::u16string input)
		{
			if (input.back() == u'요')
			{
				input.back() = u'용';
			}
			return input;
		}, -1);

		Kiwi kiwi = builder.build();
		auto res = kiwi.analyze(u"했어용! 하잖아용! 할까용? 좋아용!", Match::allWithNormalizing);

		EXPECT_FLOAT_EQ(ores.second -4, res.second);
	}
}

TEST(KiwiCpp, AddPreAnalyzedWord)
{
	Kiwi& okiwi = reuseKiwiInstance();
	auto ores = okiwi.analyze("팅겼어...", Match::allWithNormalizing);

	KiwiBuilder builder{ MODEL_PATH };
	std::vector<std::pair<const char16_t*, POSTag>> analyzed;
	analyzed.emplace_back(u"팅기", POSTag::vv);
	analyzed.emplace_back(u"었", POSTag::ep);
	analyzed.emplace_back(u"어", POSTag::ef);
	
	EXPECT_THROW(builder.addPreAnalyzedWord(u"팅겼어", analyzed), UnknownMorphemeException);

	builder.addWord(u"팅기", POSTag::vv);
	builder.addPreAnalyzedWord(u"팅겼어", analyzed);
	
	Kiwi kiwi = builder.build();
	auto res = kiwi.analyze("팅겼어...", Match::allWithNormalizing);
	
	ASSERT_GE(res.first.size(), 4);
	EXPECT_EQ(res.first[0].str, u"팅기");
	EXPECT_EQ(res.first[0].tag, POSTag::vv);
	EXPECT_EQ(res.first[1].str, u"었");
	EXPECT_EQ(res.first[1].tag, POSTag::ep);
	EXPECT_EQ(res.first[2].str, u"어");
	EXPECT_EQ(res.first[2].tag, POSTag::ef);
	EXPECT_EQ(res.first[3].str, u"...");
	EXPECT_EQ(res.first[3].tag, POSTag::sf);
}

TEST(KiwiCpp, JoinAffix)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto sample = u"사랑스러운 풋사과들아! 배송됐니";
	auto ores = kiwi.analyze(sample, Match::none);
	auto res0 = kiwi.analyze(sample, Match::joinNounPrefix);
	EXPECT_EQ(res0.first[3].str, u"풋사과");
	auto res1 = kiwi.analyze(sample, Match::joinNounSuffix);
	EXPECT_EQ(res1.first[4].str, u"사과들");
	auto res2 = kiwi.analyze(sample, Match::joinNounPrefix | Match::joinNounSuffix);
	EXPECT_EQ(res2.first[3].str, u"풋사과들");
	auto res3 = kiwi.analyze(sample, Match::joinAdjSuffix);
	EXPECT_EQ(res3.first[0].str, u"사랑스럽");
	auto res4 = kiwi.analyze(sample, Match::joinVerbSuffix);
	EXPECT_EQ(res4.first[8].str, u"배송되");
	auto res5 = kiwi.analyze(sample, Match::joinAffix);
	EXPECT_EQ(res5.first[0].str, u"사랑스럽");
	EXPECT_EQ(res5.first[2].str, u"풋사과들");
	EXPECT_EQ(res5.first[5].str, u"배송되");
}

TEST(KiwiCpp, AutoJoiner)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto joiner = kiwi.newJoiner();
	joiner.add(u"시동", POSTag::nng);
	joiner.add(u"를", POSTag::jko);
	EXPECT_EQ(joiner.getU16(), u"시동을");

	joiner = kiwi.newJoiner();
	joiner.add(u"시동", POSTag::nng);
	joiner.add(u"ᆯ", POSTag::jko);
	EXPECT_EQ(joiner.getU16(), u"시동을");

	joiner = kiwi.newJoiner();
	joiner.add(u"나", POSTag::np);
	joiner.add(u"ᆯ", POSTag::jko);
	EXPECT_EQ(joiner.getU16(), u"날");

	joiner = kiwi.newJoiner();
	joiner.add(u"시도", POSTag::nng);
	joiner.add(u"를", POSTag::jko);
	EXPECT_EQ(joiner.getU16(), u"시도를");

	joiner = kiwi.newJoiner();
	joiner.add(u"바다", POSTag::nng);
	joiner.add(u"가", POSTag::jks);
	EXPECT_EQ(joiner.getU16(), u"바다가");

	joiner = kiwi.newJoiner();
	joiner.add(u"바닥", POSTag::nng);
	joiner.add(u"가", POSTag::jks);
	EXPECT_EQ(joiner.getU16(), u"바닥이");

	joiner = kiwi.newJoiner();
	joiner.add(u"불", POSTag::nng);
	joiner.add(u"으로", POSTag::jkb);
	EXPECT_EQ(joiner.getU16(), u"불로");

	joiner = kiwi.newJoiner();
	joiner.add(u"북", POSTag::nng);
	joiner.add(u"으로", POSTag::jkb);
	EXPECT_EQ(joiner.getU16(), u"북으로");

	joiner = kiwi.newJoiner();
	joiner.add(u"갈", POSTag::vv);
	joiner.add(u"면", POSTag::ec);
	EXPECT_EQ(joiner.getU16(), u"갈면");

	joiner = kiwi.newJoiner();
	joiner.add(u"갈", POSTag::vv);
	joiner.add(u"시", POSTag::ep);
	joiner.add(u"았", POSTag::ep);
	joiner.add(u"면", POSTag::ec);
	EXPECT_EQ(joiner.getU16(), u"가셨으면");

	joiner = kiwi.newJoiner();
	joiner.add(u"하", POSTag::vv);
	joiner.add(u"았", POSTag::ep);
	joiner.add(u"다", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"했다");

	joiner = kiwi.newJoiner();
	joiner.add(u"날", POSTag::vv);
	joiner.add(u"어", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"날아");

	joiner = kiwi.newJoiner();
	joiner.add(u"고기", POSTag::nng);
	joiner.add(u"을", POSTag::jko);
	joiner.add(u"굽", POSTag::vv);
	joiner.add(u"어", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"고기를 구워");

	joiner = kiwi.newJoiner();
	joiner.add(u"길", POSTag::nng);
	joiner.add(u"을", POSTag::jko);
	joiner.add(u"걷", POSTag::vv);
	joiner.add(u"어요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"길을 걸어요");

	joiner = kiwi.newJoiner(false);
	joiner.add(u"길", POSTag::nng);
	joiner.add(u"을", POSTag::jko);
	joiner.add(u"걷", POSTag::vv);
	joiner.add(u"어요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"길을 걷어요");

	joiner = kiwi.newJoiner();
	joiner.add(u"땅", POSTag::nng);
	joiner.add(u"에", POSTag::jkb);
	joiner.add(u"묻", POSTag::vv);
	joiner.add(u"어요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"땅에 묻어요");

	joiner = kiwi.newJoiner();
	joiner.add(u"땅", POSTag::nng);
	joiner.add(u"이", POSTag::vcp);
	joiner.add(u"에요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"땅이에요");

	joiner = kiwi.newJoiner();
	joiner.add(u"바다", POSTag::nng);
	joiner.add(u"이", POSTag::vcp);
	joiner.add(u"에요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(), u"바다에요");

	joiner = kiwi.newJoiner();
	joiner.add(u"좋", POSTag::va);
	joiner.add(u"은데", POSTag::ec);
	EXPECT_EQ(joiner.getU16(), u"좋은데");

	joiner = kiwi.newJoiner();
	joiner.add(u"크", POSTag::va);
	joiner.add(u"은데", POSTag::ec);
	EXPECT_EQ(joiner.getU16(), u"큰데");
}

TEST(KiwiCpp, UserWordWithNumeric)
{
	KiwiBuilder builder{ MODEL_PATH };
	EXPECT_TRUE(builder.addWord(u"코로나19", POSTag::nnp, 0.0));
	EXPECT_TRUE(builder.addWord(u"2차전지", POSTag::nnp, 0.0));
	builder.addWord(u"K9", POSTag::nnp, 3.0);
	builder.addWord(u"K55", POSTag::nnp, 3.0);
	Kiwi kiwi = builder.build();

	auto tokens = kiwi.analyze(u"코로나19이다.", Match::all).first;

	ASSERT_GE(tokens.size(), 3);
	EXPECT_EQ(tokens[0].str, u"코로나19");
	EXPECT_EQ(tokens[1].str, u"이");
	EXPECT_EQ(tokens[2].str, u"다");

	tokens = kiwi.analyze(u"2차전지이다.", Match::all).first;

	ASSERT_GE(tokens.size(), 3);
	EXPECT_EQ(tokens[0].str, u"2차전지");
	EXPECT_EQ(tokens[1].str, u"이");
	EXPECT_EQ(tokens[2].str, u"다");

	tokens = kiwi.analyze(u"K9 K55", Match::all).first;
	ASSERT_GE(tokens.size(), 2);
	EXPECT_EQ(tokens[0].str, u"K9");
	EXPECT_EQ(tokens[1].str, u"K55");
}

TEST(KiwiCpp, Quotation)
{
	Kiwi& kiwi = reuseKiwiInstance();
	std::vector<TokenInfo> quotTokens;
	auto tokens = kiwi.analyze(u"그는 \"여러분 이거 다 거짓말인거 아시죠?\"라고 물으며 \"아무것도 모른다\"고 말했다.", Match::allWithNormalizing).first;
	EXPECT_GE(tokens.size(), 26);
	std::copy_if(tokens.begin(), tokens.end(), std::back_inserter(quotTokens), [](const TokenInfo& token)
		{
			return token.str == u"\"";
		});
	EXPECT_EQ(quotTokens.size(), 4);
	EXPECT_EQ(quotTokens[0].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[1].tag, POSTag::ssc);
	EXPECT_EQ(quotTokens[2].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[3].tag, POSTag::ssc);

	tokens = kiwi.analyze(u"\"중첩된 인용부호, 그것은 '중복', '반복', '계속되는 되풀이'인 것이다.\"", Match::allWithNormalizing).first;
	quotTokens.clear();
	std::copy_if(tokens.begin(), tokens.end(), std::back_inserter(quotTokens), [](const TokenInfo& token)
		{
			return token.str == u"\"";
		});
	EXPECT_EQ(quotTokens.size(), 2);
	EXPECT_EQ(quotTokens[0].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[1].tag, POSTag::ssc);
	quotTokens.clear();
	std::copy_if(tokens.begin(), tokens.end(), std::back_inserter(quotTokens), [](const TokenInfo& token)
		{
			return token.str == u"'";
		});
	EXPECT_EQ(quotTokens.size(), 6);
	EXPECT_EQ(quotTokens[0].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[1].tag, POSTag::ssc);
	EXPECT_EQ(quotTokens[2].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[3].tag, POSTag::ssc);
	EXPECT_EQ(quotTokens[4].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[5].tag, POSTag::ssc);

	tokens = kiwi.analyze(u"I'd like to be a tree.", Match::allWithNormalizing).first;
	EXPECT_EQ(tokens[1].tag, POSTag::ss);
}

TEST(KiwiCpp, JoinRestore)
{
	Kiwi& kiwi = reuseKiwiInstance();
	for (auto c : {
		u8"이야기가 얼마나 지겨운지 잘 알고 있다. \" '아!'하고 힐데가르드는 한숨을 푹 쉬며 말했다.",
		u8"승진해서 어쨌는 줄 아슈?",
		u8"2002년 아서 안데르센의 몰락",
		u8"호텔의 음침함이 좀 나아 보일 정도였다",
		u8"황자의 일을 물을려고 부른 것이 아니냐고",
		u8"생겼는 지 제법 알을 품는 것 같다.",
		u8"음악용 CD를 들을 수 있다",
		u8"좋아요도 눌렀다.",
		u8"좋은데",
		u8"않았다",
		u8"인정받았다",
		u8"하지 말아야",
		u8"말았다",
		//u8"비어 있다", 
		//u8"기어 가다", 
		u8"좋은 태도입니다",
		u8"바로 '내일'입니다",
		u8"in the",
		u8"할 것이었다",
		u8"몇 번의 신호 음이 이어지고",
		u8"오래 나가 있는 바람에",
		u8"간이 학교로서 인가를 받은",
		u8"있을 터였다",
		u8"극도의 인륜 상실",
		u8"'내일'을 말하다",
		u8"실톱으로 다듬어 놓은 것들",
		})
	{
		auto tokens = kiwi.analyze(c, Match::allWithNormalizing).first;
		auto joiner = kiwi.newJoiner();
		for (auto& t : tokens)
		{
			joiner.add(t.str, t.tag, false);
		}
		EXPECT_EQ(joiner.getU8(), c);
	}
}

TEST(KiwiCpp, NestedSentenceSplit)
{
	Kiwi& kiwi = reuseKiwiInstance();

	for (auto c : {
		u8"“절망한 자는 대담해지는 법이다”라는 니체의 경구가 부제로 붙은 시이다.",
		u8"우리는 저녁을 먹은 다음 식탁을 치우기 전에 할머니가 떠먹는 요구르트를 다 먹고 조그만 숟가락을 싹싹 핥는 일을 끝마치기를(할머니가 그 숟가락을 브래지어에 쑤셔넣을지도 몰랐다. 요컨대 흔한 일이다) 기다리고 있었다.",
		u8"조현준 효성그룹 회장도 신년사를 통해 “속도와 효율성에 기반한 민첩한 조직으로 탈바꿈해야 한다”며 “이를 위해 무엇보다 데이터베이스 경영이 뒷받침돼야 한다”고 말했다.",
		u8"1699년에 한 학자는 \"어떤 것은 뿔을 앞으로 내밀고 있고, 다른 것은 뾰족한 꼬리를 만들기도 한다.새부리 모양을 하고 있는 것도 있고, 털로 온 몸을 덮고 있다가 전체가 거칠어지기도 하고 비늘로 뒤덮여 뱀처럼 되기도 한다\"라고 기록했다.",
		u8"회사의 정보 서비스를 책임지고 있는 로웬버그John Loewenberg는 <서비스 산업에 있어 종이는 혈관내의 콜레스트롤과 같다. 나쁜 종이는 동맥을 막는 내부의 물질이다.>라고 말한다.",
		u8"그것은 바로 ‘내 임기 중에는 대운하 완공할 시간이 없으니 임기 내에 강별로 소운하부터 개통하겠다’는 것이나 다름없다. ",
		u8"그러나 '문학이란 무엇인가'를 묻느니보다는 '무엇이 하나의 텍스트를 문학으로 만드는가'를 묻자고 제의했던 야콥슨 이래로, 문학의 본질을 정의하기보다는 문학의 존재론을 추적하는 것이 훨씬 생산적인 일이라는 것은 널리 알려진 바가 있다.",
		})
	{
		auto ranges = kiwi.splitIntoSents(c);
		EXPECT_EQ(ranges.size(), 1);
	}
}
