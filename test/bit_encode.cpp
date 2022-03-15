#include "gtest/gtest.h"
#include <sstream>
#include <vector>
#include <random>
#include "../src/BitEncoder.hpp"

static auto lengthes = { 1, 8, 10, 30, 9999 };

TEST(BitEncoder, Fixed_4Bit_uint8) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 15 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream& , 4> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), (len * 4 + 7) / 8);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 4> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(BitEncoder, Fixed_5Bit_uint8) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 31 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream& , 5> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), (len * 5 + 7) / 8);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 5> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(BitEncoder, Fixed_8Bit_uint8) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 255 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream&, 8> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), len);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 8> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(BitEncoder, Fixed_10Bit_uint8) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 1023 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream&, 10> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), (len * 10 + 7) / 8);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 10> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(BitEncoder, Fixed_4Bit_uint32) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 15 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream&, 4, uint32_t> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), (len * 4 + 31) / 32 * 4);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 4, uint32_t> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(BitEncoder, Fixed_5Bit_uint32) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 31 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream&, 5, uint32_t> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), (len * 5 + 31) / 32 * 4);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 5, uint32_t> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(BitEncoder, Fixed_8Bit_uint32) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 255 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream&, 8, uint32_t> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), (len + 3) / 4 * 4);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 8, uint32_t> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(BitEncoder, Fixed_10Bit_uint32) 
{
	std::mt19937_64 rng;
	std::uniform_int_distribution<size_t> ud{ 0, 1023 };
	for (auto len : lengthes)
	{
		std::vector<size_t> orig, reconstructed;
		for (size_t i = 0; i < len; ++i)
		{
			orig.emplace_back(ud(rng));
		}

		std::ostringstream oss;
		kiwi::lm::FixedLengthEncoder<std::ostringstream&, 10, uint32_t> encoder{ oss };
		for (size_t i = 0; i < len; ++i) encoder.write(orig[i]);
		encoder.flush();

		EXPECT_EQ(oss.str().size(), (len * 10 + 31) / 32 * 4);

		kiwi::lm::FixedLengthEncoder<std::istringstream, 10, uint32_t> decoder{ oss.str() };
		for (size_t i = 0; i < len; ++i)
		{
			reconstructed.emplace_back(decoder.read());
		}

		EXPECT_EQ(orig, reconstructed);
	}
}
