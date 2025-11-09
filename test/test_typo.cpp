#include "gtest/gtest.h"
#include "common.h"
#include <kiwi/Kiwi.h>
#include <kiwi/TypoTransformer.h>

using namespace kiwi;

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
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, }.build(tt);

	auto config = kiwi.getGlobalConfig();
	TokenResult ret;
	config.typoCostWeight = 1e-9;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);
	
	config.typoCostWeight = 2;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);
	
	config.typoCostWeight = 4;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);

	config.typoCostWeight = 6;
	kiwi.setGlobalConfig(config);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);
}

TEST(KiwiTypo, AnalyzeBasicTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi kiwi = builder.build();

	Kiwi typoKiwi = builder.build(DefaultTypoSet::basicTypoSet);
	auto config = typoKiwi.getGlobalConfig();
	config.typoCostWeight = 5;
	typoKiwi.setGlobalConfig(config);

	TokenResult o = kiwi.analyze(u"외않됀데?", Match::allWithNormalizing);
	TokenResult c = typoKiwi.analyze(u"외않됀데?", Match::allWithNormalizing);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"나 죰 도와죠.", Match::allWithNormalizing);
	c = typoKiwi.analyze(u"나 죰 도와죠.", Match::allWithNormalizing);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"잘했따", Match::allWithNormalizing);
	c = typoKiwi.analyze(u"잘했따", Match::allWithNormalizing);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"외구거 공부", Match::allWithNormalizing);
	c = typoKiwi.analyze(u"외구거 공부", Match::allWithNormalizing);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"맗은 믈을 마셧다!", Match::allWithNormalizing);
	c = typoKiwi.analyze(u"맗은 믈을 마셧다!", Match::allWithNormalizing);
	EXPECT_TRUE(o.second < c.second);

	o = kiwi.analyze(u"Wertheimer)가 자신의 논문 <운동지각에 관한 실험연구>(Experimental studies on the perception of movement)을 통해 일상적인 지각 현상에 대한 새로운 시각을 제시한 시기이다.",
		Match::allWithNormalizing);
	c = typoKiwi.analyze(u"Wertheimer)가 자신의 논문 <운동지각에 관한 실험연구>(Experimental studies on the perception of movement)을 통해 일상적인 지각 현상에 대한 새로운 시각을 제시한 시기이다.",
		Match::allWithNormalizing);
}

TEST(KiwiTypo, ContinualTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi typoKiwi = builder.build(DefaultTypoSet::continualTypoSet);

	auto res = typoKiwi.analyze(u"프로그래미", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].str, u"프로그램");
	EXPECT_EQ(res[1].str, u"이");

	res = typoKiwi.analyze(u"프로그래믈", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].str, u"프로그램");
	EXPECT_EQ(res[1].str, u"을");

	res = typoKiwi.analyze(u"오늘사무시레서", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 3);
	EXPECT_EQ(res[1].str, u"사무실");
	EXPECT_EQ(res[2].str, u"에서");

	res = typoKiwi.analyze(u"법원이 기가캤다.", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 7);
	EXPECT_EQ(res[2].str, u"기각");
	EXPECT_EQ(res[3].str, u"하");

	res = typoKiwi.analyze(u"하나도 업써.", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 5);
	EXPECT_EQ(res[2].str, u"없");
	EXPECT_EQ(res[3].str, u"어");

	res = typoKiwi.analyze(u"말근 하늘", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 3);
	EXPECT_EQ(res[0].str, u"맑");
	EXPECT_EQ(res[1].str, u"은");

	res = typoKiwi.analyze(u"아주 만타.", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 4);
	EXPECT_EQ(res[1].str, u"많");
	EXPECT_EQ(res[2].str, u"다");
}


TEST(KiwiTypo, BasicTypoSetWithContinual)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi typoKiwi = builder.build(DefaultTypoSet::basicTypoSetWithContinual);

	auto res = typoKiwi.analyze(u"프로그레미", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].str, u"프로그램");
	EXPECT_EQ(res[1].str, u"이");

	res = typoKiwi.analyze(u"프로그레믈", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].str, u"프로그램");
	EXPECT_EQ(res[1].str, u"을");

	res = typoKiwi.analyze(u"오늘사므시레서", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 3);
	EXPECT_EQ(res[1].str, u"사무실");
	EXPECT_EQ(res[2].str, u"에서");

	res = typoKiwi.analyze(u"버붠이 기가캤다.", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 7);
	EXPECT_EQ(res[2].str, u"기각");
	EXPECT_EQ(res[3].str, u"하");

	res = typoKiwi.analyze(u"하나도 업써.", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 5);
	EXPECT_EQ(res[2].str, u"없");
	EXPECT_EQ(res[3].str, u"어");

	res = typoKiwi.analyze(u"말근 하늘", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 3);
	EXPECT_EQ(res[0].str, u"맑");
	EXPECT_EQ(res[1].str, u"은");

	res = typoKiwi.analyze(u"아주 만타.", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 4);
	EXPECT_EQ(res[1].str, u"많");
	EXPECT_EQ(res[2].str, u"다");
}

TEST(KiwiTypo, LengtheningTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi typoKiwi = builder.build(DefaultTypoSet::lengtheningTypoSet);
	const float typoCost = typoKiwi.getGlobalConfig().typoCostWeight * 0.25f;

	auto ref = typoKiwi.analyze(u"진짜?", Match::allWithNormalizing);
	auto res = typoKiwi.analyze(u"지인짜?", Match::allWithNormalizing);
	EXPECT_FLOAT_EQ(ref.second - 4 * typoCost, res.second);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"진짜");
	EXPECT_EQ(res.first[1].str, u"?");

	res = typoKiwi.analyze(u"지인짜아?", Match::allWithNormalizing);
	EXPECT_FLOAT_EQ(ref.second - 5 * typoCost, res.second);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"진짜");
	EXPECT_EQ(res.first[1].str, u"?");

	res = typoKiwi.analyze(u"그으으래?", Match::allWithNormalizing);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"그래");
	EXPECT_EQ(res.first[1].str, u"?");

	res = typoKiwi.analyze(u"그으으으으래?", Match::allWithNormalizing);
	EXPECT_EQ(res.first.size(), 2);
	EXPECT_EQ(res.first[0].str, u"그래");
	EXPECT_EQ(res.first[1].str, u"?");

	res = typoKiwi.analyze(u"학교오를 가야아해", Match::allWithNormalizing);
	EXPECT_EQ(res.first.size(), 6);
	EXPECT_EQ(res.first[0].str, u"학교");
	EXPECT_EQ(res.first[1].str, u"를");
	EXPECT_EQ(res.first[2].str, u"가");
	EXPECT_EQ(res.first[3].str, u"어야");
	EXPECT_EQ(res.first[4].str, u"하");
	EXPECT_EQ(res.first[5].str, u"어");
}
