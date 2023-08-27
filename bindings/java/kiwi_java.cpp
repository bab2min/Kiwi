#define _JNI_INT64_TO_INT
#include "JniUtils.hpp"

#include <kiwi/Kiwi.h>
#include <kiwi/Joiner.h>

struct Sentence
{
	std::u16string text;
	uint32_t start, end;
	std::vector<Sentence> subSents;
	std::optional<std::vector<kiwi::TokenInfo>> tokens;
};

struct JoinableToken
{
	std::u16string form;
	kiwi::POSTag tag;
	bool inferRegularity;
	kiwi::cmb::Space space;
};

struct AnalyzedMorph
{
	std::u16string form;
	kiwi::POSTag tag;
	int start, end;
};

static auto gClsTokenInfo = jni::DataClassDefinition<kiwi::TokenInfo>()
	.template property<&kiwi::TokenInfo::str>("form")
	.template property<&kiwi::TokenInfo::position>("position")
	.template property<&kiwi::TokenInfo::wordPosition>("wordPosition")
	.template property<&kiwi::TokenInfo::sentPosition>("sentPosition")
	.template property<&kiwi::TokenInfo::lineNumber>("lineNumber")
	.template property<&kiwi::TokenInfo::length>("length")
	.template property<&kiwi::TokenInfo::tag>("tag")
	.template property<&kiwi::TokenInfo::senseId>("senseId")
	.template property<&kiwi::TokenInfo::score>("score")
	.template property<&kiwi::TokenInfo::typoCost>("typoCost")
	.template property<&kiwi::TokenInfo::typoFormId>("typoFormId")
	.template property<&kiwi::TokenInfo::pairedToken>("pairedToken")
	.template property<&kiwi::TokenInfo::subSentPosition>("subSentPosition");

static auto gClsTokenResult = jni::DataClassDefinition<kiwi::TokenResult>()
	.template property<&kiwi::TokenResult::first>("tokens")
	.template property<&kiwi::TokenResult::second>("score");

static auto gClsSentence = jni::DataClassDefinition<Sentence>()
	.template property<&Sentence::text>("text")
	.template property<&Sentence::start>("start")
	.template property<&Sentence::end>("end")
	.template property<&Sentence::subSents>("subSents")
	.template property<&Sentence::tokens>("tokens");

static auto gClsJoinableToken = jni::DataClassDefinition<JoinableToken>()
	.template property<&JoinableToken::form>("form")
	.template property<&JoinableToken::tag>("tag")
	.template property<&JoinableToken::inferRegularity>("inferRegularity")
	.template property<&JoinableToken::space>("space");

static auto gClsAnalyzedMorph = jni::DataClassDefinition<AnalyzedMorph>()
	.template property<&AnalyzedMorph::form>("form")
	.template property<&AnalyzedMorph::tag>("tag")
	.template property<&AnalyzedMorph::start>("start")
	.template property<&AnalyzedMorph::end>("end");

static auto gClsBasicToken = jni::DataClassDefinition<kiwi::BasicToken>()
	.template property<&kiwi::BasicToken::form>("form")
	.template property<&kiwi::BasicToken::begin>("begin")
	.template property<&kiwi::BasicToken::end>("end")
	.template property<&kiwi::BasicToken::tag>("tag");

static auto gClsPretokenizedSpan = jni::DataClassDefinition<kiwi::PretokenizedSpan>()
	.template property<&kiwi::PretokenizedSpan::begin>("begin")
	.template property<&kiwi::PretokenizedSpan::end>("end")
	.template property<&kiwi::PretokenizedSpan::tokenization>("tokenization");

namespace jni
{
	template<>
	struct ValueBuilder<kiwi::BuildOption> : public ValueBuilder<uint32_t>
	{
		using CppType = kiwi::BuildOption;
		using JniType = jint;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return (CppType)v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return (JniType)v;
		}
	};

	template<>
	struct ValueBuilder<kiwi::Match> : public ValueBuilder<uint32_t>
	{
		using CppType = kiwi::Match;
		using JniType = jint;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return (CppType)v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return (JniType)v;
		}
	};

	template<>
	struct ValueBuilder<kiwi::POSTag> : public ValueBuilder<uint8_t>
	{
		using CppType = kiwi::POSTag;
		using JniType = jbyte;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return (CppType)v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return (JniType)v;
		}
	};

	template<>
	struct ValueBuilder<kiwi::cmb::Space> : public ValueBuilder<uint8_t>
	{
		using CppType = kiwi::cmb::Space;
		using JniType = jbyte;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return (CppType)v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return (JniType)v;
		}
	};

	template<>
	struct ValueBuilder<kiwi::CondVowel> : public ValueBuilder<uint8_t>
	{
		using CppType = kiwi::CondVowel;
		using JniType = jbyte;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return (CppType)v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return (JniType)v;
		}
	};

	template<>
	struct JClassName<kiwi::TokenResult>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$TokenResult" };
	};

	template<>
	struct ValueBuilder<kiwi::TokenResult> : public ValueBuilder<decltype(gClsTokenResult)>
	{
	};

	template<>
	struct JClassName<kiwi::TokenInfo>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$Token" };
	};

	template<>
	struct ValueBuilder<kiwi::TokenInfo> : public ValueBuilder<decltype(gClsTokenInfo)>
	{
	};

	template<>
	struct JClassName<Sentence>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$Sentence" };
	};

	template<>
	struct ValueBuilder<Sentence> : public ValueBuilder<decltype(gClsSentence)>
	{
	};

	template<>
	struct JClassName<JoinableToken>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$JoinableToken" };
	};

	template<>
	struct ValueBuilder<JoinableToken> : public ValueBuilder<decltype(gClsJoinableToken)>
	{
	};

	template<>
	struct JClassName<AnalyzedMorph>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/KiwiBuilder$AnalyzedMorph" };
	};

	template<>
	struct ValueBuilder<AnalyzedMorph> : public ValueBuilder<decltype(gClsAnalyzedMorph)>
	{
	};

	template<>
	struct JClassName<kiwi::BasicToken>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$BasicToken" };
	};

	template<>
	struct ValueBuilder<kiwi::BasicToken> : public ValueBuilder<decltype(gClsBasicToken)>
	{
	};

	template<>
	struct JClassName<kiwi::PretokenizedSpan>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$PretokenizedSpan" };
	};

	template<>
	struct ValueBuilder<kiwi::PretokenizedSpan> : public ValueBuilder<decltype(gClsPretokenizedSpan)>
	{
	};
}

class JKiwi;

class JMorphemeSet : jni::JObject<JMorphemeSet>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/Kiwi$MorphemeSet";

	kiwi::Kiwi* kiwiObj = nullptr;
	std::unordered_set<const kiwi::Morpheme*> morphSet;

	JMorphemeSet(JKiwi* _kiwiObj = nullptr);

	int add(const std::u16string& form, kiwi::POSTag tag)
	{
		if (!kiwiObj) return -1;
		auto found = kiwiObj->findMorpheme(form, tag);
		int added = 0;
		for(auto& m : found) added += morphSet.emplace(m).second ? 1 : 0;
		return added;
	}
};

class JMultipleTokenResult : jni::JObject<JMultipleTokenResult>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/Kiwi$MultipleTokenResult";
	jni::JUniqueGlobalRef<JKiwi> dp;
	std::deque<std::future<std::vector<kiwi::TokenResult>>> futures;

	jni::JIterator<std::u16string> texts;
	size_t topN;
	kiwi::Match matchOption;
	JMorphemeSet* blocklist;
	jni::JIterator<jni::JIterator<kiwi::PretokenizedSpan>> pretokenized;

	JMultipleTokenResult(jni::JUniqueGlobalRef<JKiwi>&& _dp,
		jni::JIterator<std::u16string> _texts,
		size_t _topN,
		kiwi::Match _matchOption,
		JMorphemeSet* _blocklist,
		jni::JIterator<jni::JIterator<kiwi::PretokenizedSpan>> _pretokenized
	);
	JMultipleTokenResult(JMultipleTokenResult&&) = default;
	JMultipleTokenResult& operator=(JMultipleTokenResult&&) = default;

	~JMultipleTokenResult()
	{
		waitQueue();
	}

	void waitQueue()
	{
		while (!futures.empty())
		{
			auto f = std::move(futures.front());
			futures.pop_front();
			f.get();
		}
	}

	bool feed();

	bool hasNext() const;

	std::vector<kiwi::TokenResult> next();
};

class JFutureTokenResult : public std::future<std::vector<kiwi::TokenResult>>, jni::JObject<JFutureTokenResult>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/Kiwi$FutureTokenResult";
	jni::JUniqueGlobalRef<JKiwi> dp;

	JFutureTokenResult(jni::JUniqueGlobalRef<JKiwi>&& _dp, std::future<std::vector<kiwi::TokenResult>>&& inst) 
		: dp{ std::move(_dp) }, future{ std::move(inst) } 
	{}

	bool isDone() const
	{
		using namespace std::chrono_literals;
		return future::wait_for(1ns) == std::future_status::ready;
	}

	std::vector<kiwi::TokenResult> get() 
	{
		return future::get();
	}
};

class JKiwi : public kiwi::Kiwi, jni::JObject<JKiwi>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/Kiwi";

	using kiwi::Kiwi::Kiwi;

	JKiwi(Kiwi&& inst) : Kiwi{ std::move(inst) } {}
	
	static std::string getVersion()
	{
		return KIWI_VERSION_STRING;
	}

	auto analyze(const std::u16string& text, uint64_t topN, kiwi::Match matchOption, JMorphemeSet* blocklist, jni::JIterator<kiwi::PretokenizedSpan> pretokenized) const
	{
		std::vector<kiwi::PretokenizedSpan> pretokenizedSpans;
		if (pretokenized)
		{
			while (pretokenized.hasNext()) pretokenizedSpans.emplace_back(pretokenized.next());
		}
		return Kiwi::analyze(text, topN, matchOption, blocklist ? &blocklist->morphSet : nullptr, pretokenizedSpans);
	}

	JFutureTokenResult asyncAnalyze(jni::JRef<JKiwi> _ref, const std::u16string& text, uint64_t topN, kiwi::Match matchOption, JMorphemeSet* blocklist, jni::JIterator<kiwi::PretokenizedSpan> pretokenized) const
	{
		std::vector<kiwi::PretokenizedSpan> pretokenizedSpans;
		if (pretokenized)
		{
			while (pretokenized.hasNext()) pretokenizedSpans.emplace_back(pretokenized.next());
		}
		return { _ref, Kiwi::asyncAnalyze(text, topN, matchOption, blocklist ? &blocklist->morphSet : nullptr, pretokenizedSpans) };
	}

	JMultipleTokenResult analyze2(jni::JRef<JKiwi> _ref, jni::JIterator<std::u16string> texts, uint64_t topN, kiwi::Match matchOption, JMorphemeSet* blocklist, jni::JIterator<jni::JIterator<kiwi::PretokenizedSpan>> pretokenized) const
	{
		if (!texts) throw std::bad_optional_access{};
		return { _ref, std::move(texts), topN, matchOption, blocklist, std::move(pretokenized) };
	}

	std::vector<Sentence> splitIntoSents(const std::u16string& text, kiwi::Match matchOption, bool returnTokens) const
	{
		std::vector<Sentence> ret;
		auto tokens = Kiwi::analyze(text, matchOption).first;
		uint32_t sentPos = -1;
		size_t i = 0, t = 0;
		for (auto& token : tokens)
		{
			if (token.sentPosition != sentPos)
			{
				if (!ret.empty())
				{
					ret.back().text = text.substr(ret.back().start, ret.back().end - ret.back().start);
					if (returnTokens)
					{
						ret.back().tokens.emplace(std::make_move_iterator(tokens.begin() + t), std::make_move_iterator(tokens.begin() + i));
					}
				}
				ret.emplace_back();
				ret.back().start = token.position;
				ret.back().end = token.position + token.length;
				sentPos = token.sentPosition;
				t = i;
			}
			else
			{
				ret.back().end = token.position + token.length;
			}
			++i;
		}
		if (!ret.empty())
		{
			ret.back().text = text.substr(ret.back().start, ret.back().end - ret.back().start);
			if (returnTokens)
			{
				ret.back().tokens.emplace(std::make_move_iterator(tokens.begin() + t), std::make_move_iterator(tokens.begin() + i));
			}
		}
		// To Do: process for subSents
		return ret;
	}

	std::u16string join(std::vector<JoinableToken>&& tokens) const
	{
		auto joiner = Kiwi::newJoiner();
		for (auto& token : tokens)
		{
			joiner.add(token.form, token.tag, token.inferRegularity, token.space);
		}
		return joiner.getU16();
	}
};

JMorphemeSet::JMorphemeSet(JKiwi* _kiwiObj)
	: kiwiObj{ _kiwiObj }
{
}

JMultipleTokenResult::JMultipleTokenResult(jni::JUniqueGlobalRef<JKiwi>&& _dp,
	jni::JIterator<std::u16string> _texts,
	size_t _topN,
	kiwi::Match _matchOption,
	JMorphemeSet* _blocklist,
	jni::JIterator<jni::JIterator<kiwi::PretokenizedSpan>> _pretokenized
)
	: dp{ std::move(_dp) },
	texts{ std::move(_texts) },
	matchOption{ _matchOption },
	blocklist{ _blocklist },
	pretokenized{ std::move(_pretokenized) }
{
	for (size_t i = 0; i < dp->getThreadPool()->size(); ++i)
	{
		if (!feed()) break;
	}
}

bool JMultipleTokenResult::feed()
{
	if (!texts.hasNext()) return false;
	std::vector<kiwi::PretokenizedSpan> pretokenizedSpans;

	if (pretokenized)
	{
		if (!pretokenized.hasNext())
		{
			throw std::runtime_error{ "The length of `pretokenized` must be equal to `texts`." };
		}

		auto pt = pretokenized.next();
		while (pt && pt.hasNext()) pretokenizedSpans.emplace_back(pt.next());
	}

	futures.emplace_back(static_cast<kiwi::Kiwi&>(dp.get()).asyncAnalyze(
		texts.next(), 
		topN, 
		matchOption, 
		blocklist ? &blocklist->morphSet : nullptr, 
		std::move(pretokenizedSpans)
	));
	return true;
}

bool JMultipleTokenResult::hasNext() const
{
	return !futures.empty();
}

std::vector<kiwi::TokenResult> JMultipleTokenResult::next()
{
	feed();
	auto f = std::move(futures.front());
	futures.pop_front();
	return f.get();
}

class JTypoTransformer : public kiwi::TypoTransformer, jni::JObject<JTypoTransformer>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/KiwiBuilder$TypoTransformer";

	using kiwi::TypoTransformer::TypoTransformer;
};

class JKiwiBuilder : public kiwi::KiwiBuilder, jni::JObject<JKiwiBuilder>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/KiwiBuilder";

	using kiwi::KiwiBuilder::KiwiBuilder;

	bool addWord(const std::u16string& form, kiwi::POSTag tag, float score)
	{
		return KiwiBuilder::addWord(form, tag, score).second;
	}

	bool addWord2(const std::u16string& form, kiwi::POSTag tag, float score, const std::u16string& orig)
	{
		return KiwiBuilder::addWord(form, tag, score, orig).second;
	}

	bool addPreAnalyzedWord(const std::u16string& form, std::vector<AnalyzedMorph>&& analyzed, float score)
	{
		std::vector<std::pair<std::u16string, kiwi::POSTag>> morphs;
		std::vector<std::pair<size_t, size_t>> positions;
		for (auto& i : analyzed)
		{
			morphs.emplace_back(std::move(i.form), std::move(i.tag));
			if (i.start >= 0 && i.end >= 0) positions.emplace_back(i.start, i.end);
		}
		if (positions.size() < morphs.size()) positions.clear();
		return KiwiBuilder::addPreAnalyzedWord(form, morphs, positions, score);
	}

	JKiwi build(JTypoTransformer* typos, float typoCostThreshold) const
	{
		if (typos) 
		{
			return KiwiBuilder::build(*typos, typoCostThreshold);
		}
		else
		{
			return KiwiBuilder::build();
		}
	}
};

jni::Module gModule{ JNI_VERSION_1_8 };

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
	return gModule.load(vm,

		jni::define<JTypoTransformer>()
			.template ctor<>()
			.template method<&JTypoTransformer::addTypo>("_addTypo"),

		jni::define<JKiwiBuilder>()
			.template ctor<std::string, size_t, kiwi::BuildOption, bool>()
			.template method<&JKiwiBuilder::addWord>("addWord")
			.template method<&JKiwiBuilder::addWord2>("addWord")
			.template method<&JKiwiBuilder::addPreAnalyzedWord>("addPreAnalyzedWord")
			.template method<&JKiwiBuilder::build>("build")
			.template method<&JKiwiBuilder::loadDictionary>("loadDictionary"),

		jni::define<JMorphemeSet>()
			.template ctor<JKiwi*>()
			.template method<&JMorphemeSet::add>("add"),

		jni::define<JMultipleTokenResult>()
			.template method<&JMultipleTokenResult::hasNext>("hasNext")
			.template method<&JMultipleTokenResult::next>("next"),

		jni::define<JFutureTokenResult>()
			.template method<&JFutureTokenResult::isDone>("isDone")
			.template method<&JFutureTokenResult::get>("get"),

		jni::define<JKiwi>()
			.template method<&JKiwi::getVersion>("getVersion")
			.template method<&JKiwi::analyze>("analyze")
			.template method<&JKiwi::analyze2>("analyze")
			.template method<&JKiwi::asyncAnalyze>("asyncAnalyze")
			.template method<&JKiwi::splitIntoSents>("splitIntoSents")
			.template method<&JKiwi::join>("join"),

		gClsTokenInfo,
		gClsTokenResult,
		gClsSentence,
		gClsJoinableToken,
		gClsAnalyzedMorph,
		gClsBasicToken,
		gClsPretokenizedSpan
	);
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved)
{
	gModule.unload(vm);
}
