#include "gtest/gtest.h"
#include <kiwi/Kiwi.h>
#include "common.h"

using namespace kiwi;

Kiwi& reuseKiwiInstance()
{
	static Kiwi kiwi = KiwiBuilder{ MODEL_PATH }.build();
	return kiwi;
}

TEST(KiwiCpp, InitClose)
{
	Kiwi& kiwi = reuseKiwiInstance();
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
TEST(KiwiCpp, AnalyzeError02) { 
 	Kiwi& kiwi = reuseKiwiInstance(); 
 	TokenResult res = kiwi.analyze(u"키윜ㅋㅋ", Match::all); 
 	EXPECT_EQ(res.first[1].str, std::u16string{ u"ㅋㅋㅋ" });  
 	res = kiwi.analyze(u"키윟ㅎ", Match::all); 
 	EXPECT_EQ(res.first[1].str, std::u16string{ u"ㅎㅎ" });
 	res = kiwi.analyze(u"키윅ㄱ", Match::all); 
 	EXPECT_EQ(res.first[1].str, std::u16string{ u"ㄱㄱ" });
	res = kiwi.analyze(u"키윈ㄴㄴ", Match::all); 
 	EXPECT_EQ(res.first[1].str, std::u16string{ u"ㄴㄴㄴ" });
	res = kiwi.analyze(u"키윊ㅎㅎ", Match::all); 
 	EXPECT_EQ(res.first[2].str, std::u16string{ u"ㅎㅎ" });
	res = kiwi.analyze(u"키윍ㄱㄱ", Match::all); 
 	EXPECT_EQ(res.first[2].str, std::u16string{ u"ㄱㄱ" });
} 
