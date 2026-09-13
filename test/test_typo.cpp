#include "gtest/gtest.h"
#include <cmath>
#include <random>
#include "common.h"
#include <kiwi/Kiwi.h>
#include <kiwi/TypoTransformer.h>

using namespace kiwi;

TEST(KiwiTypo, GenerateGraph)
{
	TypoTransformer tt;
	tt.addTypo(u"ㅐ", u"ㅚ");
	tt.addTypo(u"레", u"뢰");
	tt.addTypo(u"뢨", u"룄");
	auto ptt = tt.prepare(true);

	std::vector<TypoGraphNode> graph;
	std::u16string nstr;
	normalizeHangul(nstr, std::u16string_view{ u"그럼 내괴다룄네" });
	auto size = ptt.generateGraph(nstr, graph);
	EXPECT_EQ(size, 11);

	ptt = getDefaultTypoSet(DefaultTypoSet::basicTypoSet).prepare(true);
	nstr.clear();
	normalizeHangul(nstr, std::u16string_view{ u"앗뿔싸 그럼 오늘부터 다시 열심히 해보자꾸나." });
	size = ptt.generateGraph(nstr, graph);
	EXPECT_GT(size, 0);
}

TEST(KiwiTypo, Generate)
{
	TypoTransformer tt;
	tt.addTypo(u"ㅐ", u"ㅔ");
	tt.addTypo(u"ㅔ", u"ㅐ");
	tt.addTypo(u"사에", u"사레");
	auto ptt = tt.prepare();
	UnorderedMap<std::u16string, float> typos;

	typos.clear();
	for (auto e : ptt.generate(u"%없어"))
	{
		typos.emplace(e.str, e.cost);
	}
	EXPECT_EQ(typos.size(), 1);

	typos.clear();
	for (auto e : ptt.generate(u"개가납네", 2))
	{
		typos.emplace(e.str, e.cost);
	}
	EXPECT_EQ(typos.size(), 4);
	EXPECT_EQ(typos.find(u"개가납네")->second, 0);
	EXPECT_EQ(typos.find(u"게가납네")->second, 1);
	EXPECT_EQ(typos.find(u"개가납내")->second, 1);
	EXPECT_EQ(typos.find(u"게가납내")->second, 2);

	typos.clear();
	for (auto e : ptt.generate(u"개가납네", 1))
	{
		typos.emplace(e.str, e.cost);
	}
	EXPECT_EQ(typos.size(), 3);
	EXPECT_EQ(typos.find(u"개가납네")->second, 0);
	EXPECT_EQ(typos.find(u"게가납네")->second, 1);
	EXPECT_EQ(typos.find(u"개가납내")->second, 1);

	typos.clear();
	for (auto e : ptt.generate(u"사에", 2))
	{
		typos.emplace(e.str, e.cost);
	}
	EXPECT_EQ(typos.size(), 3);
	EXPECT_EQ(typos.find(u"사에")->second, 0);
	EXPECT_EQ(typos.find(u"사레")->second, 1);
	EXPECT_EQ(typos.find(u"사애")->second, 1);
}

TEST(KiwiTypo, BasicTypoSet)
{
	auto ptt = getDefaultTypoSet(DefaultTypoSet::basicTypoSet).prepare();

	for (auto t : ptt.generate(u"의"))
	{
	}

	for (auto t : ptt.generate(u"거의"))
	{
	}

	for (auto t : ptt.generate(u"얽히고설키"))
	{
	}
}

TEST(KiwiTypo, Builder)
{
	TypoTransformer tt;
	tt.addTypo(u"ㅐ", u"ㅔ");
	tt.addTypo(u"ㅔ", u"ㅐ");
	auto ptt = tt.prepare(true);

	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, }.build();

	AnalyzeOption option;
	option.match = Match::allWithNormalizing;
	option.typoTransformer = &ptt;

	auto config = kiwi.getGlobalConfig();
	TokenResult ret;
	config.typoCostWeight = 1e-9;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", option);

	config.typoCostWeight = 2;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", option);

	config.typoCostWeight = 4;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", option);

	config.typoCostWeight = 6;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", option);
}

TEST(KiwiTypo, AnalyzeBasicTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi kiwi = builder.build();

	auto ptt = getDefaultTypoSet(DefaultTypoSet::basicTypoSet).prepare(true);

	AnalyzeOption option;
	option.match = Match::allWithNormalizing | Match::oovChrFreqModel;
	option.typoTransformer = &ptt;

	auto config = kiwi.getGlobalConfig();
	config.typoCostWeight = 5;
	kiwi.setGlobalConfig(config);

	TokenResult o = kiwi.analyze(u"외않됀데?", Match::allWithNormalizing | Match::oovChrFreqModel);
	TokenResult c = kiwi.analyze(u"외않됀데?", option);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"나 죰 도와죠.", Match::allWithNormalizing | Match::oovChrFreqModel);
	c = kiwi.analyze(u"나 죰 도와죠.", option);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"잘했따", Match::allWithNormalizing | Match::oovChrFreqModel);
	c = kiwi.analyze(u"잘했따", option);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"외구거 공부", Match::allWithNormalizing | Match::oovChrFreqModel);
	c = kiwi.analyze(u"외구거 공부", option);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"맗은 믈을 마셧다!", Match::allWithNormalizing | Match::oovChrFreqModel);
	c = kiwi.analyze(u"맗은 믈을 마셧다!", option);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"Wertheimer)가 자신의 논문 <운동지각에 관한 실험연구>(Experimental studies on the perception of movement)을 통해 일상적인 지각 현상에 대한 새로운 시각을 제시한 시기이다.",
		Match::allWithNormalizing | Match::oovChrFreqModel);
	c = kiwi.analyze(u"Wertheimer)가 자신의 논문 <운동지각에 관한 실험연구>(Experimental studies on the perception of movement)을 통해 일상적인 지각 현상에 대한 새로운 시각을 제시한 시기이다.",
		option);
}

TEST(KiwiTypo, ContinualTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi kiwi = builder.build();

	auto ptt = getDefaultTypoSet(DefaultTypoSet::continualTypoSet).prepare(true);

	AnalyzeOption option{ Match::allWithNormalizing };
	option.typoTransformer = &ptt;

	auto res = kiwi.analyze(u"프로그래미", option).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].str, u"프로그램");
	EXPECT_EQ(res[1].str, u"이");

	res = kiwi.analyze(u"프로그래믈", option).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].str, u"프로그램");
	EXPECT_EQ(res[1].str, u"을");

	res = kiwi.analyze(u"오늘사무시레서", option).first;
	EXPECT_EQ(res.size(), 3);
	EXPECT_EQ(res[1].str, u"사무실");
	EXPECT_EQ(res[2].str, u"에서");

	res = kiwi.analyze(u"법원이 기가캤다.", option).first;
	EXPECT_EQ(res.size(), 7);
	EXPECT_EQ(res[2].str, u"기각");
	EXPECT_EQ(res[3].str, u"하");

	res = kiwi.analyze(u"하나도 업써.", option).first;
	EXPECT_EQ(res.size(), 5);
	EXPECT_EQ(res[2].str, u"없");
	EXPECT_EQ(res[3].str, u"어");

	res = kiwi.analyze(u"말근 하늘", option).first;
	EXPECT_EQ(res.size(), 3);
	EXPECT_EQ(res[0].str, u"맑");
	EXPECT_EQ(res[1].str, u"은");

	res = kiwi.analyze(u"아주 만타.", option).first;
	EXPECT_EQ(res.size(), 4);
	EXPECT_EQ(res[1].str, u"많");
	EXPECT_EQ(res[2].str, u"다");
}


TEST(KiwiTypo, BasicTypoSetWithContinual)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi kiwi = builder.build();

	auto ptt = getDefaultTypoSet(DefaultTypoSet::basicTypoSetWithContinual).prepare(true);

	AnalyzeOption option;
	option.match = Match::allWithNormalizing | Match::oovChrFreqModel;
	option.typoTransformer = &ptt;

	auto config = kiwi.getGlobalConfig();

	auto res = kiwi.analyze(u"프로그레믈", option, {}, config).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].str, u"프로그램");
	if (res.size() > 1) EXPECT_EQ(res[1].str, u"을");

	res = kiwi.analyze(u"오늘사므시레서", option, {}, config).first;
	EXPECT_EQ(res.size(), 3);
	if (res.size() > 1) EXPECT_EQ(res[1].str, u"사무실");
	if (res.size() > 2) EXPECT_EQ(res[2].str, u"에서");

	res = kiwi.analyze(u"버붠이 기가캤다.", option, {}, config).first;
	EXPECT_EQ(res.size(), 7);
	if (res.size() > 2) EXPECT_EQ(res[2].str, u"기각");
	if (res.size() > 3) EXPECT_EQ(res[3].str, u"하");

	res = kiwi.analyze(u"하나도 업써.", option, {}, config).first;
	EXPECT_EQ(res.size(), 5);
	if (res.size() > 2) EXPECT_EQ(res[2].str, u"없");
	if (res.size() > 3) EXPECT_EQ(res[3].str, u"어");

	res = kiwi.analyze(u"말근 하늘", option, {}, config).first;
	EXPECT_EQ(res.size(), 3);
	EXPECT_EQ(res[0].str, u"맑");
	if (res.size() > 1) EXPECT_EQ(res[1].str, u"은");

	res = kiwi.analyze(u"아주 만타.", option, {}, config).first;
	EXPECT_EQ(res.size(), 4);
	if (res.size() > 1) EXPECT_EQ(res[1].str, u"많");
	if (res.size() > 2) EXPECT_EQ(res[2].str, u"다");
}

TEST(KiwiTypo, LengtheningTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi kiwi = builder.build();

	auto ptt = getDefaultTypoSet(DefaultTypoSet::lengtheningTypoSet).prepare(true);

	AnalyzeOption option;
	option.match = Match::allWithNormalizing;
	option.typoTransformer = &ptt;

	const float typoCost = kiwi.getGlobalConfig().typoCostWeight * 0.25f;

	auto ref = kiwi.analyze(u"진짜?", option);
	auto res = kiwi.analyze(u"지인짜?", option);
	EXPECT_FLOAT_EQ(ref.second - 4 * typoCost, res.second);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"진짜");
	EXPECT_EQ(res.first[1].str, u"?");

	res = kiwi.analyze(u"지인짜아?", option);
	EXPECT_FLOAT_EQ(ref.second - 5 * typoCost, res.second);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"진짜");
	EXPECT_EQ(res.first[1].str, u"?");

	res = kiwi.analyze(u"그으으래?", option);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"그래");
	EXPECT_EQ(res.first[1].str, u"?");

	res = kiwi.analyze(u"그으으으으래?", option);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"그래");
	EXPECT_EQ(res.first[1].str, u"?");

	res = kiwi.analyze(u"학교오를 가야아해", option);
	EXPECT_EQ(res.first.size(), 6);
	EXPECT_EQ(res.first[0].str, u"학교");
	EXPECT_EQ(res.first[1].str, u"를");
	EXPECT_EQ(res.first[2].str, u"가");
	EXPECT_EQ(res.first[3].str, u"어야");
	EXPECT_EQ(res.first[4].str, u"하");
	EXPECT_EQ(res.first[5].str, u"어");
}

TEST(KiwiTypo, SampleTypos)
{
	PreparedTypoTransformer gen{ getDefaultTypoSet(DefaultTypoSet::basicTypoSetWithContinual), false };
	const std::vector<std::u16string> sents = {
		u"이것은 분석기입니다.",
		u"오늘 날씨가 참 좋네요.",
		u"그는 어제 학교에 갔다.",
		u"맞춰 봐야 알겠지만 안 될 것 같아요.",
		u"없어요, 그런 건 여기 없습니다.",
	};
	std::u16string out, out2;

	// 확률이 0이면 원문 그대로이고, 같은 시드는 같은 결과를 낸다.
	std::mt19937_64 rng{ 1 };
	for (auto& s : sents)
	{
		EXPECT_EQ(gen.sampleTypos(out, s, 0, rng), 0);
		EXPECT_EQ(out, s);

		std::mt19937_64 r1{ 42 }, r2{ 42 };
		const size_t n1 = gen.sampleTypos(out, s, 1, r1);
		const size_t n2 = gen.sampleTypos(out2, s, 1, r2);
		EXPECT_GT(n1, 0);
		EXPECT_EQ(n1, n2);
		EXPECT_EQ(out, out2);
		EXPECT_NE(out, s);
	}

	// prob=1이면 모든 지점에 오타가 들어가므로, 그 대비 비율이 곧 지점당 적용률이다.
	size_t full = 0, partial = 0;
	for (int i = 0; i < 20000; ++i)
	{
		for (auto& s : sents)
		{
			full += gen.sampleTypos(out, s, 1, rng);
			partial += gen.sampleTypos(out, s, 0.1f, rng);
		}
	}
	EXPECT_NEAR((double)partial / full, 0.1, 0.005);
}

TEST(KiwiTypo, SampleTyposConditions)
{
	PreparedTypoTransformer gen{ getDefaultTypoSet(DefaultTypoSet::basicTypoSetWithContinual), false };
	// joinHangul 이후에도 남아있는 받침 자모는 음절로 합쳐지지 못한 받침이다.
	const auto hasOrphanCoda = [](const std::u16string& s)
	{
		for (auto c : s) if (isHangulCoda(c)) return true;
		return false;
	};

	// generate()는 적용 조건을 모두 검사하지는 않아서, 합쳐지지 못한 받침이나 문장 첫머리의 경음화를 후보로 내놓는다.
	size_t orphanCands = 0, hardenedCands = 0;
	for (auto e : gen.generate(u"없어요")) if (hasOrphanCoda(e.str)) ++orphanCands;
	for (auto e : gen.generate(u"가방")) if (e.str[0] == u'까') ++hardenedCands;
	EXPECT_GT(orphanCands, 0);
	EXPECT_GT(hardenedCands, 0);

	// sampleTypos는 그런 결과를 내지 않는다.
	const std::vector<std::u16string> sents = { u"없어요", u"앉아서 읽었다", u"젊은이가 밟았다", u"싫어요 않았다", u"값이 넓어요", u"가방을 메고" };
	std::mt19937_64 rng{ 5 };
	std::u16string out;
	size_t numTypos = 0, orphans = 0, hardened = 0;
	for (int i = 0; i < 2000; ++i)
	{
		for (auto& s : sents)
		{
			numTypos += gen.sampleTypos(out, s, 1, rng);
			if (hasOrphanCoda(out)) ++orphans;
			if (out[0] == u'까') ++hardened;
		}
	}
	EXPECT_GT(numTypos, 0);
	EXPECT_EQ(orphans, 0);
	EXPECT_EQ(hardened, 0);
}

TEST(KiwiTypo, SampleTyposCostWeighting)
{
	// '개' 한 지점에서 비용 1인 '게'와 비용 3인 '걔'가 경합한다.
	TypoTransformer tt;
	tt.addTypo(u"ㅐ", u"ㅔ", 1);
	tt.addTypo(u"ㅐ", u"ㅒ", 3);
	PreparedTypoTransformer gen{ tt, false };

	const auto cheapToExpensiveRatio = [&](float costScale)
	{
		std::mt19937_64 rng{ 3 };
		std::u16string out;
		size_t cheap = 0, expensive = 0;
		for (int i = 0; i < 20000; ++i)
		{
			EXPECT_EQ(gen.sampleTypos(out, u"개", 1, rng, 5, costScale), 1);
			if (out == u"게") ++cheap;
			else if (out == u"걔") ++expensive;
		}
		EXPECT_EQ(cheap + expensive, 20000);
		return (double)cheap / expensive;
	};
	// 두 오타가 뽑히는 비율은 exp(costScale * (3 - 1))이어야 한다.
	EXPECT_NEAR(cheapToExpensiveRatio(0), 1, 0.1);
	EXPECT_NEAR(cheapToExpensiveRatio(0.5f), std::exp(1.), 0.3);
	EXPECT_NEAR(cheapToExpensiveRatio(1), std::exp(2.), 0.8);
}
