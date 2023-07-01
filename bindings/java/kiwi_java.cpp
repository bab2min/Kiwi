#define _JNI_INT64_TO_INT
#include "JniUtils.hpp"

#include <kiwi/Kiwi.h>

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
	struct JClassName<kiwi::TokenResult>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$TokenResult" };
	};

	template<>
	struct JClassName<kiwi::TokenInfo>
	{
		static constexpr auto value = std::string_view{ "kr/pe/bab2min/Kiwi$Token" };
	};

	template<>
	struct ValueBuilder<kiwi::TokenResult> : public ValueBuilder<decltype(gClsTokenResult)>
	{
	};

	template<>
	struct ValueBuilder<kiwi::TokenInfo> : public ValueBuilder<decltype(gClsTokenInfo)>
	{
	};
}

class JKiwi : public kiwi::Kiwi, jni::JObject<JKiwi>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/Kiwi";

	using kiwi::Kiwi::Kiwi;

	JKiwi(Kiwi&& inst) : Kiwi{ std::move(inst) } {}

	auto analyze(const std::u16string& text, uint64_t topN, kiwi::Match matchOption) const
	{
		return Kiwi::analyze(text, topN, matchOption);
	}
};

class JKiwiBuilder : public kiwi::KiwiBuilder, jni::JObject<JKiwiBuilder>
{
public:
	static constexpr std::string_view className = "kr/pe/bab2min/KiwiBuilder";

	using kiwi::KiwiBuilder::KiwiBuilder;

	JKiwi build() const
	{
		return KiwiBuilder::build();
	}
};

jni::Module gModule{ JNI_VERSION_1_8 };

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
	return gModule.load(vm,

		jni::define<JKiwiBuilder>()
		.template ctor<std::string, size_t, kiwi::BuildOption, bool>()
		.template method<static_cast<bool(JKiwiBuilder::*)(const std::u16string&, kiwi::POSTag, float)>(&JKiwiBuilder::addWord)>("addWord")
		.template method<static_cast<bool(JKiwiBuilder::*)(const std::u16string&, kiwi::POSTag, float, const std::u16string&)>(&JKiwiBuilder::addWord)>("addWord")
		.template method<&JKiwiBuilder::build>("build")
		.template method<&JKiwiBuilder::loadDictionary>("loadDictionary"),

		jni::define<JKiwi>()
			.template method<&JKiwi::analyze>("analyze"),

		gClsTokenInfo,
		gClsTokenResult
	);
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved)
{
	gModule.unload(vm);
}
