#include "gtest/gtest.h"
#include <sstream>
#include <vector>
#include <random>

#include "../src/QEncoder.hpp"

static auto lengthes = { 9, 32, 70, 999999 };

TEST(QEncoder, pack4)
{
	std::mt19937_64 rng;
	std::geometric_distribution<size_t> ud{ 0.005 };
	using QCode = kiwi::qe::QCode<0, 2, 8, 16>;

	for (auto len : lengthes)
	{
		std::vector<uint16_t> orig(len), reconstructed(len);
		for (size_t i = 0; i < len; ++i)
		{
			orig[i] = (ud(rng));
		}

		kiwi::qe::Encoder<QCode> encoder;

		encoder.encode<4>(orig.begin(), orig.end());
		QCode::template decode<4>(reconstructed.data(), encoder.getHeader().data(), (const size_t*)encoder.getBody().data(), 0, len);

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(QEncoder, pack8)
{
	std::mt19937_64 rng;
	std::geometric_distribution<size_t> ud{ 0.005 };
	using QCode = kiwi::qe::QCode<0, 2, 8, 16>;

	for (auto len : lengthes)
	{
		std::vector<uint16_t> orig(len), reconstructed(len);
		for (size_t i = 0; i < len; ++i)
		{
			orig[i] = (ud(rng));
		}

		kiwi::qe::Encoder<QCode> encoder;

		encoder.encode<8>(orig.begin(), orig.end());
		QCode::template decode<8>(reconstructed.data(), encoder.getHeader().data(), (const size_t*)encoder.getBody().data(), 0, len);

		EXPECT_EQ(orig, reconstructed);
	}
}

TEST(QEncoder, pack16)
{
	std::mt19937_64 rng;
	std::geometric_distribution<size_t> ud{ 0.005 };
	using QCode = kiwi::qe::QCode<0, 2, 8, 16>;

	for (auto len : lengthes)
	{
		std::vector<uint16_t> orig(len), reconstructed(len);
		for (size_t i = 0; i < len; ++i)
		{
			orig[i] = (ud(rng));
		}

		kiwi::qe::Encoder<QCode> encoder;

		encoder.encode<16>(orig.begin(), orig.end());
		QCode::template decode<16>(reconstructed.data(), encoder.getHeader().data(), (const size_t*)encoder.getBody().data(), 0, len);

		EXPECT_EQ(orig, reconstructed);
	}
}
