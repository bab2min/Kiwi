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
	auto ptt = basicTypoSet.prepare();
	
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

	TokenResult ret;
	kiwi.setTypoCostWeight(0);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);
	kiwi.setTypoCostWeight(2);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);
	kiwi.setTypoCostWeight(4);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);
	kiwi.setTypoCostWeight(6);
	ret = kiwi.analyze(u"문화제 보호", Match::allWithNormalizing);
}

TEST(KiwiTypo, AnalyzeBasicTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi kiwi = builder.build();

	Kiwi typoKiwi = builder.build(basicTypoSet);
	typoKiwi.setTypoCostWeight(5);

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
