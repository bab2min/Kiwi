#include "gtest/gtest.h"
#include <kiwi/Kiwi.h>
#include <kiwi/Dataset.h>
#include <kiwi/SubstringExtractor.h>
#include <unordered_map>
#include <vector>
#include <sstream>
#include "common.h"

class TestInitializer
{
public:
	TestInitializer()
	{
#ifdef _MSC_VER
		SetConsoleOutputCP(CP_UTF8);
#endif
	}
};

TestInitializer _global_initializer;

using namespace kiwi;

inline testing::AssertionResult testTokenization(Kiwi& kiwi, const std::u16string& s)
{
	auto tokens = kiwi.analyze(s, Match::all).first;
	if (tokens.empty()) return testing::AssertionFailure() << "kiwi.analyze(" << testing::PrintToString(s) << ") yields an empty result.";
	if (tokens.back().position + tokens.back().length == s.size())
	{
		return testing::AssertionSuccess();
	}
	else
	{
		return testing::AssertionFailure() << "the result of kiwi.analyze(" << testing::PrintToString(s) << ") ends at " << (tokens.back().position + tokens.back().length);
	}
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

Kiwi& reuseKiwiInstance()
{
	static Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, ModelType::none }.build();
	return kiwi;
}

TEST(KiwiCpp, ChrTokenizer)
{
	ChrTokenizer tokenizer;
	const std::string_view s = u8"안녕하세요.오늘날씨가참좋네요!Adx9810::~";

	Vector<int32_t> encodedBuf(s.size());
	encodedBuf.erase(encodedBuf.begin() + tokenizer.encode(s, encodedBuf.data(), encodedBuf.size()), encodedBuf.end());
	EXPECT_TRUE(std::all_of(encodedBuf.begin(), encodedBuf.end(), [&](int32_t t) { return t < tokenizer.vocabSize(); }));

	std::string decoded = utf16To8(tokenizer.decode(encodedBuf.data(), encodedBuf.size()));
	EXPECT_EQ(s, decoded);
}

TEST(KiwiCpp, ChrModel)
{
	ChrTokenizer tokenizer;
	Kiwi& kiwi = reuseKiwiInstance();

	auto streamProvider = utils::makeFilesystemProvider(MODEL_PATH);
	auto stream = streamProvider("nounchr.mdl");

	auto chrModel = lm::CoNgramModelBase::create(utils::createMemoryObjectFromStream(*stream),
		kiwi.archType(),
		false,
		true
	);

	EXPECT_EQ(chrModel->vocabSize(), tokenizer.vocabSize());

	std::array<int32_t, 256> buf = { 0, };
	for (auto str : { 
		"한국어",
		"됐습니다.",
		"AS365버전",
		"형태",
		"형태를",
		"바다를",
		"샤를",
		"카를",
		"아를",
		"자갈을",
		"생선마을",
		"북구을",
		"분당을",
		"사람을",
		"도서관을",
		"이민철",
		"김민철",
		"황보민수",
		"남궁민수",
		})
	{
		size_t size = tokenizer.encode(str, buf.data(), buf.size());
		buf[size++] = 0;
		float accScore = 0;
		int32_t nodeIdx = 0;
		uint32_t contextIdx = 0;
		chrModel->progressOneStep(nodeIdx, contextIdx, 0);
		for (size_t i = 0; i < size; ++i)
		{
			const size_t depth = chrModel->getNodeDepth(nodeIdx);
			const float score = chrModel->progressOneStep(nodeIdx, contextIdx, buf[i]);
			const float freq = chrModel->getContextFrequency(contextIdx);
			const float entropy = chrModel->getContextEntropy(contextIdx);
			auto tokenStr = utf16To8(tokenizer.decode(&buf[i], 1));
			std::cerr << "  Token: " << tokenStr << "(" << buf[i] << ") Score: " << score << " Depth: " << depth << " Freq: " << freq << " Entropy: " << entropy << std::endl;
			EXPECT_LT(score, 0.01);
			accScore += score;
		}
		std::cerr << "AccScore for \"" << str << "\": " << accScore << " AvgScore: " << (accScore / (size - 1)) << std::endl;
		EXPECT_LT(accScore, 0);
	}
}

TEST(KiwiCpp, ChrDataset)
{
	constexpr size_t batchSize = 64, contextSize = 8, sentSize = 1000;
	ChrDataset dataset{ batchSize, contextSize, 0, 0.f };
	double totalWeight = 0.f;
	for (size_t i = 0; i < sentSize; ++i)
	{
		const float weight = 1.f / (i + 2.f);
		dataset.addSentence(std::to_string(i), weight, "0");
		totalWeight += weight;
	}

	auto vocabProbs = dataset.getVocabProbs();
	EXPECT_EQ(vocabProbs.size(), dataset.vocabSize());

	std::array<int32_t, batchSize* contextSize> inBuf, outBuf;
	ChrTokenizer tokenizer;

	Vector<size_t> cnts(sentSize);
	size_t totalSampled = 0;
	for (size_t b = 0; b < 10000; ++b)
	{
		const size_t n = dataset.next(inBuf.data(), outBuf.data());
		for (size_t i = 0; i < n; ++i)
		{
			const auto decoded = tokenizer.decode(&inBuf[i * contextSize + 1], contextSize - 1);
			const size_t v = std::stoi(utf16To8(decoded));
			cnts[v] += 1;
			totalSampled += 1;
		}
	}

	for (size_t i = 0; i < sentSize; ++i)
	{
		const float expectedProb = (float)((1.f / (i + 2.f)) / totalWeight);
		const float actualProb = (float)(cnts[i] / (double)totalSampled);
		EXPECT_NEAR(expectedProb, actualProb, expectedProb * 0.1f) << " for sentence " << i;
	}
}

TEST(KiwiCpp, ExtractSubstrings)
{
	const std::u16string s = u"자, 너 오늘 하루 뭐 했니? "
		"난 오늘 하루가 좀 단순했지. 음 뭐 했는데? "
		"아침에 수업 받다가 오구 그~ 학생이, 응. 미찌 학생인데, "
		"음. 되게 귀엽게 생겼다, 음. 되게 웃으면 곰돌이 인형같이 생겼어. "
		"곰돌이? 응 아니 곰돌이도 아니구, 어쨌든 무슨 인형같이 생겼어, "
		"펜더곰 그런 거, 왜 이렇게 닮은 사람을 대 봐. "
		"내가 아는 사람 중에서, 한무 한무? 금붕어잖아? "
		"맞어 눈도 이렇게 톡 튀어나오구, 어. 한무? 조금 잘 생긴 한무. "
		"잘 생긴 게 아니라 귀여운 한무. 귀여운 한무? "
		"어 학원에서 별명도 귀여운 한무였어. "
		"응. 눈이 똥그래 가지고 그래? 어. 좀 특이한 사람이구나.";
	auto substrings = extractSubstrings(s.data(), s.data() + s.size(), 2, 2, 32, true, u' ');
	EXPECT_EQ(substrings.size(), 23);

	const std::u16string t = u"어. 그러다가 갈아타야 되니까 비몽사몽간에 갈아타는 데로 사람 따라서 쓸려가. "
		"음. 그러다가 요행히 자리를 차지하고 앉어. 음. 그러면은, 어. 예의상 처음부터는 이렇게 신문을 좀 "
		"보는 척을 해. 그러다가 까무룩 잠이 드는 거야, 까무룩? 근데 내릴 때는 그래두 기가 막히게 내리지? "
		"내릴 때 딱 한 번. 어, 들켰어? 넘어 간 넘어 간 적이 있었어, 너 어떻게 했어? 돌아왔지 뭐~, 다시 "
		"타고 얼마나 갔는데? 한 정거장 더 갔을 거야. 아마 선릉인가 삼성인가에, 어. 내려 가지고 삼성인가 "
		"보다 두 정거장 더 가서, 어. 다시 왔지. 음. 나는 버스 타고 다니잖아? 음. 근데 학원에 갈 때도 버스 "
		"타고 아니면 여기 교보빌딩 갈 때도 버스 타잖아? 음 짜증나겠구나, 아니야 되게 가깝잖아, 버스 타고 십 "
		"분, 막히잖아 광화문터널, 안 막혀 별루 그 시간에. 그래 좋겠다. 일곱시 반부터 여덟시부터는 막히는지 "
		"모르겠는데 어쨌든 일곱시 반부터 내가 빨리 타면 일곱시 사십분 차를 타고 늦게 타면 사십오분 차를 "
		"타거던, 음. 그때는 그까 앉아 가는 일은 별루 없어. 음. 별루 애들이 거기도 뭐~ 애들이 아현 거기쯤에 "
		"내리더라, 학생들이 그쪽 학교 있지? 너두 거기 나왔잖아? 음. 이름이 뭐지? 한성! 어이구 아네,";
	substrings = extractSubstrings(t.data(), t.data() + t.size(), 3, 3, 32, true, u' ');
	EXPECT_EQ(substrings.size(), 3);
}

TEST(KiwiCpp, InitClose)
{
	Kiwi& kiwi = reuseKiwiInstance();
}

TEST(KiwiCpp, EmptyResult)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto testCases = {
		u"걍 꽁 다이아 먹고 간거지.",
		u"쭈~욱 늘어나는 치즈",
		u"쮸~욱 늘어나는 치즈",
		u"보일덱BD2",
		u"5스트릿/7스트릿",
		u"며",
		u"\"오쿠\"(",
		u"키미토나라바킷토ah",
		u"제이플래닛2005년생위주6인조걸그룹",
		u"당장 유튜브에서 '페ㅌ",
		u"스쿠비쿨로",
		u"키블러",
		u"포뮬러",
		u"오리쿨로",
		u"만들어졌다\" 며 여전히 냉정하게 반응한다.",
		u"통과했며",
		u"우걱우걱\"",
		u"네오 플래닛S",
		u"YJ 뭐위 웨이촹GTS",
		u"쮸쮸\"",
		u"스틸블루",
		u"15살이었므로",
		u"타란튤라",
	};
	for (auto s : testCases)
	{
		EXPECT_TRUE(testTokenization(kiwi, s));
	}
}

TEST(KiwiCpp, SingleResult)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto testCases = {
		u"발신광고갑자기연락드려죄송합니다국내규모가장큰세력이며만구독자를보유하고있고주식유튜버입니다무조건큰돈버는세력",
		u"재미있게밨어요",
	};
	for (auto s : testCases)
	{
		auto res = kiwi.analyze(s, Match::allWithNormalizing);
		EXPECT_GT(res.first.size(), 1);
	}
}

TEST(KiwiCpp, SingleConsonantMorpheme)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.analyze(u"구원의 손길을 내민 시민들", Match::allWithNormalizing).first;
	EXPECT_EQ(res[4].str, u"내밀");

	res = kiwi.analyze(u"서툰 모습을", Match::allWithNormalizing).first;
	EXPECT_EQ(res[0].str, u"서툴");
}

TEST(KiwiCpp, SpecialTokenErrorOnContinualTypo)
{
	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, ModelType::none };
	Kiwi typoKiwi = builder.build();
	AnalyzeOption option = Match::allWithNormalizing;
	option.typoTransformer = getDefaultPreparedTypoSet(DefaultTypoSet::continualTypoSet);
	
	auto res = typoKiwi.analyze(u"감사합니다 -친구들과", option).first;
	EXPECT_EQ(res[0].str, u"감사");
	EXPECT_EQ(res[1].str, u"하");
	EXPECT_EQ(res[3].str, u"-");
	EXPECT_EQ(res[3].tag, POSTag::so);
}

TEST(KiwiCpp, MultiWordTypo)
{
	Kiwi& kiwi = reuseKiwiInstance();
	AnalyzeOption option = Match::allWithNormalizing;
	auto res = kiwi.analyze(u"존 F. 케네디 주니어", option).first;
	EXPECT_EQ(res[0].str, u"존 F. 케네디 주니어");
	res = kiwi.analyze(u"존 F. 캐네디 주니어", option).first;
	EXPECT_NE(res[0].str, u"존 F. 케네디 주니어");
	option.typoTransformer = getDefaultPreparedTypoSet(DefaultTypoSet::basicTypoSet);
	res = kiwi.analyze(u"존 F. 캐네디 주니어", option).first;
	EXPECT_EQ(res[0].str, u"존 F. 케네디 주니어");
	res = kiwi.analyze(u"존F.캐네디주니어", option).first;
	EXPECT_EQ(res[0].str, u"존 F. 케네디 주니어");
}

TEST(KiwiCpp, SplitComplex)
{
	Kiwi& kiwi = reuseKiwiInstance();
	{
		auto testCases = {
			//u"고맙습니다",
			u"고마워합니다",
			u"고마움을",
		};
		for (auto s : testCases)
		{
			auto res1 = kiwi.analyze(s, Match::allWithNormalizing);
			auto res2 = kiwi.analyze(s, Match::allWithNormalizing | Match::splitComplex);
			//EXPECT_NE(res1.first[0].str, u"고맙");
			EXPECT_EQ(res2.first[0].str, u"고맙");
		}
	}

	{
		auto testCases = {
			u"감사히",
		};
		for (auto s : testCases)
		{
			auto res1 = kiwi.analyze(s, Match::allWithNormalizing);
			auto res2 = kiwi.analyze(s, Match::allWithNormalizing | Match::splitComplex);
			//EXPECT_NE(res1.first[0].str, u"감사");
			EXPECT_EQ(res2.first[0].str, u"감사");
		}
	}
	
	{
		auto testCases = {
			u"집에 갔어요",
			u"집에 가요",
		};
		for (auto s : testCases)
		{
			auto res1 = kiwi.analyze(s, Match::allWithNormalizing);
			auto res2 = kiwi.analyze(s, Match::allWithNormalizing | Match::splitComplex);
			EXPECT_EQ(res1.first[res1.first.size() - 1].str, u"어요");
			EXPECT_EQ(res2.first[res2.first.size() - 1].str, u"요");
		}
	}
}

TEST(KiwiCpp, OldHangul)
{
	Kiwi& kiwi = reuseKiwiInstance();
	for (auto& str : {
		std::u16string{ u"나랏〮말〯ᄊᆞ미〮 듀ᇰ귁에〮 달아〮 문ᄍᆞᆼ와〮로 서르 ᄉᆞᄆᆞᆺ디〮 아니〮ᄒᆞᆯᄊᆡ〮" } ,
		std::u16string{ u"옛날에 갑ᄅᆈᆼ(甲龍)이라는 사람이 살었어. 이갑ᄅᆈᆼ이라고? 살었는디." },
	})
	{
		auto res = kiwi.analyze(str, Match::allWithNormalizing).first;
		for (auto& t : res)
		{
			if (std::any_of(t.str.begin(), t.str.end(), [](char16_t c)
			{
				return isOldHangulOnset(c) || isOldHangulVowel(c) || isOldHangulCoda(c) || isOldHangulToneMark(c);
			})) EXPECT_TRUE(!isSpecialClass(t.tag));
			if (isNounClass(t.tag))
			{
				auto s = str.substr(t.position, t.length);
				EXPECT_EQ(t.str, s);
			}
		}
	}
}

TEST(KiwiCpp, ChineseVsEmoji)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.analyze(u"韓𠀀𠀁𠀂𠀃🔥🤔🐶", Match::allWithNormalizing & ~Match::emoji).first;
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0].tag, POSTag::sh);
	EXPECT_EQ(res[1].tag, POSTag::sw);

	res = kiwi.analyze(u"韓𠀀𠀁𠀂𠀃🔥🤔🐶", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 4);
	EXPECT_EQ(res[0].tag, POSTag::sh);
	EXPECT_EQ(res[1].tag, POSTag::w_emoji);
	EXPECT_EQ(res[2].tag, POSTag::w_emoji);
	EXPECT_EQ(res[3].tag, POSTag::w_emoji);
}

TEST(KiwiCpp, Script)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.analyze(u"résumé", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 1);
	EXPECT_EQ(res[0].tag, POSTag::sl);
	EXPECT_EQ(res[0].script, ScriptType::latin);

	res = kiwi.analyze(u"中国の歴史における", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 4);
	EXPECT_EQ(res[0].tag, POSTag::sh);
	EXPECT_EQ(res[0].script, ScriptType::hanja);
	EXPECT_EQ(res[1].tag, POSTag::sw);
	EXPECT_EQ(res[1].script, ScriptType::kana);
	EXPECT_EQ(res[2].tag, POSTag::sh);
	EXPECT_EQ(res[2].script, ScriptType::hanja);
	EXPECT_EQ(res[3].tag, POSTag::sw);
	EXPECT_EQ(res[3].script, ScriptType::kana);

	res = kiwi.analyze(u"👍🏻👍🏿 👨‍👩‍👦 ℹ️ ✍🏼", Match::allWithNormalizing).first;
	EXPECT_EQ(res.size(), 5);
	EXPECT_EQ(res[0].tag, POSTag::w_emoji);
	EXPECT_EQ(res[0].script, ScriptType::symbols_and_pictographs);
	EXPECT_EQ(res[0].position, 0);
	EXPECT_EQ(res[0].length, 4);
	EXPECT_EQ(res[1].tag, POSTag::w_emoji);
	EXPECT_EQ(res[1].script, ScriptType::symbols_and_pictographs);
	EXPECT_EQ(res[1].position, 4);
	EXPECT_EQ(res[1].length, 4);
	EXPECT_EQ(res[2].tag, POSTag::w_emoji);
	EXPECT_EQ(res[2].script, ScriptType::symbols_and_pictographs);
	EXPECT_EQ(res[2].position, 9);
	EXPECT_EQ(res[2].length, 8);
	EXPECT_EQ(res[3].tag, POSTag::w_emoji);
	EXPECT_EQ(res[3].script, ScriptType::letterlike_symbols);
	EXPECT_EQ(res[4].tag, POSTag::w_emoji);
	EXPECT_EQ(res[4].script, ScriptType::dingbats);
}

TEST(KiwiCpp, EmptyToken)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto testCases = {
		u"제목원래 마이 리틀 김구라 아닙니까?김구라는 한번도 안빠지고 순위도 4위하는데 계속나오네요다른분들도 그럼 교체하지 말아야지요;",
	};
	for (auto s : testCases)
	{
		auto res = kiwi.analyze(s, Match::allWithNormalizing);
		for (auto& t : res.first)
		{
			EXPECT_FALSE(t.str.empty());
		}
	}
}

TEST(KiwiCpp, Pretokenized)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto str = u"드디어패트와 매트가 2017년에 국내 개봉했다. 패트와매트는 2016년...";
	AnalyzeOption option = Match::allWithNormalizing;

	std::vector<TokenInfo> res;
	{
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 3, 9, {} },
			PretokenizedSpan{ 11, 16, {} },
			PretokenizedSpan{ 34, 39, {} },
		};

		res = kiwi.analyze(str, option, pretokenized).first;
		EXPECT_EQ(res[1].str, u"패트와 매트");
		EXPECT_EQ(res[3].str, u"2017년");
		EXPECT_EQ(res[13].str, u"2016년");
	}

	{
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 27, 29, { BasicToken{ u"페트", 0, 2, POSTag::nnb } } },
			PretokenizedSpan{ 30, 32, {} },
			PretokenizedSpan{ 21, 24, { BasicToken{ u"개봉하", 0, 3, POSTag::vv }, BasicToken{ u"었", 2, 3, POSTag::ep } }},
		};

		res = kiwi.analyze(str, option, pretokenized).first;
		EXPECT_EQ(res[7].str, u"개봉하");
		EXPECT_EQ(res[7].tag, POSTag::vv);
		EXPECT_EQ(res[7].position, 21);
		EXPECT_EQ(res[7].length, 3);
		EXPECT_EQ(res[8].str, u"었");
		EXPECT_EQ(res[8].tag, POSTag::ep);
		EXPECT_EQ(res[8].position, 23);
		EXPECT_EQ(res[8].length, 1);
		EXPECT_EQ(res[11].str, u"페트");
		EXPECT_EQ(res[11].tag, POSTag::nnb);
		EXPECT_EQ(res[13].str, u"매트");
		EXPECT_EQ(res[13].tag, POSTag::nng);
	}

	{
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 9, 10, { BasicToken{ u"가", 0, 1, POSTag::jks } } },
			PretokenizedSpan{ 16, 17, { BasicToken{ u"에", 0, 1, POSTag::jkb } } },
		};

		auto ref = kiwi.analyze(str, option).first;
		res = kiwi.analyze(str, option, pretokenized).first;
		EXPECT_EQ(res[2].tag, POSTag::jks);
		EXPECT_EQ(res[2].morph, ref[2].morph);
		EXPECT_FLOAT_EQ(res[2].score, ref[2].score);
		EXPECT_EQ(res[5].tag, POSTag::jkb);
		EXPECT_EQ(res[5].morph, ref[5].morph);
		EXPECT_FLOAT_EQ(res[5].score, ref[5].score);
	}

	{
		auto str2 = u"길을 걷다";
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 3, 4, { BasicToken{ u"걷", 0, 1, POSTag::vv } } },
		};

		auto ref = kiwi.analyze(str2, option).first;
		res = kiwi.analyze(str2, option, pretokenized).first;
		EXPECT_EQ(res[2].tag, POSTag::vvi);
		EXPECT_EQ(res[2].morph, ref[2].morph);
	}
}

TEST(KiwiCpp, PretokenizedWithTypo)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto str = u"드디어패트와 매트가 2017년에 국내 개봉했다. 패트와매트는 2016년...";
	AnalyzeOption option = Match::allWithNormalizing;
	option.typoTransformer = getDefaultPreparedTypoSet(DefaultTypoSet::basicTypoSetWithContinualAndLengthening);

	std::vector<TokenInfo> res;
	{
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 3, 9, {} },
			PretokenizedSpan{ 11, 16, {} },
			PretokenizedSpan{ 34, 39, {} },
		};

		res = kiwi.analyze(str, option, pretokenized).first;
		EXPECT_EQ(res[1].str, u"패트와 매트");
		EXPECT_EQ(res[3].str, u"2017년");
		EXPECT_EQ(res[13].str, u"2016년");
	}

	{
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 27, 29, { BasicToken{ u"페트", 0, 2, POSTag::nnb } } },
			PretokenizedSpan{ 30, 32, {} },
			PretokenizedSpan{ 21, 24, { BasicToken{ u"개봉하", 0, 3, POSTag::vv }, BasicToken{ u"었", 2, 3, POSTag::ep } }},
		};

		res = kiwi.analyze(str, option, pretokenized).first;
		EXPECT_EQ(res[7].str, u"개봉하");
		EXPECT_EQ(res[7].tag, POSTag::vv);
		EXPECT_EQ(res[7].position, 21);
		EXPECT_EQ(res[7].length, 3);
		EXPECT_EQ(res[8].str, u"었");
		EXPECT_EQ(res[8].tag, POSTag::ep);
		EXPECT_EQ(res[8].position, 23);
		EXPECT_EQ(res[8].length, 1);
		EXPECT_EQ(res[11].str, u"페트");
		EXPECT_EQ(res[11].tag, POSTag::nnb);
		EXPECT_EQ(res[13].str, u"매트");
		EXPECT_EQ(res[13].tag, POSTag::nng);
	}

	{
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 9, 10, { BasicToken{ u"가", 0, 1, POSTag::jks } } },
			PretokenizedSpan{ 16, 17, { BasicToken{ u"에", 0, 1, POSTag::jkb } } },
		};

		auto ref = kiwi.analyze(str, option).first;
		res = kiwi.analyze(str, option, pretokenized).first;
		EXPECT_EQ(res[2].tag, POSTag::jks);
		EXPECT_EQ(res[2].morph, ref[2].morph);
		EXPECT_FLOAT_EQ(res[2].score, ref[2].score);
		EXPECT_EQ(res[5].tag, POSTag::jkb);
		EXPECT_EQ(res[5].morph, ref[5].morph);
		EXPECT_FLOAT_EQ(res[5].score, ref[5].score);
	}

	{
		auto str2 = u"길을 걷다";
		std::vector<PretokenizedSpan> pretokenized = {
			PretokenizedSpan{ 3, 4, { BasicToken{ u"걷", 0, 1, POSTag::vv } } },
		};

		auto ref = kiwi.analyze(str2, option).first;
		res = kiwi.analyze(str2, option, pretokenized).first;
		EXPECT_EQ(res[2].tag, POSTag::vvi);
		EXPECT_EQ(res[2].morph, ref[2].morph);
	}
}

TEST(KiwiCpp, TagRoundTrip)
{
	for (size_t i = 0; i < (size_t)POSTag::p; ++i)
	{
		auto u8tag = tagToString((POSTag)i);
		auto u16tag = tagToKString((POSTag)i);
		EXPECT_EQ(utf16To8(u16tag), u8tag);
		auto r = toPOSTag(u16tag);
		EXPECT_EQ(r, (POSTag)i);
	}
}

TEST(KiwiCpp, UserTag)
{
	KiwiBuilder kw{ MODEL_PATH, 0, BuildOption::default_, ModelType::none, };
	EXPECT_TRUE(kw.addWord(u"사용자태그", POSTag::user0, 10.f).second);
	EXPECT_TRUE(kw.addWord(u"이것도유저", POSTag::user1, 10.f).second);
	EXPECT_TRUE(kw.addWord(u"특수한표지", POSTag::user2, 10.f).second);
	auto kiwi = kw.build();
	auto tokens = kiwi.analyze(u"사용자태그를 사용할때는 특수한표지를 넣는다. 이것도유저의 권리이다.", Match::allWithNormalizing).first;

	EXPECT_EQ(tokens[0].str, u"사용자태그");
	EXPECT_EQ(tokens[0].tag, POSTag::user0);
	EXPECT_EQ(tokens[12].str, u"이것도유저");
	EXPECT_EQ(tokens[12].tag, POSTag::user1);
	EXPECT_EQ(tokens[7].str, u"특수한표지");
	EXPECT_EQ(tokens[7].tag, POSTag::user2);
}

TEST(KiwiCpp, STagPrefix)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.analyze(u"자신있는 지역은 `후분양`으로 나올듯 싶습니다.", Match::allWithNormalizing).first;
	EXPECT_EQ(res[0].str, u"자신");
	EXPECT_EQ(res[1].str, u"있");
	EXPECT_EQ(res[2].str, u"는");
	EXPECT_EQ(res[3].str, u"지역");
	EXPECT_EQ(res[4].str, u"은");
	EXPECT_EQ(res[5].str, u"`");
}

TEST(KiwiCpp, FindMorphemesWithPrefix)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto morphemes = kiwi.findMorphemesWithPrefix(10, u"키");
	EXPECT_GT(morphemes.size(), 0);
	EXPECT_TRUE(u"키" <= morphemes[0]->getForm());
	EXPECT_TRUE(morphemes[0]->getForm() <= morphemes[1]->getForm());
	EXPECT_TRUE(morphemes[1]->getForm() <= morphemes[2]->getForm());
	EXPECT_TRUE(morphemes[2]->getForm() <= morphemes[3]->getForm());

	morphemes = kiwi.findMorphemesWithPrefix(10, u"키윜");
	EXPECT_GT(morphemes.size(), 0);

	morphemes = kiwi.findMorphemesWithPrefix(10, u"");
	EXPECT_GT(morphemes.size(), 0);
}

TEST(KiwiCpp, HSDataset)
{
	KiwiBuilder kw{ MODEL_PATH, 0, BuildOption::default_, };
	std::vector<std::string> data;
	data.emplace_back("./ModelGenerator/testHSDataset.txt");

	static constexpr size_t batchSize = 32, windowSize = 8;

	std::array<int32_t, batchSize* windowSize> in;
	std::array<int32_t, batchSize> out;
	std::array<float, batchSize> lmLProbs;
	std::array<uint32_t, batchSize> outNgramBase;
	float restLm;
	uint32_t restLmCnt;

	for (size_t w : {0, 1, 2, 4})
	{
		//std::cout << w << std::endl;
		auto dataset = kw.makeHSDataset(data, batchSize, 0, windowSize, w);
		for (size_t i = 0; i < 2; ++i)
		{
			size_t totalBatchCnt = 0, totalTokenCnt = 0, s;
			dataset.reset();
			while ((s = dataset.next(in.data(), out.data(), lmLProbs.data(), outNgramBase.data(), restLm, restLmCnt)))
			{
				EXPECT_LE(s, batchSize);
				totalTokenCnt += s;
				totalBatchCnt++;
			}
			EXPECT_TRUE(std::max(dataset.numEstimBatches(), (size_t)w) - w <= totalBatchCnt && totalBatchCnt <= dataset.numEstimBatches() + w);
			EXPECT_EQ(dataset.numTokens(), totalTokenCnt);
		}
	}

	auto tokenFilter = [](const std::u16string& form, POSTag tag)
	{
		if (isJClass(tag) || isEClass(tag)) return false;
		return true;
	};

	HSDataset trainset, devset;
	trainset = kw.makeHSDataset(data, batchSize, 0, windowSize, 1, {}, tokenFilter, {}, 0.1, false, {}, 0, {}, &devset);
	for (size_t i = 0; i < 2; ++i)
	{
		{
			size_t totalBatchCnt = 0, totalTokenCnt = 0, s;
			trainset.reset();
			while ((s = trainset.next(in.data(), out.data(), lmLProbs.data(), outNgramBase.data(), restLm, restLmCnt)))
			{
				EXPECT_LE(s, batchSize);
				totalTokenCnt += s;
				totalBatchCnt++;
			}
			EXPECT_TRUE(std::max(trainset.numEstimBatches(), (size_t)1) - 1 <= totalBatchCnt && totalBatchCnt <= trainset.numEstimBatches() + 1);
			EXPECT_EQ(trainset.numTokens(), totalTokenCnt);
		}
		{
			size_t totalBatchCnt = 0, totalTokenCnt = 0, s;
			devset.reset();
			while ((s = devset.next(in.data(), out.data(), lmLProbs.data(), outNgramBase.data(), restLm, restLmCnt)))
			{
				EXPECT_LE(s, batchSize);
				totalTokenCnt += s;
				totalBatchCnt++;
			}
			EXPECT_TRUE(std::max(devset.numEstimBatches(), (size_t)1) - 1 <= totalBatchCnt && totalBatchCnt <= devset.numEstimBatches() + 1);
			EXPECT_EQ(devset.numTokens(), totalTokenCnt);
		}
	}
}

TEST(KiwiCpp, HSDatasetUnlikelihoods)
{
	KiwiBuilder kw{ MODEL_PATH, 0, BuildOption::default_, ModelType::cong };
	std::vector<std::string> data;
	data.emplace_back("./ModelGenerator/testHSDataset.txt");

	static constexpr size_t batchSize = 32, windowSize = 8;

	std::array<int32_t, batchSize* windowSize> in, unlikelihoodIn;
	std::array<int32_t, batchSize> out, unlikelihoodOut;
	std::array<float, batchSize> lmLProbs;
	std::array<uint32_t, batchSize> outNgramBase;
	float restLm;
	uint32_t restLmCnt;

	const size_t numWorkers = 4;
	auto dataset = kw.makeHSDataset(data, batchSize, 0, windowSize, numWorkers, HSDatasetOption{ 0., 0.01, 0.12, 0.05, 0.05, 999999 });
	for (size_t i = 0; i < 2; ++i)
	{
		size_t totalBatchCnt = 0, totalTokenCnt = 0, s;
		dataset.reset();
		while (s = dataset.next(in.data(), out.data(), lmLProbs.data(), outNgramBase.data(), restLm, restLmCnt, unlikelihoodIn.data(), unlikelihoodOut.data()))
		{
			EXPECT_LE(s, batchSize);
			totalTokenCnt += s;
			totalBatchCnt++;
		}
		EXPECT_TRUE((std::max(dataset.numEstimBatches(), (size_t)numWorkers) - numWorkers) * 0.9 <= totalBatchCnt && totalBatchCnt <= (dataset.numEstimBatches() + numWorkers) * 1.1);
	}
}

TEST(KiwiCpp, SentenceBoundaryErrors)
{
	Kiwi& kiwi = reuseKiwiInstance();

	for (auto str : {
		u8"어떻게 보면 신제품에 대한 기대 이런 모멘텀들이 국내 증시의 적감의 수세를 촉발시킬 수도 있는 요인이 될 수도 있다라고 보시면 될 것 같습니다.",
		u8"관련 법령 이전에 만들어져 경사로 설치 의무 대상은 아닙니다.",
		u8"적법절차의 실질적인 내용을 침해하였는지 여부 등에 관하여 충분히 심리하지",
		u8"2023. 5. 10 주식회사 키위(이하 '회사'라 한다)의 대표이사 XXX는 저녁을 직원들에게 사주었다.",
		u8"실패할까봐",
		u8"집에 갈까 봐요",
		u8"너무 낮지 싶어요",
		u8"계속 할까 싶다",
		u8"집에 가용",
		u8"집에 갔어용",
		u8"집에 가용..",
		u8"집에 갔어용..",
		u8"bab2min.github.io/kiwipiepy",
		u8"결국 슈퍼맨 역에 D.J. 코트로나, 배트맨 역에 아미 해머가 캐스팅 되었죠.",
		u8"네이크업페이스 07. Kiss the orange 제품이예요.",
		u8"1. 1리터 초대형 캔들 2명",
		u8"2017. 12. 11. 공백 1차 심의결과가 종합적 검토를 위해 보류로 의결됨",
		u8"2017.12.11. 1차 심의결과가 종합적 검토를 위해 보류로 의결됨",
		u8"짤막 T.M.I : 이 그릴이 4천만원......",
		u8"Dr. Octo가 진행한다.",
		u8"지도부가 어떻게 구성되느냐에 따라",
		u8"좋겠다하는데",
		u8"최다 우승팀이 되었다(3번 우승).",
		u8"최고 기록이었다.[4][5]",
		u8"이렇게 불편함 없이 드실 수 있어요.",
		//u8"이건 소설인가 실제인가라는 문구를 보고",
		u8"이거 불편함.",
		})
	{
		TokenResult res;
		std::vector<std::pair<size_t, size_t>> sentRanges = kiwi.splitIntoSents(str, Match::allWithNormalizing, &res);
		EXPECT_EQ(sentRanges.size(), 1);
		if (sentRanges.size() > 1)
		{
			kiwi.splitIntoSents(str, Match::allWithNormalizing, &res);
			for (auto& r : sentRanges)
			{
				std::cerr << std::string{ &str[r.first], r.second - r.first } << std::endl;
			}
			std::cerr << std::endl;
		}
	}
}

TEST(KiwiCpp, SentenceBoundaryWithOrderedBullet)
{
	Kiwi& kiwi = reuseKiwiInstance();

	for (auto str : {
		u"가. 스카이 초이스는 편당 요금을 지불",
		u"나. 스카이 초이스는 편당 요금을 지불",
		u"다. 스카이 초이스는 편당 요금을 지불",
		u"라. 스카이 초이스는 편당 요금을 지불",
		u"ㄱ. 스카이 초이스는 편당 요금을 지불",
		u"ㄴ. 스카이 초이스는 편당 요금을 지불",
		u"가) 스카이 초이스는 편당 요금을 지불",
		u"나) 스카이 초이스는 편당 요금을 지불",
		u"1) 스카이 초이스는 편당 요금을 지불",
		u"2) 스카이 초이스는 편당 요금을 지불",
		u"ㄱ) 스카이 초이스는 편당 요금을 지불",
		u"ㄴ) 스카이 초이스는 편당 요금을 지불",
		})
	{
		TokenResult res;
		std::vector<std::pair<size_t, size_t>> sentRanges = kiwi.splitIntoSents(str, Match::allWithNormalizing, &res);
		EXPECT_EQ(res.first[0].tag, POSTag::sb);
		EXPECT_EQ(sentRanges.size(), 1);
		if (sentRanges.size() > 1)
		{
			for (auto& r : sentRanges)
			{
				std::cerr << std::u16string{ &str[r.first], r.second - r.first } << std::endl;
			}
			std::cerr << std::endl;
		}
	}

	for (auto str : {
		u"집에가.",
		u"하지마.",
		u"이거사.",
		u"민철아.",
		u"자자.",
		u"따뜻한 차.",
		u"(집에가)",
		u"(하지마)",
		u"(이거사)",
		u"(민철아)",
		u"(자자)",
		u"(따뜻한 차)",
	})
	{
		auto tokens = kiwi.analyze(str, Match::allWithNormalizing).first;
		EXPECT_NE(tokens.back().tag, POSTag::sb);
	}

	for (auto str : {
		u"가. 편당 요금을 지불한다.  나. 편당 요금을 지불한다.  다. 편당 요금을 지불한다.",
		u"가) 편당 요금을 지불한다.  나) 편당 요금을 지불한다.  다) 편당 요금을 지불한다.",
		u"1) 편당 요금을 지불한다.  2) 편당 요금을 지불한다.  3) 편당 요금을 지불한다.",
		//u"가. 편당 요금을 지불한다  나. 편당 요금을 지불한다  다. 편당 요금을 지불한다",
		u"가) 편당 요금을 지불한다  나) 편당 요금을 지불한다  다) 편당 요금을 지불한다",
		//u"1) 편당 요금을 지불한다  2) 편당 요금을 지불한다  3) 편당 요금을 지불한다",
		u"가. 편당 요금을 지불  나. 편당 요금을 지불  다. 편당 요금을 지불",
		u"가) 편당 요금을 지불  나) 편당 요금을 지불  다) 편당 요금을 지불",
		u"1) 편당 요금을 지불  2) 편당 요금을 지불  3) 편당 요금을 지불",
	})
	{
		auto res = kiwi.analyze(str, 5, Match::allWithNormalizing);
		auto& tokens = res[0].first;
		std::vector<size_t> sb;
		for (auto& t : tokens)
		{
			if (t.tag == POSTag::sb) sb.emplace_back(&t - tokens.data());
		}
		EXPECT_EQ(sb.size(), 3);
		
		if (sb.size() == 3)
		{
			EXPECT_EQ(tokens[sb[0]].pairedToken, sb[1]);
			EXPECT_EQ(tokens[sb[1]].pairedToken, sb[2]);
			EXPECT_EQ(tokens[sb[2]].pairedToken, (uint32_t)-1);
		}
	}
}


TEST(KiwiCpp, FalsePositiveSB)
{
	Kiwi& kiwi = reuseKiwiInstance();

	for (auto str : {
		u"하다. 가운데 비닐을 요렇게 벗겨주고요!",
		u"자, 이것이 `열쇠`다.`` 암상인 앞의 캡슐이 열리며 그곳에서 새로운 파워업 아이템 `쿠나이`를 얻을 수 있다.",
		u"기계는 명령만 듣는다.라는 생각이 이제 사람들에게 완전히 정착이 되었다는 상황인데, 그럴싸하죠",
		u"후반 빨간 모아이들의 공격은 엄청나게 거세다.상하로 점프하며 이온링을 발사하는 중보스 모아이상들.",
		u"또 전화세, 전기세, 보험료등 월 정기지출도 지출통장으로 바꾼 다. 셋째, 물건을 살땐 무조건 카드로 긁는다.",
		u"에티하드항공이 최고의 이코노미 클래스 상을 두 번째로 받은 해는 2020년이다. 이전에는 2012년과 2013년에 최고의 이코노미 클래스 상을 수상한 적이 있어요.",
		u"은미희는 새로운 삶을 시작하기로 결심한 후 6년간의 습작기간을 거쳐 1996년 단편 《누에는 고치 속에서 무슨 꿈을 꾸는가》로 전남일보 신춘문예 당선됐다. 따라서 은미희가 《누에는 고치 속에서 무슨 꿈을 꾸는가》로 상을 받기 전에 연습 삼아 소설을 집필했던 기간은 6년이다.",
		u"도서전에서 관람객의 관심을 받을 것으로 예상되는 프로그램으로는 '인문학 아카데미'가 있어요. 이 프로그램에서는 유시민 전 의원, 광고인 박웅현 씨 등이 문화 역사 미학 등 다양한 분야에 대해 강의할 예정이다. 또한, '북 멘토 프로그램'도 이어져요. 이 프로그램에서는 각 분야 전문가들이 경험과 노하우를 전수해 주는 프로그램으로, 시 창작(이정록 시인), 번역(강주헌 번역가), 북 디자인(오진경 북디자이너) 등의 분야에서 멘토링이 이뤄져요.",
	})
	{
		auto res = kiwi.analyze(str, 1, Match::allWithNormalizing);
		auto tokens = res[0].first;
		auto sbCount = std::count_if(tokens.begin(), tokens.end(), [](const TokenInfo& t)
		{
			return t.tag == POSTag::sb;
		});
		EXPECT_EQ(sbCount, 0);
		kiwi.analyze(str, 10, Match::allWithNormalizing);
	}
}

TEST(KiwiCpp, SplitByPolarity)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto ret = kiwi.analyze(u"흘렀다", Match::all);
	EXPECT_EQ(ret.first.size(), 3);
	ret = kiwi.analyze(u"전류가 흘렀다", Match::all);
	EXPECT_EQ(ret.first.size(), 5);
	ret = kiwi.analyze(u"전류가흘렀다", Match::all);
	EXPECT_EQ(ret.first.size(), 5);
}

TEST(KiwiCpp, SpaceBetweenChunk)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto ret = kiwi.analyze(u"다 갔다.", Match::allWithNormalizing);
	EXPECT_EQ(ret.first[0].str, u"다");
}

TEST(KiwiCpp, SpaceTolerant)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto str = u"띄 어 쓰 기 문 제 가 있 습 니 다";
	auto tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_GE(tokens.size(), 11);

	auto config = kiwi.getGlobalConfig();
	config.spacePenalty = 3;
	config.spaceTolerance = 1;
	kiwi.setGlobalConfig(config);
	tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_LE(tokens.size(), 11);

	config.spaceTolerance = 2;
	kiwi.setGlobalConfig(config);
	tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_EQ(tokens.size(), 8);

	config.spaceTolerance = 3;
	kiwi.setGlobalConfig(config);
	tokens = kiwi.analyze(str, Match::all).first;
	EXPECT_EQ(tokens.size(), 5);

	EXPECT_EQ(
		kiwi.analyze(u"띄 어 쓰 기", Match::all).second,
		kiwi.analyze(u"띄     어 쓰 기", Match::all).second
	);

	config.spaceTolerance = 0;
	config.spacePenalty = 7;
	kiwi.setGlobalConfig(config);
}

TEST(KiwiCpp, MultiWordDictionary)
{
	auto& kiwi = reuseKiwiInstance();
	const auto text = u"밀리언 달러 베이비랑 더 웨이 백 중 뭐가 더 재밌었어?";

	auto res = kiwi.analyze(text, Match::allWithNormalizing).first;
	EXPECT_EQ(res[0].str, u"밀리언 달러 베이비");
	EXPECT_EQ(res[0].tag, POSTag::nnp);

	EXPECT_EQ(res[2].str, u"더 웨이 백");
	EXPECT_EQ(res[2].tag, POSTag::nnp);

	auto kiwi2 = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_ & ~BuildOption::loadMultiDict, }.build();
	res = kiwi2.analyze(text, Match::allWithNormalizing).first;
	EXPECT_NE(res[0].str, u"밀리언 달러 베이비");
}

TEST(KiwiCpp, WordsWithSpaces)
{
	KiwiBuilder kw{ MODEL_PATH, 0, BuildOption::default_ & ~BuildOption::loadMultiDict, };
	EXPECT_TRUE(kw.addWord(u"대학생 선교회", POSTag::nnp, 0.0).second);
	Kiwi kiwi = kw.build();

	auto res1 = kiwi.analyze(u"대학생 선교회", Match::all);
	auto res2 = kiwi.analyze(u"대학생선교회", Match::all);
	auto res3 = kiwi.analyze(u"대학생 \t 선교회", Match::all);
	auto res4 = kiwi.analyze(u"대 학생선교회", Match::all);
	auto res5 = kiwi.analyze(u"대 학생 선교회", Match::all);
	auto res6 = kiwi.analyze(u"대학 생선 교회", Match::all);
	EXPECT_EQ(res1.first.size(), 1);
	EXPECT_EQ(res2.first.size(), 1);
	EXPECT_EQ(res3.first.size(), 1);
	EXPECT_NE(res4.first.size(), 1);
	EXPECT_NE(res5.first.size(), 1);
	EXPECT_NE(res6.first.size(), 1);
	
	EXPECT_EQ(res1.first[0].str, u"대학생 선교회");
	EXPECT_EQ(res2.first[0].str, u"대학생 선교회");
	EXPECT_EQ(res3.first[0].str, u"대학생 선교회");
	EXPECT_NE(res4.first[0].str, u"대학생 선교회");
	EXPECT_NE(res5.first[0].str, u"대학생 선교회");
	EXPECT_NE(res6.first[0].str, u"대학생 선교회");
	
	EXPECT_EQ(res1.first[0].tag, POSTag::nnp);
	EXPECT_EQ(res2.first[0].tag, POSTag::nnp);
	EXPECT_EQ(res3.first[0].tag, POSTag::nnp);
	EXPECT_EQ(res1.second, res2.second);
	EXPECT_EQ(res1.second, res3.second);

	auto config = kiwi.getGlobalConfig();
	config.spaceTolerance = 1;
	kiwi.setGlobalConfig(config);
	res1 = kiwi.analyze(u"대학생 선교회", Match::all);
	res2 = kiwi.analyze(u"대학생선교회", Match::all);
	res3 = kiwi.analyze(u"대학생 \t 선교회", Match::all);
	res4 = kiwi.analyze(u"대 학생선교회", Match::all);
	res5 = kiwi.analyze(u"대 학생 선교회", Match::all);
	res6 = kiwi.analyze(u"대학 생선 교회", Match::all);

	EXPECT_EQ(res1.first.size(), 1);
	EXPECT_EQ(res2.first.size(), 1);
	EXPECT_EQ(res3.first.size(), 1);
	EXPECT_EQ(res4.first.size(), 1);
	EXPECT_EQ(res5.first.size(), 1);
	EXPECT_NE(res6.first.size(), 1);

	EXPECT_EQ(res1.first[0].str, u"대학생 선교회");
	EXPECT_EQ(res2.first[0].str, u"대학생 선교회");
	EXPECT_EQ(res3.first[0].str, u"대학생 선교회");
	EXPECT_EQ(res4.first[0].str, u"대학생 선교회");
	EXPECT_EQ(res5.first[0].str, u"대학생 선교회");
	EXPECT_NE(res6.first[0].str, u"대학생 선교회");

	EXPECT_LT(res4.second, res1.second);
	EXPECT_LT(res5.second, res1.second);

	EXPECT_TRUE(kw.addWord(u"농협 용인 육가공 공장", POSTag::nnp, 0.0).second);
	kiwi = kw.build();

	res1 = kiwi.analyze(u"농협 용인 육가공 공장", Match::all);
	res2 = kiwi.analyze(u"농협용인 육가공 공장", Match::all);
	res3 = kiwi.analyze(u"농협 용인육가공 공장", Match::all);
	res4 = kiwi.analyze(u"농협 용인 육가공공장", Match::all);
	res5 = kiwi.analyze(u"농협용인육가공공장", Match::all);
	res6 = kiwi.analyze(u"농협용 인육 가공 공장", Match::all);

	EXPECT_EQ(res1.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_EQ(res2.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_EQ(res3.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_EQ(res4.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_EQ(res5.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_NE(res6.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_EQ(res1.second, res2.second);
	EXPECT_EQ(res1.second, res3.second);
	EXPECT_EQ(res1.second, res4.second);
	EXPECT_EQ(res1.second, res5.second);

	config = kiwi.getGlobalConfig();
	config.spaceTolerance = 1;
	kiwi.setGlobalConfig(config);
	res2 = kiwi.analyze(u"농협용인육 가공 공장", Match::all);
	res3 = kiwi.analyze(u"농협용 인육 가공 공장", Match::all);
	res4 = kiwi.analyze(u"농협용 인육 가공공장", Match::all);
	EXPECT_EQ(res2.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_NE(res3.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_NE(res4.first[0].str, u"농협 용인 육가공 공장");

	config.spaceTolerance = 2;
	kiwi.setGlobalConfig(config);
	res3 = kiwi.analyze(u"농협용 인육 가공 공장", Match::all);
	res4 = kiwi.analyze(u"농협용 인육 가공공장", Match::all);
	EXPECT_EQ(res3.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_EQ(res4.first[0].str, u"농협 용인 육가공 공장");

	res5 = kiwi.analyze(u"농협용\n인육 가공\n공장에서", Match::all);
	EXPECT_EQ(res5.first[0].str, u"농협 용인 육가공 공장");
	EXPECT_EQ(res5.first[0].lineNumber, 0);
	EXPECT_EQ(res5.first[1].lineNumber, 2);
}

TEST(KiwiCpp, MultiDict)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.analyze(u"프렌치카페 로스터리 크리스마스에디션 인증샷", Match::all).first;
	for (auto& r : res)
	{
		EXPECT_NE(r.str, u"리 크리스마스");
	}

	res = kiwi.analyze(u"추첨이벤트 2018년 리빙디자인페어 행사기간", Match::all).first;
	for (auto& r : res)
	{
		EXPECT_NE(r.str, u"리 빙");
	}
}

TEST(KiwiCpp, Pattern)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto tokens = kiwi.analyze(u"123.4567", Match::none).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123.4567.", Match::none).first;
	EXPECT_EQ(tokens.size(), 4);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);
	EXPECT_EQ(tokens[1].tag, POSTag::sf);

	tokens = kiwi.analyze(u"123,456.", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);
	EXPECT_EQ(tokens[1].tag, POSTag::sf);

	tokens = kiwi.analyze(u"123.", Match::none).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"1,234,567", Match::none).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123,", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);
	EXPECT_EQ(tokens[1].tag, POSTag::sp);

	tokens = kiwi.analyze(u"123,456.789", Match::none).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123,456.789hz", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"123,456.789이다", Match::none).first;
	EXPECT_EQ(tokens.size(), 3);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"1.2%", Match::none).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"12:34에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"12:3:456:7890에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"12.34에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::sn);

	tokens = kiwi.analyze(u"12.34.0.1에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"2001/01/02에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"2001. 01. 02에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);

	tokens = kiwi.analyze(u"2001. 01. 02. 에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);
	EXPECT_EQ(tokens[0].str.back(), u'.');

	tokens = kiwi.analyze(u"010-1234-5678에", Match::all).first;
	EXPECT_EQ(tokens.size(), 2);
	EXPECT_EQ(tokens[0].tag, POSTag::w_serial);
}

TEST(KiwiCpp, BuilderAddWords)
{
	KiwiBuilder builder{ MODEL_PATH };
	EXPECT_TRUE(builder.addWord(KWORD, POSTag::nnp, 0.0).second);
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

TEST(KiwiCpp, AnalyzeSBG)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::none, ModelType::none }.build();
	Kiwi kiwiSbg = KiwiBuilder{ MODEL_PATH, 0, BuildOption::none, ModelType::largest }.build();
	kiwiSbg.analyze(TEST_SENT, Match::all);

	auto res = kiwi.analyze(u"이 번호로 전화를 이따가 꼭 반드시 걸어.", 3, kiwi::Match::allWithNormalizing);
	auto resSbg = kiwiSbg.analyze(u"이 번호로 전화를 이따가 꼭 반드시 걸어.", 3, kiwi::Match::allWithNormalizing);
	EXPECT_EQ(resSbg[0].first.size(), 11);
	EXPECT_EQ(resSbg[0].first[8].str, u"걸");
}

TEST(KiwiCpp, AnalyzeCong)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::none, ModelType::congGlobal }.build();
	kiwi.analyze(TEST_SENT, Match::all);

	auto res = kiwi.analyze(u"이 번호로 전화를 이따가 꼭 반드시 걸어.", 3, kiwi::Match::allWithNormalizing);
	EXPECT_EQ(res[0].first.size(), 11);
	EXPECT_EQ(res[0].first[8].str, u"걸");
}

TEST(KiwiCpp, CoNgramFunctions)
{
	if (sizeof(void*) != 8)
	{
		std::cerr << "This test is only available in 64-bit mode" << std::endl;
		return;
	}

	Kiwi kiwiF = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, ModelType::congGlobalFp32 }.build();
	Kiwi kiwiQ = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, ModelType::congGlobal }.build();
	auto lmF = dynamic_cast<const lm::CoNgramModelBase*>(kiwiF.getLangModel());
	auto lmQ = dynamic_cast<const lm::CoNgramModelBase*>(kiwiQ.getLangModel());

	const size_t vocabId = kiwiF.findMorphemeId(u"언어", POSTag::nng);
	const size_t candId = kiwiF.findMorphemeId(u"국어", POSTag::nng);
	const uint32_t vocabs[3] = {
		(uint32_t)kiwiF.findMorphemeId(u"오늘", POSTag::mag),
		(uint32_t)kiwiF.findMorphemeId(u"점심", POSTag::nng),
		(uint32_t)kiwiF.findMorphemeId(u"은", POSTag::jx),
	};
	const uint32_t contextId = lmF->toContextId(&vocabs[0], 3);
	const uint32_t bgContextId = lmF->toContextId(&vocabs[2], 1);
	std::array<std::pair<uint32_t, float>, 10> resultF, resultQ;

	auto contextMap = lmF->getContextWordMap();
	EXPECT_EQ(lmF->mostSimilarWords(vocabId, resultF.size(), resultF.data()), resultF.size());
	EXPECT_TRUE(std::any_of(resultF.begin(), resultF.end(), [&](const auto& p) { return p.first == candId; }));
	float simF = lmF->wordSimilarity(vocabId, resultF[0].first);
	EXPECT_FLOAT_EQ(simF, resultF[0].second);

	EXPECT_EQ(lmQ->mostSimilarWords(vocabId, resultQ.size(), resultQ.data()), resultQ.size());
	EXPECT_TRUE(std::any_of(resultQ.begin(), resultQ.end(), [&](const auto& p) { return p.first == candId; }));
	float simQ = lmQ->wordSimilarity(vocabId, resultQ[0].first);
	EXPECT_FLOAT_EQ(simQ, resultQ[0].second);

	EXPECT_EQ(lmF->mostSimilarContexts(contextId, resultF.size(), resultF.data()), resultF.size());
	simF = lmF->contextSimilarity(contextId, resultF[0].first);
	EXPECT_FLOAT_EQ(simF, resultF[0].second);

	EXPECT_EQ(lmQ->mostSimilarContexts(contextId, resultQ.size(), resultQ.data()), resultQ.size());
	simQ = lmQ->contextSimilarity(contextId, resultQ[0].first);
	EXPECT_FLOAT_EQ(simQ, resultQ[0].second);

	EXPECT_EQ(lmF->predictWordsFromContextDiff(contextId, bgContextId, 0.5, resultF.size(), resultF.data()), resultF.size());

	EXPECT_EQ(lmF->predictWordsFromContext(contextId, resultF.size(), resultF.data()), resultF.size());
	EXPECT_EQ(lmQ->predictWordsFromContext(contextId, resultQ.size(), resultQ.data()), resultQ.size());
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

TEST(KiwiCpp, NormalizeCoda) 
{ 
	Kiwi& kiwi = reuseKiwiInstance(); 
	TokenResult res = kiwi.analyze(u"키윜ㅋㅋ", Match::allWithNormalizing | Match::oovChrModel); 
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㅋㅋㅋ" });
	res = kiwi.analyze(u"키윟ㅎ", Match::allWithNormalizing | Match::oovChrModel);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㅎㅎ" });
	res = kiwi.analyze(u"키윅ㄱ", Match::allWithNormalizing | Match::oovChrModel);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㄱㄱ" });
	res = kiwi.analyze(u"키윈ㄴㄴ", Match::allWithNormalizing | Match::oovChrModel);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㄴㄴㄴ" });
	res = kiwi.analyze(u"키윊ㅎㅎ", Match::allWithNormalizing | Match::oovChrModel);
	EXPECT_EQ(res.first.back().str, std::u16string{ u"ㅎㅎ" });
	res = kiwi.analyze(u"키윍ㄱㄱ", Match::allWithNormalizing | Match::oovChrModel);
	EXPECT_EQ(res.first.back().str, std::u16string{u"ㄱㄱ"});
}

TEST(KiwiCpp, ZCoda)
{
	Kiwi& kiwi = reuseKiwiInstance();
	{
		std::initializer_list<std::pair<const char16_t*, const char16_t*>> testCases = {
			{ u"그랬어!", u"그랬엏!",},
			{ u"아니야!", u"아니얍!",},
			{ u"나도!", u"나돜!",},
			{ u"너무해!", u"너무행!",},
			{ u"그래요!", u"그래용!",},
		};
		for (auto s : testCases)
		{
			auto res1 = kiwi.analyze(s.first, Match::allWithNormalizing | Match::oovChrFreqModel);
			auto res2 = kiwi.analyze(s.second, Match::allWithNormalizing | Match::oovChrFreqModel);
			auto res3 = kiwi.analyze(s.second, (Match::allWithNormalizing | Match::oovChrFreqModel) & ~Match::zCoda);
			EXPECT_GE(res1.second - kiwi.getGlobalConfig().typoCostWeight, res2.second);
			EXPECT_GT(res2.second, res3.second);
			EXPECT_EQ(res2.first[res2.first.size() - 2].tag, POSTag::z_coda);
		}
	}
}

TEST(KiwiCpp, ZSiot)
{
	Kiwi& kiwi = reuseKiwiInstance();
	KiwiConfig config = kiwi.getGlobalConfig();
	config.oovRuleScale = 6;
	auto resSplit = kiwi.analyze(u"찰랑찰랑한 머릿결과 볼륨감", Match::allWithNormalizing | Match::splitSaisiot, {}, config);
	EXPECT_EQ(resSplit.first.size(), 8);
	EXPECT_EQ(resSplit.first[3].str, u"머리");
	EXPECT_EQ(resSplit.first[4].tag, POSTag::z_siot);
	EXPECT_EQ(resSplit.first[5].str, u"결");
	
	for (auto s : {u"하굣길", u"만둣국", u"나뭇잎", u"세숫물", u"고춧가루", u"시곗바늘", u"사글셋방"})
	{
		auto resNone = kiwi.analyze(s, Match::allWithNormalizing, {}, config);
		auto resSplit = kiwi.analyze(s, Match::allWithNormalizing | Match::splitSaisiot, {}, config);
		auto resMerge = kiwi.analyze(s, Match::allWithNormalizing | Match::mergeSaisiot, {}, config);
		EXPECT_FALSE(std::any_of(resNone.first.begin(), resNone.first.end(), [](const TokenInfo& token) { return token.tag == POSTag::z_siot; }));
		EXPECT_EQ(resSplit.first.size(), 3);
		EXPECT_EQ(resSplit.first[0].tag, POSTag::nng);
		EXPECT_EQ(resSplit.first[1].tag, POSTag::z_siot);
		EXPECT_EQ(resSplit.first[2].tag, POSTag::nng);
		EXPECT_EQ(resMerge.first.size(), 1);
		EXPECT_EQ(resMerge.first[0].tag, POSTag::nng);
	}

	for (auto s : {u"발렛 파킹", u"미닛"})
	{
		auto resNone = kiwi.analyze(s, Match::allWithNormalizing, {}, config);
		auto resSplit = kiwi.analyze(s, Match::allWithNormalizing | Match::splitSaisiot, {}, config);
		auto resMerge = kiwi.analyze(s, Match::allWithNormalizing | Match::mergeSaisiot, {}, config);
		EXPECT_EQ(resNone.second, resSplit.second);
		EXPECT_EQ(resNone.second, resMerge.second);
		EXPECT_FALSE(std::any_of(resSplit.first.begin(), resSplit.first.end(), [](const TokenInfo& token) { return token.tag == POSTag::z_siot; }));
	}
}

TEST(KiwiCpp, ZSiotWithTypo)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, }.build();
	AnalyzeOption option;
	option.typoTransformer = getDefaultPreparedTypoSet(DefaultTypoSet::basicTypoSetWithContinual);
	KiwiConfig config = kiwi.getGlobalConfig();
	config.oovRuleScale = 6;
	for (auto s : { u"하굣길", u"만둣국", u"나뭇잎", u"세숫물", u"고춧가루", u"시곗바늘", u"사글셋방" })
	{
		auto resNone = kiwi.analyze(s, option.withMatch(Match::allWithNormalizing), {}, config);
		auto resSplit = kiwi.analyze(s, option.withMatch(Match::allWithNormalizing | Match::splitSaisiot), {}, config);
		auto resMerge = kiwi.analyze(s, option.withMatch(Match::allWithNormalizing | Match::mergeSaisiot), {}, config);
		EXPECT_FALSE(std::any_of(resNone.first.begin(), resNone.first.end(), [](const TokenInfo& token) { return token.tag == POSTag::z_siot; }));
		EXPECT_EQ(resSplit.first.size(), 3);
		EXPECT_EQ(resSplit.first[0].tag, POSTag::nng);
		EXPECT_EQ(resSplit.first[1].tag, POSTag::z_siot);
		EXPECT_EQ(resSplit.first[2].tag, POSTag::nng);
		EXPECT_EQ(resMerge.first.size(), 1);
		EXPECT_EQ(resMerge.first[0].tag, POSTag::nng);
	}

	for (auto s : { u"발렛 파킹", u"미닛" })
	{
		auto resNone = kiwi.analyze(s, option.withMatch(Match::allWithNormalizing), {}, config);
		auto resSplit = kiwi.analyze(s, option.withMatch(Match::allWithNormalizing | Match::splitSaisiot), {}, config);
		auto resMerge = kiwi.analyze(s, option.withMatch(Match::allWithNormalizing | Match::mergeSaisiot), {}, config);
		EXPECT_EQ(resNone.second, resSplit.second);
		EXPECT_EQ(resNone.second, resMerge.second);
		EXPECT_FALSE(std::any_of(resSplit.first.begin(), resSplit.first.end(), [](const TokenInfo& token) { return token.tag == POSTag::z_siot; }));
	}
}

TEST(KiwiCpp, AnalyzeWithWordPosition)
{
	std::u16string testSentence = u"나 정말 배불렄ㅋㅋ"; 
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::none }.build();
	TokenResult tokenResult = kiwi.analyze(testSentence, Match::all);
	std::vector<TokenInfo> tokenInfoList = tokenResult.first;

	ASSERT_GE(tokenInfoList.size(), 4);
	EXPECT_EQ(tokenInfoList[0].wordPosition, 0);
	EXPECT_EQ(tokenInfoList[1].wordPosition, 1);
	EXPECT_EQ(tokenInfoList[2].wordPosition, 2);
	EXPECT_EQ(tokenInfoList[3].wordPosition, 2);
}

TEST(KiwiCpp, Issue57_BuilderAddWord)
{
	{
		KiwiBuilder builder{ MODEL_PATH };
		builder.addWord(u"울트라리스크", POSTag::nnp, 3.0);
		builder.addWord(u"파일즈", POSTag::nnp, 0.0);
		Kiwi kiwi = builder.build();
		TokenResult res = kiwi.analyze(u"울트라리스크가 뭐야?", Match::all);
		EXPECT_EQ(res.first[0].str, std::u16string{ u"울트라리스크" });
	}

	{
		KiwiBuilder builder{ MODEL_PATH };
		builder.addWord(u"파일즈", POSTag::nnp, 0.0);
		builder.addWord(u"울트라리스크", POSTag::nnp, 3.0);
		Kiwi kiwi = builder.build();
		TokenResult res = kiwi.analyze(u"울트라리스크가 뭐야?", Match::all);
		EXPECT_EQ(res.first[0].str, std::u16string{ u"울트라리스크" });
	}
}

TEST(KiwiCpp, PositionAndLength)
{
	Kiwi& kiwi = reuseKiwiInstance();

	{
		auto tokens = kiwi.analyze(u"자랑했던", Match::all).first;
		ASSERT_GE(tokens.size(), 4);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 2);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 2);
		EXPECT_EQ(tokens[2].length, 1);
		EXPECT_EQ(tokens[3].position, 3);
		EXPECT_EQ(tokens[3].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"이르렀다", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 2);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 3);
		EXPECT_EQ(tokens[2].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"일렀다", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 1);
		EXPECT_EQ(tokens[1].position, 1);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 2);
		EXPECT_EQ(tokens[2].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"다다랐다", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 3);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 1);
		EXPECT_EQ(tokens[2].position, 3);
		EXPECT_EQ(tokens[2].length, 1);
	}
	{
		auto tokens = kiwi.analyze(u"바다다!", Match::all).first;
		ASSERT_GE(tokens.size(), 3);
		EXPECT_EQ(tokens[0].position, 0);
		EXPECT_EQ(tokens[0].length, 2);
		EXPECT_EQ(tokens[1].position, 2);
		EXPECT_EQ(tokens[1].length, 0);
		EXPECT_EQ(tokens[2].position, 2);
		EXPECT_EQ(tokens[2].length, 1);
	}
}

TEST(KiwiCpp, Issue71_SentenceSplit_u16)
{
	Kiwi& kiwi = reuseKiwiInstance();
	
	std::u16string str = u"다녀온 후기\n\n<강남 토끼정에 다녀왔습니다.> 음식도 맛있었어요 다만 역시 토끼정 본점 답죠?ㅎㅅㅎ 그 맛이 크으.. 아주 맛있었음...! ^^";
	std::vector<std::pair<size_t, size_t>> sentRanges = kiwi.splitIntoSents(str);
	std::vector<std::u16string> sents;
	for (auto& p : sentRanges)
	{
		sents.emplace_back(str.substr(p.first, p.second - p.first));
	}

	ASSERT_GE(sents.size(), 6);
	EXPECT_EQ(sents[0], u"다녀온 후기");
	EXPECT_EQ(sents[1], u"<강남 토끼정에 다녀왔습니다.>");
	EXPECT_EQ(sents[2], u"음식도 맛있었어요");
	EXPECT_EQ(sents[3], u"다만 역시 토끼정 본점 답죠?ㅎㅅㅎ");
	EXPECT_EQ(sents[4], u"그 맛이 크으..");
	EXPECT_EQ(sents[5], u"아주 맛있었음...! ^^");
}

TEST(KiwiCpp, Issue71_SentenceSplit_u8)
{
	Kiwi& kiwi = reuseKiwiInstance();

	std::string str = u8"다녀온 후기\n\n<강남 토끼정에 다녀왔습니다.> 음식도 맛있었어요 다만 역시 토끼정 본점 답죠?ㅎㅅㅎ 그 맛이 크으.. 아주 맛있었음...! ^^";
	std::vector<std::pair<size_t, size_t>> sentRanges = kiwi.splitIntoSents(str);
	std::vector<std::string> sents;
	for (auto& p : sentRanges)
	{
		sents.emplace_back(str.substr(p.first, p.second - p.first));
	}

	ASSERT_GE(sents.size(), 6);
	EXPECT_EQ(sents[0], u8"다녀온 후기");
	EXPECT_EQ(sents[1], u8"<강남 토끼정에 다녀왔습니다.>");
	EXPECT_EQ(sents[2], u8"음식도 맛있었어요");
	EXPECT_EQ(sents[3], u8"다만 역시 토끼정 본점 답죠?ㅎㅅㅎ");
	EXPECT_EQ(sents[4], u8"그 맛이 크으..");
	EXPECT_EQ(sents[5], u8"아주 맛있었음...! ^^");
}

TEST(KiwiCpp, IssueP111_SentenceSplitError)
{
	const char16_t* text = uR"(그래서 정말 대충 먹고 짜증 머리 끝까지 찬 상태로 업무 보고아빠랑 통화하다가 싸우고jh가 장난쳐서 서운함 느 끼고(이건 자기 전에 대화로 화해함하필 다른거 때문에 기분이 안 좋을 때, 평소와 비슷한(?) 장난을 쳤음에도, 그리고 내가 예 민한 부분(?)을 가지고 장난을 쳐서서운함이 폭발을 했다. 이참에 얘기하다가 jh의 서운한 부분도 듣고예전에도 얘기했던 부분인데 내가 좀  더 노력하기로 할게디테일 능력을 키우자배려심을 키우자)쨋든 여러모로 컨디션이 안 좋았던 날이었다퇴원을 9일차에 못하는 이 유는허리가 계속 아프다니까 엄마가 혹시 모르니까 ct를 찍어보는게 어떠냐고 해서주치의 원장님께 여쭤봤더니 진료  보고 협력 병원에 예약을 잡아주시겠다고 했다근데 협력병원에 문의해보니 가장 빠른게 다음주라고 해서난 당장 내일 받아보고 싶은데그래 서 엄마랑 다른 병원으로 가보자고 해서 의뢰서를 받았다의뢰서는 mri 의뢰에 대한 내용이었다어쨋든 사고나고 바로 갔 던 병원 이 ct도 찍고 mri도 찍어서 거기에 가야겠다 하고 다음날 오전 외출증을 끊었다코로나때문에 외출도 금지이기 때문이러한 이유로 mri 촬영까지 하는데 퇴원은 이르지 않냐고경과 좀 지켜보자는 엄마의 의견으로 인해 퇴원보류그리고 외출증 끊는 김에 토욜에 접수해놓은 시험보러 잠시 외출 가능한지도 물어봤는데원장님 허락 있어야한다고 해서 또 원장님 뵈러 가기여쭤봤더니입 원한 사람이 시험을 본다는게 상황이 웃기지 않겠냐고기록도 남는건데그러셔서그냥 시험 취소하기로tsc 시험취소 다행히전액 환 불이 됐다다음 달에 봐야겠네하하하하바깥 세상에 나가는 날이 미뤄져서 창밖을 한참을 봤다나도 일상생활want오늘도 난 참 감정적인 사람이구나를 느끼고일상의 소중함을 느꼈다9일차는 연차이기에 다소 가벼운 마음으로 하루를 시작했다그리고 mri를 찍으러 외출을 해야하기에외출한다는 생각에 살짝 들떴다아빠가 아침 일찍 온다고 해서 아침 먹고 바로 내려갔다아침은왜 자꾸 생선 반찬을 주실까숭늉 맛있어생선은 싫어이제 그만mri 썰은 다소 길면 길다결론은 mri 못 찍었다.병원 오픈하자마자 접수하러 가서 타병원에 서 왔고 의뢰서 받아왔다라고 하며 의뢰서를 보여줌에도 불구하고저희는 mri의 경우 이 병원 의사와 2주 이상 진료를 보고, 소견이 있어야 가능합니다. 저희 병원에선 의뢰서를 가져와도 못해드려요. 다른 병원으로 가세요. 죄송합니다저번 수납 접수때도 되 게 차갑고 불친절했던 사람으로 기억하는데 너무 단칼에 저렇게15초만에 거절당해서 벙찌고 당황스러웠다그래서그냥 나왔지where is my 아빠?아빠는 내가 여기서 검사할 줄 알고 검사 끝나면 연락하라고 했다어제 밤새 실험하느라 잠을 못자서 사무실 다시 나가서 잠도 좀 자고 실험도 새로 해야한다고 하며날 버리고 간 아빠(상황은 어느정도 이해는 되지만보호자로 따라 온거 아니신지?조금 실망이야)바로 엄마한테 상황 얘기하니까 조금 기다려보라고 다른 병원 알아보겠다고 해서나도 기다리면서 다른 병원 도 알아보고 보험사에도 물어보고 했는데엄마가 그냥 지금 입원한 병원의 협력 병원에서 하는게 제일 좋을 거 같다고 해서 다시 병원 에 연락해서 예약 다시 할 수 있는지 문의드렸다퇴원일 조정이 가능한지는 먼저 보험사에 물어봤더니 그건 병원 권한이어서 그쪽에 알아보라고 하셔서이건 다시 병원 복귀해서 알아보기로쨋든 아빠는 날 두고 사무실로 가버리셔서다시 연락하니 방금 실험 걸 어놔서 택시타고 갈 수 있겠냐며택시타고 갈게해놓고 오랜만에 밖에 나와서 신나서(?) 집까지 걸어갔다20분 걸었나허리랑 골 반 이 좀 아팠는데오랜만에 1층 땅을 밟아서 살을 에는 추위도 잊고 걸어서 집 도착(왜냐면 길어진 입원에 챙겨야할 물건들이 있 어서잠시 귀가)걸어왔다니까 엄마한테 등짝스매싱아픈 애가 어딜 걸어오냐고날도 추운데필요한 거만 챙기고 엄마가 타준 유자차 한잔 들고 바로 엄마 차 타고 병원 복귀오자마자 침 맞고간호사분이파스 왜 안붙이냐고 쌓였다면서 봉지 가져다줄까요? 해서 받 았다매일 2장씩 주신다나는 파스 부자매일 붙이기엔 그래서 23일에 한개씩 붙이는중이다효과가 제법 좋은 파스다:)연차라 업무로부터 자유로우니 온열찜질방도 구경하고 오기:)시설이 정말 괜찮다)";
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.splitIntoSents(text);
	EXPECT_GT(res.size(), 1);

	KiwiBuilder builder{ MODEL_PATH, 1, BuildOption::default_, ModelType::none };
	EXPECT_TRUE(builder.addWord(u"모", POSTag::nng).second);
	Kiwi kiwi2 = builder.build();
	auto res2 = kiwi2.splitIntoSents(text);
	EXPECT_EQ(res.size(), res2.size());
}

TEST(KiwiCpp, IssueP131_SentenceSplitError)
{
	const char16_t* text[] = {
		u"특파원입니다. --지난",
		u"특파원입니다.\n--지난",
		u"특파원입니다.-- 지난",
	};
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.splitIntoSents(text[0]);
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0], std::make_pair((size_t)0, (size_t)7));
	EXPECT_EQ(res[1], std::make_pair((size_t)8, (size_t)12));

	res = kiwi.splitIntoSents(text[1]);
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0], std::make_pair((size_t)0, (size_t)7));
	EXPECT_EQ(res[1], std::make_pair((size_t)8, (size_t)12));

	res = kiwi.splitIntoSents(text[2]);
	EXPECT_EQ(res.size(), 2);
	EXPECT_EQ(res[0], std::make_pair((size_t)0, (size_t)9));
	EXPECT_EQ(res[1], std::make_pair((size_t)10, (size_t)12));
}

TEST(KiwiCpp, Issue181_SentenceSplitError)
{
	const char16_t* text = u"존 슈발John Schwall은 그에 꼭 들어맞는 흥미로운 사례였다. 슈발의 아버지와 할아버지는 스테이튼 아일랜드의 소방관이었다. “제 친가 쪽의 남자들은 모두 소방관이에요. 전 다 른 일을 하고 싶었죠.” 슈발이 말했다.";
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.splitIntoSents(text);
	EXPECT_EQ(res.size(), 5);
	EXPECT_EQ(res[0], std::make_pair((size_t)0, (size_t)38));
	EXPECT_EQ(res[1], std::make_pair((size_t)39, (size_t)72));
	EXPECT_EQ(res[2], std::make_pair((size_t)73, (size_t)97));
	EXPECT_EQ(res[3], std::make_pair((size_t)98, (size_t)115));
	EXPECT_EQ(res[4], std::make_pair((size_t)116, (size_t)124));
}

TEST(KiwiCpp, AddRule)
{
	Kiwi& okiwi = reuseKiwiInstance();
	auto ores = okiwi.analyze(u"했어요! 하잖아요! 할까요? 좋아요!", Match::allWithNormalizing);
	
	{
		KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_ & ~BuildOption::loadTypoDict, ModelType::none };
		auto inserted = builder.addRule(POSTag::ef, [](std::u16string input)
		{
			if (input.back() == u'요')
			{
				input.back() = u'용';
			}
			return input;
		}, 0);

		Kiwi kiwi = builder.build();
		auto res = kiwi.analyze(u"했어용! 하잖아용! 할까용? 좋아용!", Match::allWithNormalizing);

		EXPECT_EQ(ores.second, res.second);
	}

	{
		KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_ & ~BuildOption::loadTypoDict, ModelType::none };
		auto inserted = builder.addRule(POSTag::ef, [](std::u16string input)
		{
			if (input.back() == u'요')
			{
				input.back() = u'용';
			}
			return input;
		}, -1);

		Kiwi kiwi = builder.build();
		auto res = kiwi.analyze(u"했어용! 하잖아용! 할까용? 좋아용!", Match::allWithNormalizing);

		EXPECT_FLOAT_EQ(ores.second - 4, res.second);
	}
}

TEST(KiwiCpp, AddPreAnalyzedWord)
{
	Kiwi& okiwi = reuseKiwiInstance();
	auto ores = okiwi.analyze("뜅겼어...", Match::allWithNormalizing);

	KiwiBuilder builder{ MODEL_PATH };
	std::vector<std::tuple<const char16_t*, POSTag, uint8_t>> analyzed;
	analyzed.emplace_back(u"뜅기", POSTag::vv, undefSenseId);
	analyzed.emplace_back(u"었", POSTag::ep, undefSenseId);
	analyzed.emplace_back(u"어", POSTag::ef, undefSenseId);
	
	EXPECT_THROW(builder.addPreAnalyzedWord(u"뜅겼어", analyzed), UnknownMorphemeException);

	builder.addWord(u"뜅기", POSTag::vv, 0, u"튕기");
	builder.addPreAnalyzedWord(u"뜅겼어", analyzed);
	
	Kiwi kiwi = builder.build();
	auto res = kiwi.analyze("뜅겼어...", Match::allWithNormalizing);
	
	ASSERT_GE(res.first.size(), 4);
	EXPECT_EQ(res.first[0].str, u"뜅기");
	EXPECT_EQ(res.first[0].tag, POSTag::vv);
	EXPECT_EQ(res.first[1].str, u"었");
	EXPECT_EQ(res.first[1].tag, POSTag::ep);
	EXPECT_EQ(res.first[2].str, u"어");
	EXPECT_EQ(res.first[2].tag, POSTag::ef);
	EXPECT_EQ(res.first[3].str, u"...");
	EXPECT_EQ(res.first[3].tag, POSTag::sf);
}

TEST(KiwiCpp, JoinAffix)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto sample = u"사랑스러운 풋사과들아! 배송됐니";
	auto ores = kiwi.analyze(sample, Match::none);
	auto res0 = kiwi.analyze(sample, Match::joinNounPrefix);
	EXPECT_EQ(res0.first[3].str, u"풋사과");
	auto res1 = kiwi.analyze(sample, Match::joinNounSuffix);
	EXPECT_EQ(res1.first[4].str, u"사과들");
	auto res2 = kiwi.analyze(sample, Match::joinNounPrefix | Match::joinNounSuffix);
	EXPECT_EQ(res2.first[3].str, u"풋사과들");
	auto res3 = kiwi.analyze(sample, Match::joinAdjSuffix);
	EXPECT_EQ(res3.first[0].str, u"사랑스럽");
	auto res4 = kiwi.analyze(sample, Match::joinVerbSuffix);
	EXPECT_EQ(res4.first[8].str, u"배송되");
	auto res5 = kiwi.analyze(sample, Match::joinAffix);
	EXPECT_EQ(res5.first[0].str, u"사랑스럽");
	EXPECT_EQ(res5.first[2].str, u"풋사과들");
	EXPECT_EQ(res5.first[5].str, u"배송되");
}

TEST(KiwiCpp, JoinParticleYo)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto sample1 = u"밥을 먹는다던가요";
	auto res_without = kiwi.analyze(sample1, Match::none).first;
	auto res_with = kiwi.analyze(sample1, Match::joinParticleYo).first;
	
	EXPECT_EQ(res_without[res_without.size() - 2].str, u"는다던가");
	EXPECT_EQ(res_without[res_without.size() - 1].str, u"요");

	EXPECT_EQ(res_with[res_with.size() - 1].str, u"는다던가요");
}

TEST(KiwiCpp, CompatibleJamo)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto res1 = kiwi.analyze(u"미룬다. 미룸. 미룰것.", Match::none).first;
	EXPECT_EQ(res1.size(), 10);
	EXPECT_EQ(res1[1].str, u"ᆫ다");
	EXPECT_EQ(res1[4].str, u"ᆷ");
	EXPECT_EQ(res1[7].str, u"ᆯ");

	auto res2 = kiwi.analyze(u"미룬다. 미룸. 미룰것.", Match::compatibleJamo).first;
	EXPECT_EQ(res2.size(), 10);
	EXPECT_EQ(res2[1].str, u"ㄴ다");
	EXPECT_EQ(res2[4].str, u"ㅁ");
	EXPECT_EQ(res2[7].str, u"ㄹ");

	auto res3 = kiwi.analyze(u"ᄀᄁᄂᄃᄄᄅᄆᄇᄈᄉᄊᄋᄌᄍᄎᄏᄐᄑᄒ ᆨᆩᆪᆫᆬᆭᆮᆯᆰᆱᆲᆳᆴᆵᆶᆷᆸᆹᆺᆻᆼᆽᆾᆿᇀᇁᇂ", Match::compatibleJamo).first;
	EXPECT_EQ(res3.size(), 2);
	EXPECT_EQ(res3[0].str, u"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ");
	EXPECT_EQ(res3[1].str, u"ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ");
}

TEST(KiwiCpp, AutoJoiner)
{
	Kiwi& kiwi = reuseKiwiInstance();
	std::vector<std::pair<uint32_t, uint32_t>> ranges;
	auto joiner = kiwi.newJoiner();
	joiner.add(u"시동", POSTag::nng);
	joiner.add(u"를", POSTag::jko);
	EXPECT_EQ(joiner.getU16(&ranges), u"시동을");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 2, 2, 3 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"시동", POSTag::nng);
	joiner.add(u"ᆯ", POSTag::jko);
	EXPECT_EQ(joiner.getU16(&ranges), u"시동을");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 2, 2, 3 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"나", POSTag::np);
	joiner.add(u"ᆯ", POSTag::jko);
	EXPECT_EQ(joiner.getU16(&ranges), u"날");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 0, 1 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"시도", POSTag::nng);
	joiner.add(u"를", POSTag::jko);
	EXPECT_EQ(joiner.getU16(&ranges), u"시도를");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 2, 2, 3 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"바다", POSTag::nng);
	joiner.add(u"가", POSTag::jks);
	EXPECT_EQ(joiner.getU16(&ranges), u"바다가");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 2, 2, 3 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"바닥", POSTag::nng);
	joiner.add(u"가", POSTag::jks);
	EXPECT_EQ(joiner.getU16(&ranges), u"바닥이");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 2, 2, 3 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"불", POSTag::nng);
	joiner.add(u"으로", POSTag::jkb);
	EXPECT_EQ(joiner.getU16(&ranges), u"불로");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"북", POSTag::nng);
	joiner.add(u"으로", POSTag::jkb);
	EXPECT_EQ(joiner.getU16(&ranges), u"북으로");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 3 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"갈", POSTag::vv);
	joiner.add(u"면", POSTag::ec);
	EXPECT_EQ(joiner.getU16(&ranges), u"갈면");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"갈", POSTag::vv);
	joiner.add(u"시", POSTag::ep);
	joiner.add(u"았", POSTag::ep);
	joiner.add(u"면", POSTag::ec);
	EXPECT_EQ(joiner.getU16(&ranges), u"가셨으면");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2, 1, 2, 2, 4 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"하", POSTag::vv);
	joiner.add(u"았", POSTag::ep);
	joiner.add(u"다", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"했다");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 0, 1, 1, 2 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"날", POSTag::vv);
	joiner.add(u"어", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"날아");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"고기", POSTag::nng);
	joiner.add(u"을", POSTag::jko);
	joiner.add(u"굽", POSTag::vv);
	joiner.add(u"어", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"고기를 구워");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 2, 2, 3, 4, 6, 5, 6 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"길", POSTag::nng);
	joiner.add(u"을", POSTag::jko);
	joiner.add(u"걷", POSTag::vv);
	joiner.add(u"어요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"길을 걸어요");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2, 3, 4, 4, 6 }));

	joiner = kiwi.newJoiner(false);
	joiner.add(u"길", POSTag::nng);
	joiner.add(u"을", POSTag::jko);
	joiner.add(u"걷", POSTag::vv);
	joiner.add(u"어요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"길을 걷어요");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2, 3, 4, 4, 6 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"땅", POSTag::nng);
	joiner.add(u"에", POSTag::jkb);
	joiner.add(u"묻", POSTag::vv);
	joiner.add(u"어요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"땅에 묻어요");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2, 3, 4, 4, 6 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"땅", POSTag::nng);
	joiner.add(u"이", POSTag::vcp);
	joiner.add(u"에요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"땅이에요");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 2, 2, 4 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"바다", POSTag::nng);
	joiner.add(u"이", POSTag::vcp);
	joiner.add(u"에요", POSTag::ef);
	EXPECT_EQ(joiner.getU16(&ranges), u"바다에요");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 2, 2, 2, 2, 4 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"좋", POSTag::va);
	joiner.add(u"은데", POSTag::ec);
	EXPECT_EQ(joiner.getU16(&ranges), u"좋은데");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 1, 3 }));

	joiner = kiwi.newJoiner();
	joiner.add(u"크", POSTag::va);
	joiner.add(u"은데", POSTag::ec);
	EXPECT_EQ(joiner.getU16(&ranges), u"큰데");
	EXPECT_EQ(ranges, toPair<uint32_t>({ 0, 1, 0, 2 }));
}

TEST(KiwiCpp, UserWordWithNumeric)
{
	KiwiBuilder builder{ MODEL_PATH };
	EXPECT_TRUE(builder.addWord(u"코로나19", POSTag::nnp, 0.0).second);
	EXPECT_TRUE(builder.addWord(u"2차전지", POSTag::nnp, 0.0).second);
	builder.addWord(u"K9", POSTag::nnp, 3.0);
	builder.addWord(u"K55", POSTag::nnp, 3.0);
	Kiwi kiwi = builder.build();

	auto tokens = kiwi.analyze(u"코로나19이다.", Match::all).first;

	ASSERT_GE(tokens.size(), 3);
	EXPECT_EQ(tokens[0].str, u"코로나19");
	EXPECT_EQ(tokens[1].str, u"이");
	EXPECT_EQ(tokens[2].str, u"다");

	tokens = kiwi.analyze(u"2차전지이다.", Match::all).first;

	ASSERT_GE(tokens.size(), 3);
	EXPECT_EQ(tokens[0].str, u"2차전지");
	EXPECT_EQ(tokens[1].str, u"이");
	EXPECT_EQ(tokens[2].str, u"다");

	tokens = kiwi.analyze(u"K9 K55", Match::all).first;
	ASSERT_GE(tokens.size(), 2);
	EXPECT_EQ(tokens[0].str, u"K9");
	EXPECT_EQ(tokens[1].str, u"K55");
}

TEST(KiwiCpp, Quotation)
{
	Kiwi& kiwi = reuseKiwiInstance();
	std::vector<TokenInfo> quotTokens;
	auto tokens = kiwi.analyze(u"그는 \"여러분 이거 다 거짓말인거 아시죠?\"라고 물으며 \"아무것도 모른다\"고 말했다.", Match::allWithNormalizing).first;
	EXPECT_GE(tokens.size(), 26);
	std::copy_if(tokens.begin(), tokens.end(), std::back_inserter(quotTokens), [](const TokenInfo& token)
	{
		return token.str == u"\"";
	});
	EXPECT_EQ(quotTokens.size(), 4);
	EXPECT_EQ(quotTokens[0].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[1].tag, POSTag::ssc);
	EXPECT_EQ(quotTokens[2].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[3].tag, POSTag::ssc);

	tokens = kiwi.analyze(u"\"중첩된 인용부호, 그것은 '중복', '반복', '계속되는 되풀이'인 것이다.\"", Match::allWithNormalizing).first;
	quotTokens.clear();
	std::copy_if(tokens.begin(), tokens.end(), std::back_inserter(quotTokens), [](const TokenInfo& token)
	{
		return token.str == u"\"";
	});
	EXPECT_EQ(quotTokens.size(), 2);
	EXPECT_EQ(quotTokens[0].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[1].tag, POSTag::ssc);
	quotTokens.clear();
	std::copy_if(tokens.begin(), tokens.end(), std::back_inserter(quotTokens), [](const TokenInfo& token)
	{
		return token.str == u"'";
	});
	EXPECT_EQ(quotTokens.size(), 6);
	EXPECT_EQ(quotTokens[0].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[1].tag, POSTag::ssc);
	EXPECT_EQ(quotTokens[2].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[3].tag, POSTag::ssc);
	EXPECT_EQ(quotTokens[4].tag, POSTag::sso);
	EXPECT_EQ(quotTokens[5].tag, POSTag::ssc);

	tokens = kiwi.analyze(u"I'd like to be a tree.", Match::allWithNormalizing).first;
	EXPECT_EQ(tokens[1].tag, POSTag::ss);
}

TEST(KiwiCpp, JoinRestore)
{
	Kiwi& kiwi = reuseKiwiInstance();
	for (auto c : {
		//u8"이야기가 얼마나 지겨운지 잘 알고 있다. \" '아!'하고 힐데가르드는 한숨을 푹 쉬며 말했다.",
		u8"승진해서 어쨌는 줄 아슈?",
		u8"2002년 아서 안데르센의 몰락",
		u8"호텔의 음침함이 좀 나아 보일 정도였다",
		u8"황자의 일을 물을려고 부른 것이 아니냐고",
		u8"생겼는 지 제법 알을 품는 것 같다.",
		u8"음악용 CD를 들을 수 있다",
		u8"좋아요도 눌렀다.",
		u8"좋은데",
		u8"않았다",
		u8"인정받았다",
		u8"하지 말아야",
		u8"말았다",
		//u8"비어 있다", 
		u8"기어 가다", 
		u8"좋은 태도입니다",
		u8"바로 '내일'입니다",
		u8"in the",
		u8"할 것이었다",
		u8"몇 번의 신호 음이 이어지고",
		u8"오래 나가 있는 바람에",
		u8"간이 학교로서 인가를 받은",
		u8"있을 터였다",
		u8"극도의 인륜 상실",
		u8"'내일'을 말하다",
		//u8"실톱으로 다듬어 놓은 것들",
		})
	{
		auto tokens = kiwi.analyze(c, Match::allWithNormalizing).first;
		auto joiner = kiwi.newJoiner();
		for (auto& t : tokens)
		{
			joiner.add(t.str, t.tag, false);
		}
		EXPECT_EQ(joiner.getU8(), c);
	}
}

TEST(KiwiCpp, JoinZSiot)
{
	Kiwi& kiwi = reuseKiwiInstance();
	for (auto c : { u8"하굣길", u8"만둣국", u8"나뭇잎", u8"세숫물", u8"고춧가루", u8"시곗바늘", u8"사글셋방" })
	{
		auto tokens = kiwi.analyze(c, Match::allWithNormalizing | Match::splitSaisiot).first;
		auto joiner = kiwi.newJoiner();
		for (auto& t : tokens)
		{
			joiner.add(t.str, t.tag, false);
		}
		EXPECT_EQ(joiner.getU8(), c);
	}
}

TEST(KiwiCpp, NestedSentenceSplit)
{
	Kiwi& kiwi = reuseKiwiInstance();

	for (auto c : {
		u8"“절망한 자는 대담해지는 법이다”라는 니체의 경구가 부제로 붙은 시이다.",
		u8"우리는 저녁을 먹은 다음 식탁을 치우기 전에 할머니가 떠먹는 요구르트를 다 먹고 조그만 숟가락을 싹싹 핥는 일을 끝마치기를(할머니가 그 숟가락을 브래지어에 쑤셔넣을지도 몰랐다. 요컨대 흔한 일이다) 기다리고 있었다.",
		u8"조현준 효성그룹 회장도 신년사를 통해 “속도와 효율성에 기반한 민첩한 조직으로 탈바꿈해야 한다”며 “이를 위해 무엇보다 데이터베이스 경영이 뒷받침돼야 한다”고 말했다.",
		u8"1699년에 한 학자는 \"어떤 것은 뿔을 앞으로 내밀고 있고, 다른 것은 뾰족한 꼬리를 만들기도 한다.새부리 모양을 하고 있는 것도 있고, 털로 온 몸을 덮고 있다가 전체가 거칠어지기도 하고 비늘로 뒤덮여 뱀처럼 되기도 한다\"라고 기록했다.",
		u8"회사의 정보 서비스를 책임지고 있는 로웬버그John Loewenberg는 <서비스 산업에 있어 종이는 혈관내의 콜레스트롤과 같다. 나쁜 종이는 동맥을 막는 내부의 물질이다.>라고 말한다.",
		u8"그것은 바로 ‘내 임기 중에는 대운하 완공할 시간이 없으니 임기 내에 강별로 소운하부터 개통하겠다’는 것이나 다름없다. ",
		u8"그러나 '문학이란 무엇인가'를 묻느니보다는 '무엇이 하나의 텍스트를 문학으로 만드는가'를 묻자고 제의했던 야콥슨 이래로, 문학의 본질을 정의하기보다는 문학의 존재론을 추적하는 것이 훨씬 생산적인 일이라는 것은 널리 알려진 바가 있다.",
		})
	{
		auto ranges = kiwi.splitIntoSents(c);
		EXPECT_EQ(ranges.size(), 1);
	}
}

TEST(KiwiCpp, IssueP172_LengthError)
{
	std::u16string text;
	text += u"\n";
	for (int i = 0; i < 4000; ++i)
	{
		text += u"좋은채팅사이트《35141561234.wang.com》";
	}
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.analyze(text, Match::allWithNormalizing).first;
	EXPECT_GT(res.size(), 0);
}

TEST(KiwiCpp, IssueP189)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto res = kiwi.analyze(u"담아 1팩 무료", Match::allWithNormalizing).first;

	EXPECT_EQ(res.size(), 5);
	EXPECT_EQ(res[0].str, u"담");
	EXPECT_EQ(res[1].str, u"어");
	EXPECT_EQ(res[2].str, u"1");
	EXPECT_EQ(res[3].str, u"팩");
	EXPECT_EQ(res[4].str, u"무료");
}

TEST(KiwiCpp, Issue205)
{
	if (sizeof(void*) != 8) 
	{
		std::cerr << "This test is only available in 64-bit mode" << std::endl;
		return;
	}

	KiwiBuilder builder{ MODEL_PATH, 0, BuildOption::default_, };
	builder.addWord(u"함박 스테이크");
	auto kiwi1 = builder.build();
	auto res1 = kiwi1.analyze(u"함박 스테이크를 먹었습니다", Match::allWithNormalizing).first;

	EXPECT_EQ(res1[0].str, u"함박 스테이크");

	auto kiwi2 = builder.build();
	AnalyzeOption option = Match::allWithNormalizing;
	option.typoTransformer = getDefaultPreparedTypoSet(DefaultTypoSet::basicTypoSetWithContinual);
	auto res2 = kiwi2.analyze(u"함박 스테이크를 먹었습니다", option).first;

	EXPECT_EQ(res2[0].str, u"함박 스테이크");
}

TEST(KiwiCpp, Dialect)
{
	Kiwi kiwi = KiwiBuilder{ MODEL_PATH, 0, BuildOption::default_, ModelType::none, Dialect::chungcheong }.build();

	auto res = kiwi.analyze(u"남자 손금은 오약손으루 보는 겨.", AnalyzeOption{ Match::allWithNormalizing, nullptr, false, Dialect::all, 6.f }).first;
	EXPECT_EQ(res.size(), 11);
	EXPECT_EQ(res[0].str, u"남자");
	EXPECT_EQ(res[1].str, u"손금");
	EXPECT_EQ(res[2].str, u"은");
	EXPECT_EQ(res[3].str, u"오약손");
	EXPECT_TRUE(!!(res[3].dialect & Dialect::chungcheong));
	EXPECT_EQ(res[4].str, u"으루");
	EXPECT_EQ(res[5].str, u"보");
	EXPECT_EQ(res[6].str, u"는");
	EXPECT_EQ(res[7].str, u"기");
	EXPECT_TRUE(!!(res[7].dialect & Dialect::chungcheong));
	EXPECT_EQ(res[8].str, u"이");
	EXPECT_EQ(res[9].str, u"어");
	EXPECT_EQ(res[10].str, u".");
}

TEST(KiwiCpp, StreamProvider)
{
	// Test the new StreamProvider interface for flexible model loading
	
	// Test 1: Verify the filesystem provider works (backward compatibility)
	{
		auto filesystemProvider = utils::makeFilesystemProvider(MODEL_PATH);
		
		// Test that we can create streams for required files
		try {
			auto morphStream = filesystemProvider("sj.morph");
			EXPECT_TRUE(morphStream != nullptr);
			EXPECT_TRUE(morphStream->good());
		} catch (const std::exception& e) {
			FAIL() << "Failed to create stream for sj.morph: " << e.what();
		}
	}
	
	// Test 2: Test memory provider with dummy data
	{
		std::unordered_map<std::string, std::vector<char>> memoryFiles;
		
		// Create dummy model files that won't cause crashes but demonstrate the interface
		std::string dummyMorph = "KIWI"; // Minimal header for morph file
		std::string dummyDict = "테스트\tnnp\t1.0\n"; // Simple dictionary entry
		std::string dummyRule = "# Simple rule\n"; // Simple rule file
		
		memoryFiles["sj.morph"] = std::vector<char>(dummyMorph.begin(), dummyMorph.end());
		memoryFiles["default.dict"] = std::vector<char>(dummyDict.begin(), dummyDict.end());
		memoryFiles["combiningRule.txt"] = std::vector<char>(dummyRule.begin(), dummyRule.end());
		
		auto memoryProvider = utils::makeMemoryProvider(memoryFiles);
		
		// Test that we can create streams from memory
		auto morphStream = memoryProvider("sj.morph");
		EXPECT_TRUE(morphStream != nullptr);
		EXPECT_TRUE(morphStream->good());
		
		// Test reading content
		std::string content;
		*morphStream >> content;
		EXPECT_EQ(content, "KIWI");
		
		auto dictStream = memoryProvider("default.dict");
		EXPECT_TRUE(dictStream != nullptr);
		
		std::string line;
		std::getline(*dictStream, line);
		EXPECT_TRUE(line.find("테스트") != std::string::npos);
		
		// Test non-existent file returns nullptr
		EXPECT_EQ(memoryProvider("nonexistent.file"), nullptr);
	}
	
	// Test 3: Test custom provider
	{
		auto customProvider = [](const std::string& filename) -> std::unique_ptr<std::istream> {
			std::string content = "custom_content_for_" + filename;
			return std::make_unique<std::stringstream>(content);
		};
		
		auto stream = customProvider("any_filename");
		EXPECT_TRUE(stream != nullptr);
		
		std::string content;
		*stream >> content;
		EXPECT_EQ(content, "custom_content_for_any_filename");
	}
}

TEST(KiwiCpp, Issue231)
{
	Kiwi& kiwi = reuseKiwiInstance();
	auto tokens = kiwi.analyze(u"숫", Match::allWithNormalizing).first;
	EXPECT_EQ(tokens.size(), 1);
	EXPECT_EQ(tokens[0].str, u"숫");
}
