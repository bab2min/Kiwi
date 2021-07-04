#include "gtest/gtest.h"
#include <kiwi/capi.h>

TEST(TestCaseName, TestName) {
	
	kiwi_init("", 0, 0);

	EXPECT_EQ(1, 1);
	EXPECT_TRUE(true);
}