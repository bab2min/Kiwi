#pragma once
#include <cstdint>
#include <cstddef>
#include <tuple>

namespace kiwi
{
	namespace tp
	{
		template<size_t a, size_t b>
		struct gcd
		{
			static constexpr size_t value = gcd<b, a% b>::value;
		};

		template<size_t a>
		struct gcd<a, 0>
		{
			static constexpr size_t value = a;
		};

		template<size_t a, size_t b>
		struct lcm
		{
			static constexpr size_t value = a * b / gcd<a, b>::value;
		};

		template<class _T> using Invoke = typename _T::type;

		template<std::ptrdiff_t...> struct seq { using type = seq; };

		template<class _S1, class _S2> struct concat;

		template<std::ptrdiff_t... _i1, std::ptrdiff_t... _i2>
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

		template<class Ty>
		struct SeqSize;

		template<std::ptrdiff_t ..._i>
		struct SeqSize<seq<_i...>>
		{
			static constexpr size_t value = sizeof...(_i);
		};

		template<class Ty>
		struct SeqMax
		{
			static constexpr std::ptrdiff_t value = 0;
		};

		template<std::ptrdiff_t i>
		struct SeqMax<seq<i>>
		{
			static constexpr std::ptrdiff_t value = i;
		};

		template<std::ptrdiff_t i, std::ptrdiff_t ...j>
		struct SeqMax<seq<i, j...>>
		{
			static constexpr std::ptrdiff_t value = (i > SeqMax<seq<j...>>::value) ? i : SeqMax<seq<j...>>::value;
		};

		template<size_t n, class Seq, std::ptrdiff_t ..._j>
		struct slice;

		template<size_t n, class Seq, std::ptrdiff_t ..._j>
		using Slice = Invoke<slice<n, Seq, _j...>>;

		template<size_t n, std::ptrdiff_t first, std::ptrdiff_t ..._i, std::ptrdiff_t ..._j>
		struct slice<n, seq<first, _i...>, _j...>
		{
			using type = Slice<n - 1, seq<_i...>, _j..., first>;
		};

		template<std::ptrdiff_t first, std::ptrdiff_t ..._i, std::ptrdiff_t ..._j>
		struct slice<0, seq<first, _i...>, _j...>
		{
			using type = seq<_j...>;
		};

		template<std::ptrdiff_t ..._j>
		struct slice<0, seq<>, _j...>
		{
			using type = seq<_j...>;
		};

		template<size_t n, class Seq, std::ptrdiff_t ...j>
		struct get;

		template<size_t n, std::ptrdiff_t first, std::ptrdiff_t ..._i>
		struct get<n, seq<first, _i...>> : get<n - 1, seq<_i...>>
		{
		};

		template<std::ptrdiff_t first, std::ptrdiff_t ..._i>
		struct get<0, seq<first, _i...>> : std::integral_constant<std::ptrdiff_t, first>
		{
		};

		template<>
		struct get<0, seq<>>
		{
		};

		namespace detail
		{
			template<std::ptrdiff_t... Ns, typename... Ts>
			auto tail(seq<Ns...>, const std::tuple<Ts...>& t) -> decltype(std::make_tuple(std::get<Ns + 1>(t)...))
			{
				return std::make_tuple(std::get<Ns + 1>(t)...);
			}
		}

		template<typename... Ts>
		auto tuple_tail(const std::tuple<Ts...>& t) -> decltype(detail::tail(gen_seq<sizeof...(Ts) - 1>{}, t))
		{
			return detail::tail(gen_seq<sizeof...(Ts) - 1>{}, t);
		}

		template<class ValTy, class SeqTy>
		class Table
		{
			std::array<ValTy, SeqMax<SeqTy>::value + 1> table;

			template<class ValGetter>
			void set(seq<>)
			{
			}

			template<class ValGetter, std::ptrdiff_t i, std::ptrdiff_t ...j>
			void set(seq<i, j...>)
			{
				table[i] = ValGetter::template Wrapper<i>::value;
				set<ValGetter>(seq<j...>{});
			}

		public:
			template<class ValGetter>
			Table(ValGetter)
			{
				set<ValGetter>(SeqTy{});
			}

			constexpr ValTy operator[](std::ptrdiff_t idx) const
			{
				if (idx < 0 || (size_t)idx >= table.size()) return ValTy{};
				return table[idx];
			}
		};
	}


	template<class IntTy>
	struct SignedType { using type = IntTy; };

	template<>
	struct SignedType<uint8_t> { using type = int8_t; };

	template<>
	struct SignedType<uint16_t> { using type = int16_t; };

	template<>
	struct SignedType<uint32_t> { using type = int32_t; };

	template<>
	struct SignedType<uint64_t> { using type = int64_t; };

	template<>
	struct SignedType<char16_t> { using type = int16_t; };


	template<class IntTy>
	struct UnsignedType { using type = IntTy; };

	template<>
	struct UnsignedType<int8_t> { using type = uint8_t; };

	template<>
	struct UnsignedType<int16_t> { using type = uint16_t; };

	template<>
	struct UnsignedType<int32_t> { using type = uint32_t; };

	template<>
	struct UnsignedType<int64_t> { using type = uint64_t; };

	template<>
	struct UnsignedType<char16_t> { using type = uint16_t; };
}

