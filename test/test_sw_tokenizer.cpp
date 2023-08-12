#include "gtest/gtest.h"
#include <fstream>
#include <kiwi/Kiwi.h>
#include <kiwi/SwTokenizer.h>
#include "common.h"

using namespace kiwi;

Kiwi& reuseKiwiInstance();

template<class Ty>
inline std::string to_string_with_fill(Ty value, char chr, size_t width = 0)
{
	std::string ret = std::to_string(value);
	if (ret.size() < width)
	{
		ret.insert(ret.begin(), width - ret.size(), chr);
	}
	return ret;
}

#if defined(__GNUC__) && __GNUC__ < 5
template<class Ty>
constexpr std::vector<std::pair<Ty, Ty>> toPair(std::initializer_list<Ty> init)
{
	return std::vector<std::pair<Ty, Ty>>{ (const std::pair<Ty, Ty>*)init.begin(), (const std::pair<Ty, Ty>*)init.begin() + init.size() / 2 };
}
#else
template<class Ty, class ATy, size_t n>
constexpr std::vector<std::pair<Ty, Ty>> toPair(const ATy(&init)[n])
{
	static_assert(n % 2 == 0, "initializer_list must have an even number of elements.");
	return std::vector<std::pair<Ty, Ty>>{ (const std::pair<Ty, Ty>*)init, (const std::pair<Ty, Ty>*)init + n / 2 };
}
#endif

TEST(KiwiSwTokenizer, Builder)
{
	using VocabTy = std::tuple<std::string, POSTag, SwTokenFlag, float>;

	SwTokenizerConfig config;
	config.specialTokens[config.unk] = u8"[UNK]";
	config.specialTokens[config.cls] = u8"[CLS]";
	SwTokenizerBuilder builder{ reuseKiwiInstance(), config };

	std::initializer_list<VocabTy> vocabs = {
		VocabTy{ config.specialTokens[config.unk], POSTag::unknown, SwTokenFlag::special, 0}, // 0
		VocabTy{ u8"하", POSTag::vv, SwTokenFlag::none, -3.33 }, // 1
		VocabTy{ u8".", POSTag::unknown, SwTokenFlag::none, -3.34 }, // 2
		VocabTy{ u8"어", POSTag::ep, SwTokenFlag::none, -3.89 }, // 3
		VocabTy{ u8"이", POSTag::vv, SwTokenFlag::none, -4.06 }, // 4
		VocabTy{ u8"ᆫ", POSTag::ep, SwTokenFlag::none, -4.09 }, // 5
		VocabTy{ u8"었", POSTag::ep, SwTokenFlag::none, -4.15 }, // 6
		VocabTy{ u8"을", POSTag::jks, SwTokenFlag::none, -4.79 }, // 7
		VocabTy{ u8"사람", POSTag::unknown, SwTokenFlag::subword, -5.00 }, // 8
		VocabTy{ u8"말", POSTag::unknown, SwTokenFlag::none, -5.10 }, // 9
		VocabTy{ u8"생각", POSTag::unknown, SwTokenFlag::none, -5.20 }, // 10
		VocabTy{ u8"옛", POSTag::unknown, SwTokenFlag::none, -5.30 }, // 11
		VocabTy{ u8"", POSTag::unknown, SwTokenFlag::glue, -6.0 }, // 12
		VocabTy{ config.specialTokens[config.cls], POSTag::unknown, SwTokenFlag::special, 0}, // 13
	};
	for (auto& t : vocabs)
	{
		builder.addToken(std::get<0>(t), std::get<1>(t), std::get<2>(t), std::get<3>(t));
	}
	auto tokenizer = builder.build();
	EXPECT_EQ(tokenizer.size(), vocabs.size());

	std::string inp, reconstructed;
	std::vector<uint32_t> res;
	std::vector<std::pair<uint32_t, uint32_t>> offsets;

	{
		inp = u8"말한 옛사람을 생각했어..";
		offsets.clear();
		res = tokenizer.encode(inp, &offsets);
		EXPECT_EQ(res, std::vector<uint32_t>({ 9, 1, 5, 11, 8, 7, 10, 1, 6, 3, 2, 2 }));
		EXPECT_EQ(res.size(), offsets.size());
		EXPECT_EQ(offsets, toPair<uint32_t>({ 0, 3, 3, 6, 3, 6, 7, 10, 10, 16, 16, 19, 20, 26, 26, 29, 26, 29, 29, 32, 32, 33, 33, 34 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, inp);
	}

	{
		inp = u8"옛 생각";
		offsets.clear();
		res = tokenizer.encode(inp, &offsets);
		EXPECT_EQ(res, std::vector<uint32_t>({ 11, 10 }));
		EXPECT_EQ(res.size(), offsets.size());
		EXPECT_EQ(offsets, toPair<uint32_t>({ 0, 3, 4, 10 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, inp);
	}

	{
		inp = u8"옛생각";
		offsets.clear();
		res = tokenizer.encode(inp, &offsets);
		EXPECT_EQ(res, std::vector<uint32_t>({ 11, 12, 10 }));
		EXPECT_EQ(res.size(), offsets.size());
		EXPECT_EQ(offsets, toPair<uint32_t>({ 0, 3, 3, 3, 3, 9 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, inp);
	}

	{
		inp = u8"[CLS] 잘 모르는 단어";
		offsets.clear();
		res = tokenizer.encode(inp, &offsets);
		EXPECT_EQ(res, std::vector<uint32_t>({ 13, 0, 0, 0 }));
		EXPECT_EQ(res.size(), offsets.size());
		EXPECT_EQ(offsets, toPair<uint32_t>({ 0, 5, 6, 9, 10, 19, 20, 26 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, u8"[CLS] [UNK] [UNK] [UNK]");
	}

	{
		std::ofstream ofs{ "test_tokenizer.json" };
		tokenizer.save(ofs);
	}

	{
		std::ifstream ifs{ "test_tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
	}

	{
		inp = u8"말한 옛사람을 생각했어..";
		res = tokenizer.encode(inp);
		EXPECT_EQ(res, std::vector<uint32_t>({ 9, 1, 5, 11, 8, 7, 10, 1, 6, 3, 2, 2 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, inp);
	}

	{
		inp = u8"옛 생각";
		res = tokenizer.encode(inp);
		EXPECT_EQ(res, std::vector<uint32_t>({ 11, 10 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, inp);
	}

	{
		inp = u8"옛생각";
		res = tokenizer.encode(inp);
		EXPECT_EQ(res, std::vector<uint32_t>({ 11, 12, 10 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, inp);
	}

	{
		inp = u8"[CLS] 잘 모르는 단어";
		res = tokenizer.encode(inp);
		EXPECT_EQ(res, std::vector<uint32_t>({ 13, 0, 0, 0 }));

		reconstructed = tokenizer.decode(res);
		EXPECT_EQ(reconstructed, u8"[CLS] [UNK] [UNK] [UNK]");
	}
}

TEST(KiwiSwTokenizer, EncodeError)
{
	SwTokenizer tokenizer;
	{
		std::ifstream ifs{ "tokenizers/kor.16k.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
	}

	for (auto c : {
		u8"또는 “𡆮”으로 새겨야 하는 것",
		})
	{
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		//EXPECT_EQ(decoded, c);
	}
}

TEST(KiwiSwTokenizer, BasicEncodeAndDecode)
{
	SwTokenizer tokenizer;
	{
		std::ifstream ifs{ "test/written.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
	}

	for (auto c : { 
		u8"",
		u8"한국어에 특화된 토크나이저입니다.", 
		u8"감사히 먹겠습니당!",
		u8"노래진 손톱을 봤던걸요.",
		u8"제임스웹우주천체망원경",
		u8"그만해여~",
		//u8"공부도 시킬 만큼 시켰는데 미쳐버리다니",
	})
	{
		std::vector<std::pair<uint32_t, uint32_t>> offsets;
		auto encoded = tokenizer.encode(c, &offsets);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, c);
	}
}

TEST(KiwiSwTokenizer, EncodeFromAlreadyTokenized)
{
	SwTokenizer tokenizer;
	{
		std::ifstream ifs{ "test/written.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
	}

	for (auto c : {
		u8"",
		u8"한국어에 특화된 토크나이저입니다.",
		u8"감사히 먹겠습니당!",
		u8"노래진 손톱을 봤던걸요.",
		u8"제임스웹우주천체망원경",
		u8"그만해여~",
	})
	{
		auto result = tokenizer.getKiwi()->analyze(c, Match::allWithNormalizing | Match::zCoda).first;
		std::vector<std::pair<std::u16string, POSTag>> tokens;
		std::vector<std::tuple<std::u16string, POSTag, bool>> tokensWithSpaceness;
		std::vector<std::pair<uint32_t, uint32_t>> offset;
		for (auto& t : result)
		{
			tokens.emplace_back(t.str, t.tag);
			bool hasSpacePrefix = &t == result.data() || ((&t)[-1].position + (&t)[-1].length < t.position);
			tokensWithSpaceness.emplace_back(t.str, t.tag, hasSpacePrefix);
		}
		auto encoded = tokenizer.encode(tokens, &offset);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, c);
		
		offset.clear();
		auto encodedUsingSpaceness = tokenizer.encode(tokensWithSpaceness, &offset);
		auto decodedUsingSpaceness = tokenizer.decode(encodedUsingSpaceness);
		EXPECT_EQ(decodedUsingSpaceness, c);
	}

	for (auto c : {
		u8"제임스웹 우주천체망원경",
	})
	{
		auto result = tokenizer.getKiwi()->analyze(c, Match::allWithNormalizing | Match::zCoda).first;
		std::vector<std::tuple<std::u16string, POSTag, bool>> tokensWithSpaceness;
		for (auto& t : result)
		{
			bool hasSpacePrefix = &t == result.data() || ((&t)[-1].position + (&t)[-1].length < t.position);
			tokensWithSpaceness.emplace_back(t.str, t.tag, hasSpacePrefix);
		}
		auto encodedUsingSpaceness = tokenizer.encode(tokensWithSpaceness);
		auto decodedUsingSpaceness = tokenizer.decode(encodedUsingSpaceness);
		EXPECT_EQ(decodedUsingSpaceness, c);
	}
}

TEST(KiwiSwTokenizer, WholeWordUnk)
{
	SwTokenizer tokenizer;
	{
		std::ifstream ifs{ "test/written.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
	}

	auto c = u8"화폐의 주인공은 110여 년 전의 여류 직업 소설가 '히구치 이치樋口一葉'이다.";
	{
		tokenizer.setWholeWordUnk(false);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"화폐의 주인공은 110여 년 전의 여류 직업 소설가'히구치 이치 [UNK]口一葉'이다.");
	}

	{
		tokenizer.setWholeWordUnk(true);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"화폐의 주인공은 110여 년 전의 여류 직업 소설가'히구치 [UNK]'이다.");
	}
}

TEST(KiwiSwTokenizer, FallbackHangul)
{
	auto c = u8"분석이 어려운 한글에는 뚮뷇괗 등이 있다.";
	SwTokenizer tokenizer;
	{
		std::ifstream ifs{ "test/written.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"분석이 어려운 한글에는 [UNK] 등이 있다.");
	}

	{
		std::ifstream ifs{ "test/written.fallback_hangul.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"분석이 어려운 한글에는 뚮뷇괗 등이 있다.");
	}
}

TEST(KiwiSwTokenizer, FallbackByte)
{
	auto c = u8"분석이 어려운 유니코드에는 💯η💢💥 등이 있다.";
	SwTokenizer tokenizer;
	{
		std::ifstream ifs{ "test/written.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"분석이 어려운 유니코드에는 [UNK] 등이 있다.");
	}

	{
		std::ifstream ifs{ "test/written.fallback_byte.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"분석이 어려운 유니코드에는 💯η💢💥 등이 있다.");
	}
}

TEST(KiwiSwTokenizer, Newline)
{
	auto c = u8"줄 바꿈이 하나\n둘\n\n셋\n\n\n있어요.";
	SwTokenizer tokenizer;
	{
		std::ifstream ifs{ "test/written.fallback_byte.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"줄 바꿈이 하나 둘 셋 있어요.");
	}

	{
		std::ifstream ifs{ "test/written.newline.tokenizer.json" };
		tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
		auto encoded = tokenizer.encode(c);
		auto decoded = tokenizer.decode(encoded);
		EXPECT_EQ(decoded, u8"줄 바꿈이 하나\n둘\n\n셋\n\n\n있어요.");
	}
}

TEST(KiwiSwTokenizer, FallbackByteDecodeIgnoreErrors)
{
	SwTokenizer tokenizer;
	std::ifstream ifs{ "test/written.fallback_byte.tokenizer.json" };
	tokenizer = SwTokenizer::load(reuseKiwiInstance(), ifs);
	
	std::vector<uint32_t> d{ 32255, 32254, 32253, 10, 32230, 32128 };

	{
		EXPECT_ANY_THROW(tokenizer.decode(d, false));
	}

	{
		auto decoded = tokenizer.decode(d);
		EXPECT_EQ(decoded, u8"<0xFF><0xFE><0xFD>에<0xE6><0x80>");
	}
}
