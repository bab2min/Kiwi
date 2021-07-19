#include "gtest/gtest.h"
#include <kiwi/Kiwi.h>
#include "common.h"

using namespace kiwi;

TEST(KiwiCpp, InitClose)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH }.build();
}

#define KWORD u"킼윜"

TEST(KiwiCpp, BuilderAddWords)
{
	KiwiBuilder builder{ MODEL_PATH };
	EXPECT_TRUE(builder.addWord(KWORD, POSTag::nnp, 0.0));
	Kiwi kiwi = builder.build();

	auto res = kiwi.analyze(KWORD, Match::all);
	EXPECT_EQ(res.first[0].str, KWORD);
}

