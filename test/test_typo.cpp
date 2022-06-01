#include "gtest/gtest.h"
#include "common.h"
#include <kiwi/Kiwi.h>
#include <kiwi/TypoTransformer.h>

using namespace kiwi;

TEST(KiwiTypo, Generate)
{
	TypoTransformer tt;
	tt.addTypo(u"ㅐㅔ", u"ㅐㅔ");

	UnorderedMap<std::u16string, float> typos;
	
	for (auto e : tt.generate(u"개가납네"))
	{
		typos.emplace(e);
	}

	EXPECT_EQ(typos.size(), 4);
	EXPECT_EQ(typos.find(u"개가납네")->second, 0);
	EXPECT_EQ(typos.find(u"게가납네")->second, 1);
	EXPECT_EQ(typos.find(u"개가납내")->second, 1);
	EXPECT_EQ(typos.find(u"게가납내")->second, 2);
}

TEST(KiwiTypo, Builder)
{
	TypoTransformer tt;
	tt.addTypo(u"ㅐㅔ", u"ㅐㅔ");
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

TEST(KiwiTypo, BasicTypoSet)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	Kiwi kiwi = builder.build();

	Kiwi typoKiwi = builder.build(basicTypoSet);

	TokenResult o = kiwi.analyze(u"외안됀데?", Match::allWithNormalizing);
	TokenResult c = typoKiwi.analyze(u"외안됀데?", Match::allWithNormalizing);
	EXPECT_TRUE(o.second < c.second);
}
