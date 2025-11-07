#pragma once
#include <iostream>
#include <memory>
#include <string>
#include <array>
#include <type_traits>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdio>

namespace kiwi
{
	namespace serializer
	{
		namespace detail
		{
			template<class _T> using Invoke = typename _T::type;

			template<size_t...> struct seq { using type = seq; };

			template<class _S1, class _S2> struct concat;

			template<size_t... _i1, size_t... _i2>
			struct concat<seq<_i1...>, seq<_i2...>>
				: seq<_i1..., (sizeof...(_i1) + _i2)...> {};

			template<class _S1, class _S2>
			using Concat = Invoke<concat<_S1, _S2>>;

			template<size_t _n> struct gen_seq;
			template<size_t _n> using GenSeq = Invoke<gen_seq<_n>>;

			template<size_t _n>
			struct gen_seq : Concat<GenSeq<_n / 2>, GenSeq<_n - _n / 2>> {};

			template<> struct gen_seq<0> : seq<> {};
			template<> struct gen_seq<1> : seq<0> {};

			template <size_t _n, size_t ... _is>
			std::array<char, _n - 1> to_array(const char(&a)[_n], seq<_is...>)
			{
				return { {a[_is]...} };
			}

			template <size_t _n>
			constexpr std::array<char, _n - 1> to_array(const char(&a)[_n])
			{
				return to_array(a, GenSeq<_n - 1>{});
			}

			template <size_t _n, size_t ... _is>
			std::array<char, _n> to_arrayz(const char(&a)[_n], seq<_is...>)
			{
				return { {a[_is]..., 0} };
			}

			template <size_t _n>
			constexpr std::array<char, _n> to_arrayz(const char(&a)[_n])
			{
				return to_arrayz(a, GenSeq<_n - 1>{});
			}

			template<typename _Class, typename _RetTy, typename ..._Args>
			_RetTy test_mf_c(_RetTy(_Class::* mf)(_Args...) const)
			{
				return _RetTy{};
			}

			template<typename> struct sfinae_true : std::true_type {};
			template<typename _Ty>
			static auto testSave(int)->sfinae_true<decltype(test_mf_c<_Ty, void, std::ostream&>(&_Ty::serializerWrite))>;
			template<typename _Ty>
			static auto testSave(long)->std::false_type;

			template<typename ... _Args>
			std::string format(const std::string& format, _Args ... args)
			{
				size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1;
				std::vector<char> buf(size);
				snprintf(buf.data(), size, format.c_str(), args ...);
				return std::string{ buf.data(), buf.data() + size - 1 };
			}
		}

		template<typename _Ty>
		struct hasSave : decltype(detail::testSave<_Ty>(0)){};

		template<class Ty>
		using remove_cr = typename std::remove_const<typename std::remove_reference<Ty>::type>::type;

		template<size_t _len>
		struct Key
		{
			std::array<char, _len> m;

			std::string str() const
			{
				return std::string{ m.begin(), m.end() };
			}

			Key(const std::array<char, _len>& _m) : m(_m)
			{
			}

			Key(std::array<char, _len>&& _m) : m(_m)
			{
			}

			Key(const char(&a)[_len + 1]) : Key{ detail::to_array(a) }
			{
			}
		};

		template<typename Ty>
		struct is_key : public std::false_type
		{
		};

		template<size_t _len>
		struct is_key<Key<_len>> : public std::true_type
		{
		};

		template<size_t _n>
		constexpr Key<_n - 1> toKey(const char(&a)[_n])
		{
			return Key<_n - 1>{detail::to_array(a)};
		}

		template<size_t _n>
		constexpr Key<_n> toKeyz(const char(&a)[_n])
		{
			return Key<_n>{detail::to_arrayz(a)};
		}

		template<class Ty, class = void>
		struct Serializer;

		template<class Ty, class S = Serializer<remove_cr<Ty>>> inline void writeToStream(std::ostream& ostr, const Ty& v);
		template<class Ty, class S = Serializer<remove_cr<Ty>>> inline void readFromStream(std::istream& istr, Ty& v);
		template<class Ty, class S = Serializer<remove_cr<Ty>>> inline Ty readFromStream(std::istream& istr);

		template<class Ty, class S>
		inline void writeToStream(std::ostream& ostr, const Ty& v)
		{
			S{}.write(ostr, v);
		}

		template<class Ty, class S>
		inline void readFromStream(std::istream& istr, Ty& v)
		{
			S{}.read(istr, v);
		}

		template<class Ty, class S>
		inline Ty readFromStream(std::istream& istr)
		{
			Ty v;
			S{}.read(istr, v);
			return v;
		}

		inline void writeMany(std::ostream& ostr)
		{
			// do nothing
		}

		template<class FirstTy, class ... RestTy>
		inline typename std::enable_if<
			!is_key<typename std::remove_reference<FirstTy>::type>::value
		>::type writeMany(std::ostream& ostr, FirstTy&& first, RestTy&&... rest)
		{
			writeToStream(ostr, std::forward<FirstTy>(first));
			writeMany(ostr, std::forward<RestTy>(rest)...);
		}

		template<size_t len, typename ... RestTy>
		inline void writeMany(std::ostream& ostr, const Key<len>& first, RestTy&&... rest)
		{
			ostr.write(first.m.data(), first.m.size());
			writeMany(ostr, std::forward<RestTy>(rest)...);
		}

		inline void readMany(std::istream& istr)
		{
			// do nothing
		}

		template<class FirstTy, class ... RestTy>
		inline typename std::enable_if<
			!is_key<typename std::remove_reference<FirstTy>::type>::value
		>::type readMany(std::istream& istr, FirstTy&& first, RestTy&&... rest)
		{
			readFromStream(istr, std::forward<FirstTy>(first));
			readMany(istr, std::forward<RestTy>(rest)...);
		}

		template<size_t len, class ... RestTy>
		inline void readMany(std::istream& istr, const Key<len>& first, RestTy&&... rest)
		{
			std::array<char, len> m;
			istr.read(m.data(), m.size());
			if (m != first.m)
			{
				throw SerializationException(std::string("'") + first.str() + std::string("' is needed but '") + std::string{ m.begin(), m.end() } + std::string("'"));
			}
			readMany(istr, std::forward<RestTy>(rest)...);
		}

		template<class Ty>
		struct Serializer<Ty, typename std::enable_if<std::is_fundamental<Ty>::value || std::is_enum<Ty>::value>::type>
		{
			void write(std::ostream& ostr, const Ty& v)
			{
				if (!ostr.write((const char*)&v, sizeof(Ty)))
					throw SerializationException(std::string("writing type '") + typeid(Ty).name() + std::string("' failed"));
			}

			void read(std::istream& istr, Ty& v)
			{
				if (!istr.read((char*)&v, sizeof(Ty)))
					throw SerializationException(std::string("reading type '") + typeid(Ty).name() + std::string("' failed"));
			}
		};

		template<typename _Ty>
		struct Serializer<_Ty, typename std::enable_if<hasSave<_Ty>::value>::type>
		{
			void write(std::ostream& ostr, const _Ty& v)
			{
				v.serializerWrite(ostr);
			}

			void read(std::istream& istr, _Ty& v)
			{
				v.serializerRead(istr);
			}
		};

		template<class Ty, class Alloc>
		struct Serializer<std::vector<Ty, Alloc>, typename std::enable_if<std::is_fundamental<Ty>::value || std::is_enum<Ty>::value>::type>
		{
			using VTy = std::vector<Ty, Alloc>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeToStream(ostr, (uint32_t)v.size());
				if (!ostr.write((const char*)v.data(), sizeof(Ty) * v.size()))
					throw SerializationException(std::string("writing type '") + typeid(Ty).name() + std::string("' is failed"));
			}

			void read(std::istream& istr, VTy& v)
			{
				auto size = readFromStream<uint32_t>(istr);
				v.resize(size);
				if (!istr.read((char*)v.data(), sizeof(Ty) * size))
					throw SerializationException(std::string("reading type '") + typeid(Ty).name() + std::string("' is failed"));
			}
		};

		template<class Ty, class Alloc>
		struct Serializer<std::vector<Ty, Alloc>, typename std::enable_if<!(std::is_fundamental<Ty>::value || std::is_enum<Ty>::value)>::type>
		{
			using VTy = std::vector<Ty, Alloc>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeToStream(ostr, (uint32_t)v.size());
				for (auto& e : v) Serializer<Ty>{}.write(ostr, e);
			}

			void read(std::istream& istr, VTy& v)
			{
				auto size = readFromStream<uint32_t>(istr);
				v.resize(size);
				for (auto& e : v) Serializer<Ty>{}.read(istr, e);
			}
		};

		template<class Ty, size_t n>
		struct Serializer<std::array<Ty, n>, typename std::enable_if<std::is_fundamental<Ty>::value>::type>
		{
			using VTy = std::array<Ty, n>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeToStream(ostr, (uint32_t)v.size());
				if (!ostr.write((const char*)v.data(), sizeof(Ty) * v.size()))
					throw SerializationException(std::string("writing type '") + typeid(Ty).name() + std::string("' is failed"));
			}

			void read(std::istream& istr, VTy& v)
			{
				auto size = readFromStream<uint32_t>(istr);
				if (n != size) throw SerializationException(detail::format("the size of array must be %zd, not %zd", n, size));
				if (!istr.read((char*)v.data(), sizeof(Ty) * size))
					throw SerializationException(std::string("reading type '") + typeid(Ty).name() + std::string("' is failed"));
			}
		};

		template<class Ty, size_t n>
		struct Serializer<std::array<Ty, n>, typename std::enable_if<!std::is_fundamental<Ty>::value>::type>
		{
			using VTy = std::array<Ty, n>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeToStream(ostr, (uint32_t)v.size());
				for (auto& e : v) Serializer<Ty>{}.write(ostr, e);
			}

			void read(std::istream& istr, VTy& v)
			{
				auto size = readFromStream<uint32_t>(istr);
				if (n != size) throw SerializationException(detail::format("the size of array must be %zd, not %zd", n, size));
				for (auto& e : v) Serializer<Ty>{}.read(istr, e);
			}
		};

		template<class Ty, class Traits, class Alloc>
		struct Serializer<std::basic_string<Ty, Traits, Alloc>>
		{
			using VTy = std::basic_string<Ty, Traits, Alloc>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeToStream(ostr, (uint32_t)v.size());
				if (!ostr.write((const char*)v.data(), sizeof(Ty) * v.size()))
					throw SerializationException(std::string("writing type '") + typeid(Ty).name() + std::string("' is failed"));
			}

			void read(std::istream& istr, VTy& v)
			{
				auto size = readFromStream<uint32_t>(istr);
				v.resize(size);
				if (!istr.read((char*)v.data(), sizeof(Ty) * size))
					throw SerializationException(std::string("reading type '") + typeid(Ty).name() + std::string("' is failed"));
			}
		};

		template<class _Ty1, class Ty2>
		struct Serializer<std::pair<_Ty1, Ty2>>
		{
			using VTy = std::pair<_Ty1, Ty2>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeMany(ostr, v.first, v.second);
			}

			void read(std::istream& istr, VTy& v)
			{
				readMany(istr, v.first, v.second);
			}
		};

		template<class _Ty1, class Ty2>
		struct Serializer<std::unordered_map<_Ty1, Ty2>>
		{
			using VTy = std::unordered_map<_Ty1, Ty2>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeToStream(ostr, (uint32_t)v.size());
				for (auto& e : v) writeToStream(ostr, e);
			}

			void read(std::istream& istr, VTy& v)
			{
				auto size = readFromStream<uint32_t>(istr);
				v.clear();
				for (size_t i = 0; i < size; ++i)
				{
					v.emplace(readFromStream<std::pair<_Ty1, Ty2>>(istr));
				}
			}
		};

		template<class _Ty1, class Ty2>
		struct Serializer<std::map<_Ty1, Ty2>>
		{
			using VTy = std::map<_Ty1, Ty2>;
			void write(std::ostream& ostr, const VTy& v)
			{
				writeToStream(ostr, (uint32_t)v.size());
				for (auto& e : v) writeToStream(ostr, e);
			}

			void read(std::istream& istr, VTy& v)
			{
				auto size = readFromStream<uint32_t>(istr);
				v.clear();
				for (size_t i = 0; i < size; ++i)
				{
					v.emplace(readFromStream<std::pair<_Ty1, Ty2>>(istr));
				}
			}
		};
	}
}

#define DEFINE_SERIALIZER(...) void serializerRead(std::istream& istr)\
{\
	kiwi::serializer::readMany(istr, __VA_ARGS__);\
}\
void serializerWrite(std::ostream& ostr) const\
{\
	kiwi::serializer::writeMany(ostr, __VA_ARGS__);\
}

#define DECLARE_SERIALIZER(...) void serializerRead(std::istream& istr);\
void serializerWrite(std::ostream& ostr) const

#define DEFINE_SERIALIZER_OUTSIDE(NS, ...) void NS::serializerRead(std::istream& istr)\
{\
	kiwi::serializer::readMany(istr, __VA_ARGS__);\
}\
void NS::serializerWrite(std::ostream& ostr) const\
{\
	kiwi::serializer::writeMany(ostr, __VA_ARGS__);\
}
