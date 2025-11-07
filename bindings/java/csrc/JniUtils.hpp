#pragma once
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <optional>
#include <iostream>
#include <cstdint>

#include <jni.h>

namespace jni
{
	using namespace std::literals;

	struct StringConcat {
		template <std::string_view const&... Vs>
		struct Helper {
			static constexpr auto Build() noexcept {
				constexpr std::size_t len = (Vs.size() + ... + 0);
				std::array<char, len + 1> arr{};
				auto n =
					[i = 0, &arr](auto const& s) mutable {
					for (auto c : s) arr[i++] = c;
				};
				(n(Vs), ...);
				arr[len] = 0;

				return arr;
			}

			static constexpr auto arr = Build();
			static constexpr std::string_view value{ arr.data(), arr.size() - 1 };
		};

		template <std::string_view const&... Vs>
		static constexpr std::string_view value = Helper<Vs...>::value;
	};

	template <std::string_view const&... Vs>
	static constexpr auto StringConcat_v = StringConcat::value<Vs...>;

	static constexpr auto svLParen = "("sv;
	static constexpr auto svRParen = ")"sv;
	static constexpr auto svLBrack = "["sv;
	static constexpr auto svV = "V"sv;
	static constexpr auto svL = "L"sv;
	static constexpr auto svSC = ";"sv;
	static constexpr auto svNullTerm = "\x00"sv;
	static constexpr auto svNotInstanceOf = "Object isn't instance of "sv;

	template<class Func>
	inline auto handleExc(JNIEnv* env, Func&& func);

	template<class Ty>
	struct NativeMethod
	{
		const char* name;
		const char* signature;
		Ty fnPtr;

		operator JNINativeMethod() const
		{
			return { (char*)name, (char*)signature, (void*)fnPtr };
		}
	};

	template<class Ty>
	class JPureObject
	{
	public:
		static jclass jClass;
	};

	template<class Ty>
	class JObject : public JPureObject<Ty>
	{
	public:
		static jfieldID jInstField;
		static jmethodID jInitMethod;
	};
	
	template<class Ty>
	jclass JPureObject<Ty>::jClass = nullptr;

	template<class Ty>
	jfieldID JObject<Ty>::jInstField = nullptr;

	template<class Ty>
	jmethodID JObject<Ty>::jInitMethod = nullptr;

	template<class Ty>
	class JRef
	{
	protected:
		jobject inst;
	public:
		JRef(jobject _inst = nullptr)
			: inst{_inst}
		{}

		JRef(const JRef&) = default;
		JRef(JRef&&) = default;

		Ty& get();
		const Ty& get() const;

		bool empty() const { return !inst; }
		operator bool() const { return !!inst; }
		operator jobject() const { return inst; }

		Ty* operator->()
		{
			return &get();
		}

		const Ty* operator->() const
		{
			return &get();
		}
	};

	static thread_local JNIEnv* threadLocalEnv = nullptr;

	template<class Ty>
	class JUniqueGlobalRef : public JRef<Ty>
	{
	public:
		JUniqueGlobalRef()
		{}

		JUniqueGlobalRef(JRef<Ty> ref)
			: JRef<Ty>{ ref }
		{
			this->inst = threadLocalEnv->NewGlobalRef(this->inst);
		}

		JUniqueGlobalRef(const JUniqueGlobalRef& ref)
			: JRef<Ty>{ ref }
		{
			this->inst = threadLocalEnv->NewGlobalRef(this->inst);
		}

		JUniqueGlobalRef(JUniqueGlobalRef&& o)
		{
			std::swap(this->inst, o.inst);
		}

		JUniqueGlobalRef& operator=(const JUniqueGlobalRef& o)
		{
			if (this != &o)
			{
				close(threadLocalEnv);
				this->inst = threadLocalEnv->NewGlobalRef(o.inst);
			}
			return *this;
		}

		JUniqueGlobalRef& operator=(JUniqueGlobalRef&& o)
		{
			std::swap(this->inst, o.inst);
			return *this;
		}

		~JUniqueGlobalRef()
		{
			close(threadLocalEnv);
		}

	private:
		void close(JNIEnv* env)
		{
			if (this->inst)
			{
				env->DeleteGlobalRef(this->inst);
				this->inst = nullptr;
			}
		}
	};

	class JIteratorBase : public JUniqueGlobalRef<int>
	{
	public:
		static inline jclass jClass;
		static inline jmethodID jHasNext;
		static inline jmethodID jNext;

		JIteratorBase(jobject _inst)
			: JUniqueGlobalRef{ JRef{ _inst } }
		{}
	};

	template<class Ty>
	class JIterator : public JIteratorBase
	{
	public:
		using JIteratorBase::JIteratorBase;

		bool hasNext();
		Ty next();

		int& get() = delete;
		const int& get() const = delete;
		int* operator->() = delete;
		const int* operator->() const = delete;
	};

	template<class Ty>
	struct JClassName
	{
		static constexpr auto value = Ty::className;
	};

	template<class Ty>
	static constexpr auto jclassName = JClassName<Ty>::value;

	template<class Ty, class = void>
	struct ValueBuilder;

	template<class Ty>
	using ToJniType = typename ValueBuilder<Ty>::JniType;

	template<class Ty>
	static constexpr auto toJniTypeStr = ValueBuilder<Ty>::typeStr;

	template<>
	struct ValueBuilder<void>
	{
		using CppType = void;
		using JniType = void;
		static constexpr auto typeStr = "V"sv;

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			env->CallVoidMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
		}
	};

	template<>
	struct ValueBuilder<uint8_t>
	{
		using CppType = uint8_t;
		using JniType = jbyte;
		static constexpr auto typeStr = "B"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallByteMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<int8_t>
	{
		using CppType = int8_t;
		using JniType = jbyte;
		static constexpr auto typeStr = "B"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallByteMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<uint16_t>
	{
		using CppType = uint16_t;
		using JniType = jshort;
		static constexpr auto typeStr = "S"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallShortMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<int16_t>
	{
		using CppType = int16_t;
		using JniType = jshort;
		static constexpr auto typeStr = "S"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallShortMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<uint32_t>
	{
		using CppType = uint32_t;
		using JniType = jint;
		static constexpr auto typeStr = "I"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallIntMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<int32_t>
	{
		using CppType = int32_t;
		using JniType = jint;
		static constexpr auto typeStr = "I"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallIntMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

#ifdef _JNI_INT64_TO_INT
	template<>
	struct ValueBuilder<uint64_t>
	{
		using CppType = uint64_t;
		using JniType = jint;
		static constexpr auto typeStr = "I"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallIntMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<int64_t>
	{
		using CppType = int64_t;
		using JniType = jint;
		static constexpr auto typeStr = "I"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallIntMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

#ifdef __APPLE__
	template<>
	struct ValueBuilder<unsigned long>
	{
		using CppType = unsigned long;
		using JniType = jint;
		static constexpr auto typeStr = "I"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallIntMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<long>
	{
		using CppType = long;
		using JniType = jint;
		static constexpr auto typeStr = "I"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallIntMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};
#endif
#else
	template<>
	struct ValueBuilder<uint64_t>
	{
		using CppType = uint64_t;
		using JniType = jlong;
		static constexpr auto typeStr = "J"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallLongMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<int64_t>
	{
		using CppType = int64_t;
		using JniType = jlong;
		static constexpr auto typeStr = "J"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallLongMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};
#ifdef __APPLE__
	template<>
	struct ValueBuilder<unsigned long>
	{
		using CppType = unsigned long;
		using JniType = jlong;
		static constexpr auto typeStr = "J"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallLongMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<long>
	{
		using CppType = long;
		using JniType = jlong;
		static constexpr auto typeStr = "J"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallLongMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};
#endif
#endif

	template<>
	struct ValueBuilder<bool>
	{
		using CppType = bool;
		using JniType = jboolean;
		static constexpr auto typeStr = "Z"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return !!v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v ? -1 : 0;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallBooleanMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return !!ret;
		}
	};

	template<>
	struct ValueBuilder<char16_t>
	{
		using CppType = char16_t;
		using JniType = jchar;
		static constexpr auto typeStr = "C"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallCharMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<float>
	{
		using CppType = float;
		using JniType = jfloat;
		static constexpr auto typeStr = "F"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallFloatMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<double>
	{
		using CppType = double;
		using JniType = jdouble;
		static constexpr auto typeStr = "D"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return v;
		}

		JniType toJava(JNIEnv* env, CppType v)
		{
			return v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = env->CallDoubleMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return ret;
		}
	};

	template<>
	struct ValueBuilder<std::string>
	{
		using CppType = std::string;
		using JniType = jstring;
		static constexpr auto typeStr = "Ljava/lang/String;"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				throw std::bad_optional_access{};
			}
			auto* c = env->GetStringUTFChars(v, nullptr);
			auto size = env->GetStringUTFLength(v);
			return std::string{ c, c + size };
		}

		CppType fromJava(JNIEnv* env, jobject v)
		{
			return fromJava(env, (jstring)v);
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			return env->NewStringUTF(v.c_str());
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<>
	struct ValueBuilder<std::u16string>
	{
		using CppType = std::u16string;
		using JniType = jstring;
		static constexpr auto typeStr = "Ljava/lang/String;"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				throw std::bad_optional_access{};
			}
			auto* c = env->GetStringChars(v, nullptr);
			auto size = env->GetStringLength(v);
			return std::u16string{ (const char16_t*)c, (const char16_t*)c + size };
		}

		CppType fromJava(JNIEnv* env, jobject v)
		{
			return fromJava(env, (jstring)v);
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			return env->NewString((const jchar*)v.data(), v.size());
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<Ty, std::enable_if_t<std::is_base_of_v<JObject<Ty>, Ty>>>
	{
		using CppType = Ty;
		using JniType = jobject;
		static constexpr auto typeStr = StringConcat_v<svL, jclassName<Ty>, svSC>;

		CppType& fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				throw std::bad_optional_access{};
			}
			if (!env->IsInstanceOf(v, JObject<Ty>::jClass)) throw std::runtime_error{ StringConcat_v<svNotInstanceOf, typeStr, svNullTerm>.data()};
			auto ptr = (Ty*)env->GetLongField(v, JObject<Ty>::jInstField);
			if (!ptr) throw std::runtime_error{ "Object is already closed or not initialized." };
			return *ptr;
		}

		JniType toJava(JNIEnv* env, CppType&& v)
		{
			auto ptr = new CppType{ std::move(v) };
			if (!ptr) throw std::runtime_error{ std::string{ jclassName<Ty> } + ": failed to prepare c++ object." };
			auto ret = env->NewObject(JObject<Ty>::jClass, JObject<Ty>::jInitMethod, (jlong)ptr);
			if (!ret) throw std::runtime_error{ std::string{ jclassName<Ty> } + ": failed to construct object." };
			return ret;
		}

		template<class ... Args>
		CppType& callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<std::optional<Ty>, std::enable_if_t<std::is_base_of_v<JObject<Ty>, Ty>>>
	{
		using CppType = std::optional<Ty>;
		using JniType = jobject;
		static constexpr auto typeStr = StringConcat_v<svL, jclassName<Ty>, svSC>;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v) return {};
			if (!env->IsInstanceOf(v, JObject<Ty>::jClass)) throw std::runtime_error{ StringConcat_v<svNotInstanceOf, typeStr, svNullTerm>.data()};
			auto ptr = (Ty*)env->GetLongField(v, JObject<Ty>::jInstField);
			if (!ptr) throw std::runtime_error{ "Object is already closed or not initialized." };
			return *ptr;
		}

		JniType toJava(JNIEnv* env, CppType&& v)
		{
			if (!v) return nullptr;
			auto ptr = new CppType{ std::move(*v) };
			if (!ptr) throw std::runtime_error{ std::string{ jclassName<Ty> } + ": failed to prepare c++ object." };
			auto ret = env->NewObject(JObject<Ty>::jClass, JObject<Ty>::jInitMethod, (jlong)ptr);
			if (!ret) throw std::runtime_error{ std::string{ jclassName<Ty> } + ": failed to construct object." };
			return ret;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<Ty*, std::enable_if_t<std::is_base_of_v<JObject<Ty>, Ty>>>
	{
		using CppType = Ty*;
		using JniType = jobject;
		static constexpr auto typeStr = StringConcat_v<svL, jclassName<Ty>, svSC>;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v) return nullptr;
			if (!env->IsInstanceOf(v, JObject<Ty>::jClass)) throw std::runtime_error{ StringConcat_v<svNotInstanceOf, typeStr, svNullTerm>.data()};
			auto ptr = (Ty*)env->GetLongField(v, JObject<Ty>::jInstField);
			if (!ptr) throw std::runtime_error{ "Object is already closed or not initialized." };
			return ptr;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<JRef<Ty>, std::enable_if_t<std::is_base_of_v<JPureObject<Ty>, Ty>>>
	{
		using CppType = JRef<Ty>;
		using JniType = jobject;
		static constexpr auto typeStr = StringConcat_v<svL, jclassName<Ty>, svSC>;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			return CppType{ v };
		}

		JniType toJava(JNIEnv* env, CppType&& v)
		{
			return (jobject)v;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<std::vector<Ty>, std::enable_if_t<!std::is_integral_v<Ty> && !std::is_floating_point_v<Ty>>>
	{
		using CppType = std::vector<Ty>;
		using JniType = jobjectArray;
		static constexpr auto typeStr = StringConcat_v<svLBrack, svL, jclassName<Ty>, svSC>;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				return {};
			}
			size_t len = env->GetArrayLength(v);
			ValueBuilder<Ty> vb;
			std::vector<Ty> ret;
			ret.reserve(len);
			for (size_t i = 0; i < len; ++i)
			{
				ret.emplace_back(vb.fromJava(env, env->GetObjectArrayElement(v, i)));
			}
			return ret;
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			auto arr = env->NewObjectArray(v.size(), JObject<Ty>::jClass, nullptr);
			ValueBuilder<Ty> vb;
			for (size_t i = 0; i < v.size(); ++i)
			{
				env->SetObjectArrayElement(arr, i, vb.toJava(env, v[i]));
			}
			return arr;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<std::optional<std::vector<Ty>>, std::enable_if_t<!std::is_integral_v<Ty> && !std::is_floating_point_v<Ty>>>
	{
		using CppType = std::optional<std::vector<Ty>>;
		using JniType = jobjectArray;
		static constexpr auto typeStr = StringConcat_v<svLBrack, svL, jclassName<Ty>, svSC>;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v) return {};
			size_t len = env->GetArrayLength(v);
			ValueBuilder<Ty> vb;
			std::vector<Ty> ret;
			ret.reserve(len);
			for (size_t i = 0; i < len; ++i)
			{
				ret.emplace_back(vb.fromJava(env, env->GetObjectArrayElement(v, i)));
			}
			return ret;
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			if (!v) return nullptr;
			auto arr = env->NewObjectArray(v->size(), JObject<Ty>::jClass, nullptr);
			ValueBuilder<Ty> vb;
			for (size_t i = 0; i < v->size(); ++i)
			{
				env->SetObjectArrayElement(arr, i, vb.toJava(env, (*v)[i]));
			}
			return arr;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<std::vector<Ty>, std::enable_if_t<std::is_integral_v<Ty>>>
	{
		using CppType = std::vector<Ty>;
		using JniType = std::conditional_t<sizeof(Ty) == 1, jbyteArray,
			std::conditional_t<sizeof(Ty) == 2, jshortArray,
				std::conditional_t<sizeof(Ty) == 4, jintArray,
					std::conditional_t<sizeof(Ty) == 8, jlongArray, void>
				>
			>
		>;
		static constexpr auto typeStr = sizeof(Ty) == 1 ? "[B"sv : 
			sizeof(Ty) == 2 ? "[S"sv :
			sizeof(Ty) == 4 ? "[I"sv :
			sizeof(Ty) == 8 ? "[J"sv : "";

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				throw std::bad_optional_access{};
			}
			size_t len = env->GetArrayLength(v);
			std::vector<Ty> ret(len);
			if constexpr (sizeof(Ty) == 1)
			{
				auto ptr = env->GetByteArrayElements(v, nullptr);
				std::copy(ptr, ptr + len, ret.data());
			}
			else if constexpr (sizeof(Ty) == 2)
			{
				auto ptr = env->GetShortArrayElements(v, nullptr);
				std::copy(ptr, ptr + len, ret.data());
			}
			else if constexpr (sizeof(Ty) == 4)
			{
				auto ptr = env->GetIntArrayElements(v, nullptr);
				std::copy(ptr, ptr + len, ret.data());
			}
			else if constexpr (sizeof(Ty) == 8)
			{
				auto ptr = env->GetLongArrayElements(v, nullptr);
				std::copy(ptr, ptr + len, ret.data());
			}
			return ret;
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			if constexpr (sizeof(Ty) == 1)
			{
				auto arr = env->NewByteArray(v.size());
				auto ptr = env->GetByteArrayElements(arr, nullptr);
				std::copy(v.begin(), v.end(), ptr);
				return arr;
			}
			else if constexpr (sizeof(Ty) == 2)
			{
				auto arr = env->NewShortArray(v.size());
				auto ptr = env->GetShortArrayElements(arr, nullptr);
				std::copy(v.begin(), v.end(), ptr);
				return arr;
			}
			else if constexpr (sizeof(Ty) == 4)
			{
				auto arr = env->NewIntArray(v.size());
				auto ptr = env->GetIntArrayElements(arr, nullptr);
				std::copy(v.begin(), v.end(), ptr);
				return arr;
			}
			else if constexpr (sizeof(Ty) == 8)
			{
				auto arr = env->NewLongArray(v.size());
				auto ptr = env->GetLongArrayElements(arr, nullptr);
				std::copy(v.begin(), v.end(), ptr);
				return arr;
			}
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<>
	struct ValueBuilder<std::vector<char16_t>>
	{
		using CppType = std::vector<char16_t>;
		using JniType = jcharArray;
		static constexpr auto typeStr = "[C"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				throw std::bad_optional_access{};
			}
			size_t len = env->GetArrayLength(v);
			std::vector<char16_t> ret(len);
			auto ptr = env->GetCharArrayElements(v, nullptr);
			std::copy(ptr, ptr + len, ret.data());
			return ret;
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			auto arr = env->NewCharArray(v.size());
			auto ptr = env->GetCharArrayElements(arr, nullptr);
			std::copy(v.begin(), v.end(), ptr);
			return arr;
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<std::vector<Ty>, std::enable_if_t<std::is_floating_point_v<Ty>>>
	{
		using CppType = std::vector<Ty>;
		using JniType = std::conditional_t<sizeof(Ty) == 4, jfloatArray, jdoubleArray>;
		static constexpr auto typeStr = sizeof(Ty) == 4 ? "[F"sv : "[D"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				throw std::bad_optional_access{};
			}
			size_t len = env->GetArrayLength(v);
			std::vector<Ty> ret(len);
			if constexpr (sizeof(Ty) == 4)
			{
				auto ptr = env->GetFloatArrayElements(v, nullptr);
				std::copy(ptr, ptr + len, ret.data());
			}
			else
			{
				auto ptr = env->GetDoubleArrayElements(v, nullptr);
				std::copy(ptr, ptr + len, ret.data());
			}
			return ret;
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			if constexpr (sizeof(Ty) == 4)
			{
				auto arr = env->NewFloatArray(v.size());
				auto ptr = env->GetFloatArrayElements(arr, nullptr);
				std::copy(v.begin(), v.end(), ptr);
				return arr;
			}
			else
			{
				auto arr = env->NewDoubleArray(v.size());
				auto ptr = env->GetDoubleArrayElements(arr, nullptr);
				std::copy(v.begin(), v.end(), ptr);
				return arr;
			}
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	struct ValueBuilder<JIterator<Ty>>
	{
		using CppType = JIterator<Ty>;
		using JniType = jobject;
		static constexpr auto typeStr = "Ljava/util/Iterator;"sv;

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v)
			{
				return CppType{ v };
			}
			// The following line crashes clang compiler. I don't know why, but it's not necessary. So I commented it out.
			if (!env->IsInstanceOf(v, JIteratorBase::jClass)) throw std::runtime_error{ ""/*StringConcat_v<svNotInstanceOf, typeStr, svNullTerm>.data()*/};
			return CppType{ v };
		}

		template<class ... Args>
		CppType callMethod(JNIEnv* env, jobject obj, jmethodID methodID, Args&&... args)
		{
			auto ret = (JniType)env->CallObjectMethod(obj, methodID, std::forward<Args>(args)...);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				throw std::runtime_error{ "Java exception occurred." };
			}
			return fromJava(env, ret);
		}
	};

	template<class Ty>
	bool JIterator<Ty>::hasNext()
	{
		return ValueBuilder<bool>{}.callMethod(threadLocalEnv, inst, jHasNext);
	}

	template<class Ty>
	Ty JIterator<Ty>::next()
	{
		// Iterator::next() always returns Object, so we need to use CallObjectMethod
		auto ret = threadLocalEnv->CallObjectMethod(inst, jNext);
		if (threadLocalEnv->ExceptionCheck())
		{
			threadLocalEnv->ExceptionDescribe();
			threadLocalEnv->ExceptionClear();
			throw std::runtime_error{ "Java exception occurred." };
		}
		return ValueBuilder<Ty>{}.fromJava(threadLocalEnv, ret);
	}

	template<class Ty>
	Ty& JRef<Ty>::get()
	{
		return ValueBuilder<Ty>{}.fromJava(threadLocalEnv, inst);
	}

	template<class Ty>
	const Ty& JRef<Ty>::get() const
	{
		return ValueBuilder<Ty>{}.fromJava(threadLocalEnv, inst);
	}

	template<class Ty>
	using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<Ty>>;

	namespace detail
	{
		template <typename T>
		struct IsFunctionObjectImpl
		{
		private:
			using Yes = char(&)[1];
			using No = char(&)[2];

			struct Fallback
			{
				void operator()();
			};

			struct Derived : T, Fallback
			{
			};

			template <typename U, U>
			struct Check;

			template <typename>
			static Yes Test(...);

			template <typename C>
			static No Test(Check<void(Fallback::*)(), &C::operator()>*);

		public:
			static constexpr bool value{ sizeof(Test<Derived>(0)) == sizeof(Yes) };
		};
	
		template <class T, class ClsOverride>
		struct CppWrapperImpl;

		/* global function object */
		template <typename R, typename... Ts, class ClsOverride>
		struct CppWrapperImpl<R(Ts...), ClsOverride>
		{
			using Type = R(Ts...);
			using FunctionPointerType = R(*)(Ts...);
			using ReturnType = R;
			using ClassType = void;
			using ArgsTuple = std::tuple<Ts...>;

			template <std::size_t N>
			using Arg = typename std::tuple_element<N, ArgsTuple>::type;

			static const std::size_t nargs{ sizeof...(Ts) };

		};

		/* global function pointer */
		template <typename R, typename... Ts, class ClsOverride>
		struct CppWrapperImpl<R(*)(Ts...), ClsOverride>
		{
			using Type = R(*)(Ts...);
			using FunctionPointerType = R(*)(Ts...);
			using ReturnType = R;
			using ClassType = std::conditional_t<std::is_same_v<ClsOverride, void>, void, ClsOverride>;
			using ArgsTuple = std::tuple<Ts...>;

			template <std::size_t N>
			using Arg = typename std::tuple_element<N, ArgsTuple>::type;

			static const std::size_t nargs{ sizeof...(Ts) };

			static constexpr auto typeStr = StringConcat_v<svLParen, toJniTypeStr<remove_cvref_t<Ts>>..., svRParen, toJniTypeStr<R>>;

			using JniType = ToJniType<R>(*)(JNIEnv*, jobject, ToJniType<remove_cvref_t<Ts>>...);

			template<Type func>
			static constexpr JniType method()
			{
				return [](JNIEnv* env, jobject obj, ToJniType<remove_cvref_t<Ts>>... args) -> ToJniType<R>
				{
					return handleExc(env, [&]() -> ToJniType<R>
					{
						threadLocalEnv = env;
						if constexpr (std::is_same_v<R, void>)
						{
							(*func)(ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
						}
						else
						{
							auto ret = (*func)(ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
							return ValueBuilder<R>{}.toJava(env, std::move(ret));
						}
					});
				};
			}
		};

		/* member function pointer */
		template <typename C, typename R, typename... Ts, class ClsOverride>
		struct CppWrapperImpl<R(C::*)(Ts...), ClsOverride>
		{
			using Type = R(C::*)(Ts...);
			using FunctionPointerType = R(*)(C*, Ts...);
			using ReturnType = R;
			using ClassType = std::conditional_t<std::is_same_v<ClsOverride, void>, C, ClsOverride>;;
			using ArgsTuple = std::tuple<Ts...>;

			template <std::size_t N>
			using Arg = typename std::tuple_element<N, ArgsTuple>::type;

			static constexpr std::size_t nargs{ sizeof...(Ts) };

			static constexpr auto typeStr = StringConcat_v<svLParen, toJniTypeStr<remove_cvref_t<Ts>>..., svRParen, toJniTypeStr<R>>;

			using JniType = ToJniType<R>(*)(JNIEnv*, jobject, ToJniType<remove_cvref_t<Ts>>...);

			template<Type func>
			static constexpr JniType method()
			{
				static_assert(std::is_base_of_v<JObject<ClassType>, ClassType>, "Only methods of JObject can be registered.");

				return [](JNIEnv* env, jobject obj, ToJniType<remove_cvref_t<Ts>>... args) -> ToJniType<R>
				{
					return handleExc(env, [&]() -> ToJniType<R>
					{
						threadLocalEnv = env;
						auto ptr = (ClassType*)env->GetLongField(obj, JObject<ClassType>::jInstField);
						if (!ptr) throw std::runtime_error{ "Object is already closed or not initialized." };
						
						if constexpr (std::is_same_v<R, void>)
						{
							(ptr->*func)(ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
						}
						else
						{
							auto ret = (ptr->*func)(ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
							return ValueBuilder<R>{}.toJava(env, std::move(ret));
						}
					});
				};
			}
		};

		/* const member function pointer */
		template <typename C, typename R, typename... Ts, class ClsOverride>
		struct CppWrapperImpl<R(C::*)(Ts...) const, ClsOverride>
		{
			using Type = R(C::*)(Ts...) const;
			using FunctionPointerType = R(*)(C*, Ts...);
			using ReturnType = R;
			using ClassType = std::conditional_t<std::is_same_v<ClsOverride, void>, C, ClsOverride>;;
			using ArgsTuple = std::tuple<Ts...>;

			template <std::size_t N>
			using Arg = typename std::tuple_element<N, ArgsTuple>::type;

			static constexpr std::size_t nargs{ sizeof...(Ts) };

			static constexpr auto typeStr = StringConcat_v<svLParen, toJniTypeStr<remove_cvref_t<Ts>>..., svRParen, toJniTypeStr<R>>;

			using JniType = ToJniType<R>(*)(JNIEnv*, jobject, ToJniType<remove_cvref_t<Ts>>...);

			template<Type func>
			static constexpr JniType method()
			{
				static_assert(std::is_base_of_v<JObject<ClassType>, ClassType>, "Only methods of JObject can be registered.");

				return [](JNIEnv* env, jobject obj, ToJniType<remove_cvref_t<Ts>>... args) -> ToJniType<R>
				{
					return handleExc(env, [&]() -> ToJniType<R>
					{
						threadLocalEnv = env;
						auto ptr = (ClassType*)env->GetLongField(obj, JObject<ClassType>::jInstField);
						if (!ptr) throw std::runtime_error{ "Object is already closed or not initialized." };

						if constexpr (std::is_same_v<R, void>)
						{
							(ptr->*func)(ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
						}
						else
						{
							auto ret = (ptr->*func)(ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
							return ValueBuilder<R>{}.toJava(env, std::move(ret));
						}
					});
				};
			}
		};

		template <typename R, typename... Ts, class ClsOverride>
		struct CppWrapperImpl<R(*)(jobject, Ts...), ClsOverride>
		{
			using Type = R(*)(jobject, Ts...);
			using FunctionPointerType = R(*)(jobject, Ts...);
			using ReturnType = R;
			using ClassType = std::conditional_t<std::is_same_v<ClsOverride, void>, void, ClsOverride>;
			using ArgsTuple = std::tuple<Ts...>;

			template <std::size_t N>
			using Arg = typename std::tuple_element<N, ArgsTuple>::type;

			static const std::size_t nargs{ sizeof...(Ts) };

			static constexpr auto typeStr = StringConcat_v<svLParen, toJniTypeStr<remove_cvref_t<Ts>>..., svRParen, toJniTypeStr<R>>;

			using JniType = ToJniType<R>(*)(JNIEnv*, jobject, ToJniType<remove_cvref_t<Ts>>...);

			template<Type func>
			static constexpr JniType method()
			{
				return [](JNIEnv* env, jobject obj, ToJniType<remove_cvref_t<Ts>>... args) -> ToJniType<R>
				{
					return handleExc(env, [&]() -> ToJniType<R>
					{
						threadLocalEnv = env;
						if constexpr (std::is_same_v<R, void>)
						{
							(*func)(obj, ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
						}
						else
						{
							auto ret = (*func)(obj, ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
							return ValueBuilder<R>{}.toJava(env, std::move(ret));
						}
					});
				};
			}
		};

		/* member function pointer */
		template <typename C, typename R, typename... Ts, class ClsOverride>
		struct CppWrapperImpl<R(C::*)(JRef<C>, Ts...), ClsOverride>
		{
			using Type = R(C::*)(Ts...);
			using FunctionPointerType = R(*)(C*, JRef<C>, Ts...);
			using ReturnType = R;
			using ClassType = std::conditional_t<std::is_same_v<ClsOverride, void>, C, ClsOverride>;;
			using ArgsTuple = std::tuple<Ts...>;

			template <std::size_t N>
			using Arg = typename std::tuple_element<N, ArgsTuple>::type;

			static constexpr std::size_t nargs{ sizeof...(Ts) };

			static constexpr auto typeStr = StringConcat_v<svLParen, toJniTypeStr<remove_cvref_t<Ts>>..., svRParen, toJniTypeStr<R>>;

			using JniType = ToJniType<R>(*)(JNIEnv*, jobject, ToJniType<remove_cvref_t<Ts>>...);

			template<Type func>
			static constexpr JniType method()
			{
				static_assert(std::is_base_of_v<JObject<ClassType>, ClassType>, "Only methods of JObject can be registered.");

				return [](JNIEnv* env, jobject obj, ToJniType<remove_cvref_t<Ts>>... args) -> ToJniType<R>
				{
					return handleExc(env, [&]() -> ToJniType<R>
					{
						threadLocalEnv = env;
						auto ptr = (ClassType*)env->GetLongField(obj, JObject<ClassType>::jInstField);
						if (!ptr) throw std::runtime_error{ "Object is already closed or not initialized." };

						if constexpr (std::is_same_v<R, void>)
						{
							(ptr->*func)(JRef<C>{obj}, ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
						}
						else
						{
							auto ret = (ptr->*func)(JRef<C>{obj}, ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
							return ValueBuilder<R>{}.toJava(env, std::move(ret));
						}
					});
				};
			}
		};

		/* const member function pointer */
		template <typename C, typename R, typename... Ts, class ClsOverride>
		struct CppWrapperImpl<R(C::*)(JRef<C>, Ts...) const, ClsOverride>
		{
			using Type = R(C::*)(JRef<C>, Ts...) const;
			using FunctionPointerType = R(*)(C*, JRef<C>, Ts...);
			using ReturnType = R;
			using ClassType = std::conditional_t<std::is_same_v<ClsOverride, void>, C, ClsOverride>;;
			using ArgsTuple = std::tuple<Ts...>;

			template <std::size_t N>
			using Arg = typename std::tuple_element<N, ArgsTuple>::type;

			static constexpr std::size_t nargs{ sizeof...(Ts) };

			static constexpr auto typeStr = StringConcat_v<svLParen, toJniTypeStr<remove_cvref_t<Ts>>..., svRParen, toJniTypeStr<R>>;

			using JniType = ToJniType<R>(*)(JNIEnv*, jobject, ToJniType<remove_cvref_t<Ts>>...);

			template<Type func>
			static constexpr JniType method()
			{
				static_assert(std::is_base_of_v<JObject<ClassType>, ClassType>, "Only methods of JObject can be registered.");

				return [](JNIEnv* env, jobject obj, ToJniType<remove_cvref_t<Ts>>... args) -> ToJniType<R>
				{
					return handleExc(env, [&]() -> ToJniType<R>
					{
						threadLocalEnv = env;
						auto ptr = (ClassType*)env->GetLongField(obj, JObject<ClassType>::jInstField);
						if (!ptr) throw std::runtime_error{ "Object is already closed or not initialized." };

						if constexpr (std::is_same_v<R, void>)
						{
							(ptr->*func)(JRef<C>{obj}, ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
						}
						else
						{
							auto ret = (ptr->*func)(JRef<C>{obj}, ValueBuilder<remove_cvref_t<Ts>>{}.fromJava(env, args)...);
							return ValueBuilder<R>{}.toJava(env, std::move(ret));
						}
					});
				};
			}
		};

		/* member variable pointer */
		template <typename C, typename R, class ClsOverride>
		struct CppWrapperImpl<R(C::*), ClsOverride>
		{
			using Type = R(C::*);
			using ReturnType = R;
			using ClassType = std::conditional_t<std::is_same_v<ClsOverride, void>, C, ClsOverride>;
		};

	}

	template <typename T>
	struct IsFunctionObject : std::conditional<
		std::is_class<T>::value,
		detail::IsFunctionObjectImpl<T>,
		std::false_type
	>::type
	{
	};

	template <typename T, class ClsOverride = void, class = void>
	struct CppWrapper : detail::CppWrapperImpl<T, ClsOverride>
	{
	};

	template <typename T, class ClsOverride>
	struct CppWrapper<T, ClsOverride, std::enable_if_t<IsFunctionObject<T>::value>> :
		detail::CppWrapperImpl<decltype(&T::operator()), ClsOverride>
	{
	};

	template<class Func>
	inline auto handleExc(JNIEnv* env, Func&& func)
	{
		try
		{
			return func();
		}
		catch (const std::bad_optional_access& e)
		{
			jclass exc = env->FindClass("java/lang/NullPointerException");
			env->ThrowNew(exc, e.what());
		}
		catch (const std::invalid_argument& e)
		{
			jclass exc = env->FindClass("java/lang/IllegalArgumentException");
			env->ThrowNew(exc, e.what());
		}
		catch (const std::exception& e)
		{
			jclass exc = env->FindClass("java/lang/Exception");
			env->ThrowNew(exc, e.what());
		}

		if constexpr (!std::is_same_v<decltype(func()), void>)
		{
			return decltype(func()){};
		}
	}

	template<class Ty, class... Args>
	constexpr auto makeCtorDef()
	{
		using FuncPtr = void(*)(JNIEnv*, jobject, ToJniType<Args>...);

		return NativeMethod<FuncPtr>{ "ctor", StringConcat_v<svLParen, toJniTypeStr<Args>..., svRParen, svV, svNullTerm>.data(), 
			(FuncPtr)[](JNIEnv* env, jobject obj, ToJniType<Args>... args)
		{
			return handleExc(env, [&]()
			{
				threadLocalEnv = env;
				auto ptr = new Ty(ValueBuilder<Args>{}.fromJava(env, args)...);
				env->SetLongField(obj, JObject<Ty>::jInstField, (jlong)ptr);
				return;
			});
		} };
	}

	template<class Ty, class... Args>
	static constexpr NativeMethod ctorDef = makeCtorDef<Ty, Args...>();

	template<class Ty>
	constexpr auto makeDtorDef()
	{
		using FuncPtr = void(*)(JNIEnv*, jobject);

		return NativeMethod<FuncPtr>{ "close", "()V", (FuncPtr)[](JNIEnv* env, jobject obj)
		{
			return handleExc(env, [&]()
			{
				auto ptr = (Ty*)env->GetLongField(obj, JObject<Ty>::jInstField);
				if (ptr)
				{
					threadLocalEnv = env;
					delete ptr;
					env->SetLongField(obj, JObject<Ty>::jInstField, 0);
				}
				return;
			});
		} };
	}

	template<class Ty>
	static constexpr NativeMethod dtorDef = makeDtorDef<Ty>();

	template<class Ty, auto memFn>
	constexpr auto makeMethodDef()
	{
		using FuncPtr = decltype(CppWrapper<decltype(memFn), Ty>::template method<memFn>());
		return NativeMethod<FuncPtr>{ nullptr, CppWrapper<decltype(memFn), Ty>::typeStr.data(), CppWrapper<decltype(memFn), Ty>::template method<memFn>()};
	}

	template<class Ty, auto memFn>
	static constexpr NativeMethod methodDef = makeMethodDef<Ty, memFn>();

	template<class Ty, const auto& ... methods>
	class ClassDefinition
	{
		friend class Module;
	public:
		std::vector<const char*> methodNames;

		using Class = Ty;
		static_assert(std::is_base_of_v<JPureObject<Class>, Class>, "Only JObject has its ClassDefinition.");

		inline static std::array<JNINativeMethod, sizeof...(methods)> methodDefs{ ((JNINativeMethod)methods)... };

		constexpr ClassDefinition(const std::vector<const char*>& _methodNames = {}) : methodNames{ _methodNames } {}

		template<class... Args>
		constexpr ClassDefinition<Ty, methods..., ctorDef<Ty, Args...>> ctor() const
		{
			return { methodNames };
		}

		constexpr ClassDefinition<Ty, methods..., dtorDef<Ty>> dtor() const
		{
			return { methodNames };
		}

		template<auto memFn>
		constexpr ClassDefinition<Ty, methods..., methodDef<Ty, memFn>> method(const char* name) const
		{
			auto ret = ClassDefinition<Ty, methods..., methodDef<Ty, memFn>>{ methodNames };
			ret.methodNames.emplace_back(name);
			return ret;
		}
	};

	template<class Ty, auto... memPtrs>
	class DataClassDefinition
	{
		friend class Module;
	public:
		using Class = Ty;
		using MemPtrTypes = std::tuple<decltype(memPtrs)...>;
		static constexpr MemPtrTypes properties = std::make_tuple(memPtrs...);

		std::vector<const char*> propertyNames;
		
		static jclass jClass;
		static jmethodID jInitMethod;
		static std::array<jfieldID, sizeof...(memPtrs)> jFields;

		constexpr DataClassDefinition(const std::vector<const char*>& _propertyNames = {}) : propertyNames{ _propertyNames } {}

		template<auto newMemPtr>
		constexpr DataClassDefinition<Ty, memPtrs..., newMemPtr> property(const char* name) const
		{
			auto ret = DataClassDefinition<Ty, memPtrs..., newMemPtr>{ propertyNames };
			ret.propertyNames.emplace_back(name);
			return ret;
		}
	};

	template<class Ty, auto... memPtrs>
	jclass DataClassDefinition<Ty, memPtrs...>::jClass = nullptr;

	template<class Ty, auto... memPtrs>
	jmethodID DataClassDefinition<Ty, memPtrs...>::jInitMethod = nullptr;

	template<class Ty, auto... memPtrs>
	std::array<jfieldID, sizeof...(memPtrs)> DataClassDefinition<Ty, memPtrs...>::jFields;

	template<class Ty, auto... memPtrs>
	struct ValueBuilder<DataClassDefinition<Ty, memPtrs...>>
	{
		using DefTy = DataClassDefinition<Ty, memPtrs...>;
		using CppType = Ty;
		using JniType = jobject;

		template<class VTy>
		bool getProperty(JNIEnv* env, jobject obj, jfieldID field, VTy& v)
		{
			using JniFieldType = typename ValueBuilder<VTy>::JniType;
			if constexpr (std::is_same_v<JniFieldType, jbyte>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetByteField(obj, field));
			}
			else if constexpr (std::is_same_v<JniFieldType, jshort>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetShortField(obj, field));
			}
			else if constexpr (std::is_same_v<JniFieldType, jint>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetIntField(obj, field));
			}
			else if constexpr (std::is_same_v<JniFieldType, jlong>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetLongField(obj, field));
			}
			else if constexpr (std::is_same_v<JniFieldType, jfloat>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetFloatField(obj, field));
			}
			else if constexpr (std::is_same_v<JniFieldType, jdouble>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetDoubleField(obj, field));
			}
			else if constexpr (std::is_same_v<JniFieldType, jboolean>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetBooleanField(obj, field));
			}
			else if constexpr (std::is_same_v<JniFieldType, jchar>)
			{
				v = ValueBuilder<VTy>{}.fromJava(env, env->GetCharField(obj, field));
			}
			else
			{
				v = ValueBuilder<VTy>{}.fromJava(env, (JniFieldType)env->GetObjectField(obj, field));
			}
			return true;
		}

		template<size_t... idx>
		bool getProperties(JNIEnv* env, jobject obj, CppType& v, std::index_sequence<idx...>)
		{
			return (... && getProperty(env, obj, DefTy::jFields[idx], v.*memPtrs));
		}

		CppType fromJava(JNIEnv* env, JniType v)
		{
			if (!v) throw std::bad_optional_access{};
			if (!env->IsInstanceOf(v, DefTy::jClass)) throw std::runtime_error{ StringConcat_v<svNotInstanceOf, jclassName<CppType>, svNullTerm>.data()};
			CppType ret;
			if (!getProperties(env, v, ret, std::make_index_sequence<sizeof...(memPtrs)>{}))
			{
				throw std::runtime_error{ "Failed to get fields of " + std::string{jclassName<CppType>} + "." };
			}
			return ret;
		}

		template<class VTy>
		bool setProperty(JNIEnv* env, jobject obj, jfieldID field, const VTy& v)
		{
			using JniFieldType = typename ValueBuilder<VTy>::JniType;
			if constexpr (std::is_same_v<JniFieldType, jbyte>)
			{
				env->SetByteField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else if constexpr (std::is_same_v<JniFieldType, jshort>)
			{
				env->SetShortField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else if constexpr (std::is_same_v<JniFieldType, jint>)
			{
				env->SetIntField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else if constexpr (std::is_same_v<JniFieldType, jlong>)
			{
				env->SetLongField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else if constexpr (std::is_same_v<JniFieldType, jfloat>)
			{
				env->SetFloatField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else if constexpr (std::is_same_v<JniFieldType, jdouble>)
			{
				env->SetDoubleField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else if constexpr (std::is_same_v<JniFieldType, jboolean>)
			{
				env->SetBooleanField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else if constexpr (std::is_same_v<JniFieldType, jchar>)
			{
				env->SetCharField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			else
			{
				env->SetObjectField(obj, field, ValueBuilder<VTy>{}.toJava(env, v));
			}
			return true;
		}

		template<size_t... idx>
		bool setProperties(JNIEnv* env, jobject obj, const CppType& v, std::index_sequence<idx...>)
		{
			return (... && setProperty(env, obj, DefTy::jFields[idx], v.*memPtrs));
		}

		JniType toJava(JNIEnv* env, const CppType& v)
		{
			auto obj = env->NewObject(DefTy::jClass, DefTy::jInitMethod);
			if (!setProperties(env, obj, v, std::make_index_sequence<sizeof...(memPtrs)>{}))
			{
				throw std::runtime_error{ "Failed to set fields of " + std::string{jclassName<CppType>} + "." };
			}
			return obj;
		}
	};

	template<class Ty>
	constexpr auto define()
	{
		return ClassDefinition<Ty>{}.dtor();
	}

	template<class Ty>
	struct IsClassDefinition : std::false_type {};

	template<class Ty, const auto& ... methods>
	struct IsClassDefinition<ClassDefinition<Ty, methods...>> : std::true_type {};

	template<class Ty>
	struct IsDataClassDefinition : std::false_type {};

	template<class Ty, auto ... memPtrs>
	struct IsDataClassDefinition<DataClassDefinition<Ty, memPtrs...>> : std::true_type {};

	class Module
	{
		friend class ClassDefiner;
		int javaVersion;
		std::vector<const char*> addedClasses;

		template<class Type, class Class>
		bool setPropertyId(jfieldID& out, JNIEnv* env, jclass cls, const char* name, Type(Class::*ptr))
		{
			out = env->GetFieldID(cls, name, toJniTypeStr<Type>.data());
			if (!out) return false;
			return true;
		}

		template<class DefTy, size_t... idx, class... MemPtrTypes>
		bool fetchPropertyIds(DefTy& def, JNIEnv* env, jclass cls, std::index_sequence<idx...>, std::tuple<MemPtrTypes...> memPtrs)
		{
			return (... && setPropertyId(def.jFields[idx], env, cls, def.propertyNames[idx], std::get<idx>(memPtrs)));
		}

		template<class Definition>
		bool addClass(JNIEnv* env, Definition&& def)
		{
			using DefTy = remove_cvref_t<Definition>;

			static_assert(IsClassDefinition<DefTy>::value || IsDataClassDefinition<DefTy>::value,
				"Only ClassDefinition or DataClassDefinition can be registered."
			);

			if constexpr (IsClassDefinition<DefTy>::value)
			{
				auto cls = JPureObject<typename DefTy::Class>::jClass 
					= (jclass)env->NewGlobalRef(env->FindClass(jclassName<typename DefTy::Class>.data()));
				if (!cls) return false;

				if constexpr (std::is_base_of_v<JObject<typename DefTy::Class>, typename DefTy::Class>)
				{
					JObject<typename DefTy::Class>::jInstField = env->GetFieldID(cls, "_inst", "J");
					if (!JObject<typename DefTy::Class>::jInstField)
					{
						std::cerr << jclassName<typename DefTy::Class> << " has no `_inst` field." << std::endl;
						return false;
					}

					JObject<typename DefTy::Class>::jInitMethod = env->GetMethodID(cls, "<init>", "(J)V");
					if (!JObject<typename DefTy::Class>::jInitMethod)
					{
						std::cerr << jclassName<typename DefTy::Class> << " has no constructor with a long argument" << std::endl;
						return false;
					}

					auto defs = (JNINativeMethod*)def.methodDefs.data();
					auto size = def.methodDefs.size();

					size_t m = 0;
					for (size_t i = 0; i < size; ++i)
					{
						if (!defs[i].name)
						{
							defs[i].name = (char*)def.methodNames[m++];
						}
					}

					if (env->RegisterNatives(cls, defs, size) != JNI_OK) return false;
				}

				addedClasses.emplace_back(jclassName<typename DefTy::Class>.data());
				return true;
			}
			else
			{
				auto cls = JObject<typename DefTy::Class>::jClass
					= def.jClass = (jclass)env->NewGlobalRef(env->FindClass(jclassName<typename DefTy::Class>.data()));
				if (!cls) return false;

				def.jInitMethod = env->GetMethodID(cls, "<init>", "()V");
				if (!def.jInitMethod)
				{
					std::cerr << jclassName<typename DefTy::Class> << " has no default constructor." << std::endl;
					return false;
				}

				auto idx = std::make_index_sequence<std::tuple_size_v<typename DefTy::MemPtrTypes>>{};
				if (!fetchPropertyIds(def, env, cls, idx, def.properties)) return false;
				return true;
			}
		}

	public:

		Module(int _javaVersion) : javaVersion{ _javaVersion } {}

		template<class ...Args>
		jint load(JavaVM* vm, Args&&... args)
		{
			JNIEnv* env;
			if (vm->GetEnv((void**)&env, javaVersion) != JNI_OK) return -1;

			JIteratorBase::jClass = (jclass)env->NewGlobalRef(env->FindClass("java/util/Iterator"));
			JIteratorBase::jHasNext = env->GetMethodID(JIteratorBase::jClass, "hasNext", "()Z");
			JIteratorBase::jNext = env->GetMethodID(JIteratorBase::jClass, "next", "()Ljava/lang/Object;");

			if (!(... && addClass(env, std::forward<Args>(args)))) return -1;
			return javaVersion;
		}

		void unload(JavaVM* vm)
		{
			JNIEnv* env;
			if (vm->GetEnv((void**)&env, javaVersion) != JNI_OK) return;
			for (auto c : addedClasses)
			{
				auto cls = env->FindClass(c);
				if (!cls) return;

				env->UnregisterNatives(cls);
			}
		}
	};
}
