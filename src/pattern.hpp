#pragma once

#include <array>
#include <iterator>
#include <utility>

namespace pattern
{
	namespace chr
	{
		template<int _chr>
		class OneChar
		{
		public:
			enum { chr = _chr };

			bool test(int c) const
			{
				return c == chr;
			}
		};

		template<int _front, int _back>
		class CharRange
		{
		public:
			enum { front = _front, back = _back };

			bool test(int c) const
			{
				return front <= c && c <= back;
			}
		};

		template<typename... _Subsets>
		class CharOr;

		template<>
		class CharOr<>
		{
		public:
			bool test(int c) const
			{
				return false;
			}
		};

		template<typename _Subset, typename ..._SubsetRest>
		class CharOr<_Subset, _SubsetRest...>
		{
		public:
			bool test(int c) const
			{
				return _Subset{}.test(c) || CharOr<_SubsetRest...>{}.test(c);
			}
		};

		template<typename _Ty>
		struct CharTrait
		{
		};

		template<int _chr>
		struct CharTrait<OneChar<_chr>>
		{
			enum { lowest = _chr, highest = _chr, span = 1 };
		};

		template<int _front, int _back>
		struct CharTrait<CharRange<_front, _back>>
		{
			enum { lowest = _front, highest = _back, span = highest - lowest + 1 };
		};

		template<typename _Subset>
		struct CharTrait<CharOr<_Subset>> : public CharTrait<_Subset>
		{
		};

		template<typename _Subset1, typename _Subset2, typename ... _SubsetRest>
		struct CharTrait<CharOr<_Subset1, _Subset2, _SubsetRest...>>
		{
		private:
			enum { 
				l1 = CharTrait<_Subset1>::lowest, 
				h1 = CharTrait<_Subset1>::highest,
				l2 = CharTrait<CharOr<_Subset2, _SubsetRest...>>::lowest, 
				h2 = CharTrait<CharOr<_Subset2, _SubsetRest...>>::highest 
			};
		public:
			enum { lowest = l1 < l2 ? l1 : l2, highest = h1 < h2 ? h2 : h1, span = highest - lowest + 1 };
		};

		namespace detail
		{

			template<typename ..._Subsets>
			struct LUTSetter
			{
			};

			template<>
			struct LUTSetter<>
			{
				void operator()(int8_t* arr) const
				{
				}
			};

			template<typename _Subset, typename... _SubsetRest>
			struct LUTSetter<_Subset, _SubsetRest...>
			{
				void operator()(int8_t* arr) const
				{
					for (int c = CharTrait<_Subset>::lowest; c <= CharTrait<_Subset>::highest; ++c)
					{
						arr[c] = 1;
					}

					LUTSetter<_SubsetRest...>{}(arr);
				}
			};
		}

		template<bool _negate, typename _Ty>
		class CharSet
		{
		public:
			bool test(int c) const
			{
				if(_negate) return !_Ty{}.test(c);
				return _Ty{}.test(c);
			}
		};

		template<bool _negate, typename _S1, typename _S2, typename _S3, typename ..._SRest>
		class CharSet<_negate, CharOr<_S1, _S2, _S3, _SRest...>>
		{
			using Trait = CharTrait<CharOr<_S1, _S2, _S3, _SRest...>>;
			std::array<int8_t, Trait::span> lut = { { 0, } };
		public:
			CharSet()
			{
				detail::LUTSetter<_S1, _S2, _S3, _SRest...>{}(lut.data() - Trait::lowest);
			}

			bool test(int c) const
			{
				if (_negate)
				{
					if (c < Trait::lowest
						|| c > Trait::highest) return true;
					return !lut[c - Trait::lowest];
				}
				if (c < Trait::lowest
					|| c > Trait::highest) return false;
				return !!lut[c - Trait::lowest];
			}
		};

		namespace detail
		{
			template<typename _Ty1, typename _Ty2>
			struct OrConcat
			{
				using type = CharOr<_Ty1, _Ty2>;
			};

			template<typename _Ty1, typename ..._Subsets>
			struct OrConcat<_Ty1, CharOr<_Subsets...>>
			{
				using type = CharOr<_Ty1, _Subsets...>;
			};

			template<int ..._chrs>
			struct CharSetParserImpl;

			template<int ..._rest>
			struct CharSetParserImpl<0, _rest...>
			{
				using type = CharOr<>;
			};

			template<>
			struct CharSetParserImpl<>
			{
				using type = CharOr<>;
			};

			template<int _c1, int _c2, int ..._rest>
			struct CharSetParserImpl<_c1, '-', _c2, _rest...>
			{
				static_assert(_c1 < _c2, "A range 'x-y' should satify x < y");

				using type = typename OrConcat<
					CharRange<_c1, _c2>,
					typename CharSetParserImpl<_rest...>::type
				>::type;
			};

			template<int _chr, int ..._rest>
			struct CharSetParserImpl<_chr, _rest...>
			{
				using type = typename OrConcat<
					OneChar<_chr>,
					typename CharSetParserImpl<_rest...>::type
				>::type;
			};

		}

		template<int ..._chrs>
		struct CharSetParser
		{
			using type = CharSet<false, typename detail::CharSetParserImpl<_chrs...>::type>;
		};

		template<int ..._chrs>
		struct CharSetParser<'^', _chrs...>
		{
			using type = CharSet<true, typename detail::CharSetParserImpl<_chrs...>::type>;
		};
	}

	namespace detail
	{
		template<class T> using Invoke = typename T::type;

		template<int...> struct seq { using type = seq; };

		template<class S1, class S2> struct concat;

		template<int... I1, int... I2>
		struct concat<seq<I1...>, seq<I2...>>
			: seq<I1..., I2...> {};

		template<class S1, class S2>
		using Concat = Invoke<concat<S1, S2>>;

		template<int _offset, int _n> struct gen_seq_offset;
		template<int _offset, int _n> using GenSeqOffset = Invoke<gen_seq_offset<_offset, _n>>;
		template<int _begin, int _end> using GenSeqRange = Invoke < gen_seq_offset<_begin, _begin < _end ? _end - _begin : 0>>;
		template<int _end> using GenSeq = Invoke<gen_seq_offset<0, _end>>;

		template<int _offset, int _n>
		struct gen_seq_offset : Concat<GenSeqOffset<_offset, _n / 2>, GenSeqOffset<_offset + _n / 2, _n - _n / 2>> {};

		template<int _offset>
		struct gen_seq_offset<_offset, 0> : seq<> {};

		template<int _offset>
		struct gen_seq_offset<_offset, 1> : seq<_offset> {};

		template<int _n, typename _Seq>
		struct SelectAt;

		template<int _arg1, int ..._args>
		struct SelectAt<0, seq<_arg1, _args...>>
		{
			enum { value = _arg1 };
		};

		template<int _n, int _arg1, int ..._args>
		struct SelectAt<_n, seq<_arg1, _args...>>
		{
			enum { value = SelectAt<_n - 1, seq<_args...>>::value };
		};

		template<int ..._chrs>
		struct Equal;

		template<>
		struct Equal<>
		{
			template<typename _Iter>
			bool operator()(_Iter it) const
			{
				return true;
			}
		};

		template<int _c1, int ..._crest>
		struct Equal<_c1, _crest...>
		{
			template<typename _Iter>
			bool operator()(_Iter it) const
			{
				if (*it != _c1) return false;
				return Equal<_crest...>{}(++it);
			}
		};
	}

	template<int ..._chrs>
	class StrSeq
	{
	public:
		template<typename _Iter>
		std::pair<_Iter, bool> progress(_Iter first, _Iter last) const
		{
			if (std::distance(first, last) >= sizeof...(_chrs) 
				&& detail::Equal<_chrs...>{}(first))
			{
				std::advance(first, sizeof...(_chrs));
				return std::make_pair(first, true);
			}
			return std::make_pair(first, false);
		}
	};

	template<typename ..._CharSets>
	class CharSetSeq;

	template<>
	class CharSetSeq<>
	{
	public:
		template<typename _Iter>
		std::pair<_Iter, bool> progress(_Iter first, _Iter last) const
		{
			return std::make_pair(first, true);
		}
	};

	template<typename _Cs1, typename ..._CsRest>
	class CharSetSeq<_Cs1, _CsRest...>
	{
		_Cs1 cs1;
		CharSetSeq<_CsRest...> csRest;
	public:
		template<typename _Iter>
		std::pair<_Iter, bool> progress(_Iter first, _Iter last) const
		{
			if (first == last || !cs1.test(*first)) return std::make_pair(first, false);
			return csRest.progress(++first, last);
		}
	};

	template<typename ..._Ty>
	class ATSeq;

	template<>
	class ATSeq<>
	{
	public:
		template<typename _Iter>
		std::pair<_Iter, bool> progress(_Iter first, _Iter last) const
		{
			return std::make_pair(first, true);
		}
	};

	template<typename _Ty>
	class ATSeq<_Ty> : public _Ty
	{
	};

	template<typename _Ty>
	class ATSeq<_Ty, ATSeq<>> : public _Ty
	{
	};

	template<typename _Ty1, typename _Ty2, typename ..._TyRest>
	class ATSeq<_Ty1, _Ty2, _TyRest...>
	{
		_Ty1 t1;
		ATSeq<_Ty2, _TyRest...> tRest;
	public:
		template<typename _Iter>
		std::pair<_Iter, bool> progress(_Iter first, _Iter last) const
		{
			std::pair<_Iter, bool> r;
			if(first == last || !(r = t1.progress(first, last)).second) return std::make_pair(first, false);
			first = r.first;
			return tRest.progress(first, last);
		}
	};

	enum { repeat_max = 0x7FFFFFFF };

	template<typename _Ty, int _repeatA, int _repeatB = repeat_max>
	class Quantifier
	{
		_Ty t1;
	public:
		template<typename _Iter>
		std::pair<_Iter, bool> progress(_Iter first, _Iter last) const
		{
			return t1.progress(first, last);
		}
	};

	template<typename ..._Ty>
	class Automaton
	{
	public:
		template<typename _Iter>
		std::pair<_Iter, bool> progress(_Iter first, _Iter last) const
		{
		}
	};

	namespace detail
	{
		enum class ParseType { strseq, charsetseq };

		template<int _c>
		struct IsNormal
		{
			enum { value = !(_c == '[' 
				|| _c == '+' || _c == '*' || _c == '?'
				|| _c == '(' || _c == ')') };
		};

		template<int ..._chrs>
		struct CountNormal;

		template<>
		struct CountNormal<>
		{
			enum { value = 0 };
		};

		template<int _c1, int ..._chrs>
		struct CountNormal<_c1, _chrs...>
		{
			enum { value = IsNormal<_c1>::value ? (CountNormal<_chrs...>::value + 1) : 0 };
		};

		template<int _target, int ..._chrs>
		struct FindChr;

		template<int _target>
		struct FindChr<_target>
		{
			enum { value = 0 };
		};

		template<int _target, int _c1, int ..._chrs>
		struct FindChr<_target, _c1, _chrs...>
		{
			enum { value = _c1 == _target ? 0 : FindChr<_target, _chrs...>::value + 1 };
		};
	}

	template<typename = void, int ..._chrs>
	struct Parser;

	template<>
	struct Parser<void>
	{
		using type = ATSeq<>;
	};

	template<typename _Seq, typename _Chrs>
	struct CutStrSeq;

	template<int ..._is, typename _Pack>
	struct CutStrSeq<detail::seq<_is...>, _Pack> : public StrSeq<detail::SelectAt<_is, _Pack>::value...>
	{
	};

	template<typename _Seq, typename _Chrs>
	struct CutCharSetParser;

	template<int ..._is, typename _Pack>
	struct CutCharSetParser<detail::seq<_is...>, _Pack> : public chr::CharSetParser<detail::SelectAt<_is, _Pack>::value...>
	{
	};

	template<int ..._chrs>
	using CutSZCharSetParser = CutCharSetParser<detail::GenSeq<detail::FindChr<0, _chrs...>::value>, detail::seq<_chrs...>>;

	template<typename _Seq, typename _Chrs>
	struct CutParser;

	template<int ..._is, typename _Pack>
	struct CutParser<detail::seq<_is...>, _Pack> : public Parser<void, detail::SelectAt<_is, _Pack>::value...>
	{
	};

	template<int _c1, int ..._cRest>
	struct Parser<typename std::enable_if<detail::IsNormal<_c1>::value>::type, _c1, _cRest...>
	{
		enum { end_of_chunk = detail::CountNormal<_c1, _cRest...>::value };
		using type = ATSeq<
			CutStrSeq<
				detail::GenSeq<end_of_chunk>, 
				detail::seq<_c1, _cRest...>
			>,
			typename CutParser<
				detail::GenSeqRange<end_of_chunk, sizeof...(_cRest) + 1>, 
				detail::seq<_c1, _cRest...>
			>::type
		>;
	};

	template<int _c1, int ..._cRest>
	struct Parser<typename std::enable_if<_c1 == '['>::type, _c1, _cRest...>
	{
		enum { end_of_chunk = detail::FindChr<']', _c1, _cRest...>::value };
		
		static_assert(end_of_chunk < sizeof...(_cRest) + 1, "Unpaired ']'");

		using type = ATSeq<
			CharSetSeq<typename CutCharSetParser<
				detail::GenSeqRange<1, end_of_chunk>,
				detail::seq<_c1, _cRest...>
			>::type>,
			typename CutParser<
				detail::GenSeqRange<end_of_chunk + 1, sizeof...(_cRest) + 1>,
				detail::seq<_c1, _cRest...>
			>::type
		>;
	};

	template<int _c1, int ..._cRest>
	struct Parser<typename std::enable_if<_c1 == '('>::type, _c1, _cRest...>
	{
		enum { end_of_chunk = detail::FindChr<')', _c1, _cRest...>::value };

		static_assert(end_of_chunk < sizeof...(_cRest) + 1, "Unpaired ')'");

		using type = ATSeq<
			typename CutParser<
				detail::GenSeqRange<1, end_of_chunk>,
				detail::seq<_c1, _cRest...>
			>::type,
			typename CutParser<
				detail::GenSeqRange<end_of_chunk + 1, sizeof...(_cRest) + 1>,
				detail::seq<_c1, _cRest...>
			>::type
		>;
	};
	template<int _c1, int ..._cRest>
	struct Parser<typename std::enable_if<_c1 == '+'>::type, _c1, _cRest...>
	{
		static_assert(_c1 != '+', "cannot start with '+'");
	};

	template<int _c1, int ..._cRest>
	struct Parser<typename std::enable_if<_c1 == '*'>::type, _c1, _cRest...>
	{
		static_assert(_c1 != '*', "cannot start with '*'");
	};

	template<int _c1, int ..._cRest>
	struct Parser<typename std::enable_if<_c1 == '?'>::type, _c1, _cRest...>
	{
		static_assert(_c1 != '?', "cannot start with '?'");
	};

	template<int ..._chrs>
	using CutSZParser = CutParser<detail::GenSeq<detail::FindChr<0, _chrs...>::value>, detail::seq<_chrs...>>;
}

#define PP_GET_1(str, i) \
(sizeof(str) > (i) ? str[(i)] : 0)

#define PP_GET_4(str, i) \
PP_GET_1(str, i+0),  \
PP_GET_1(str, i+1),  \
PP_GET_1(str, i+2),  \
PP_GET_1(str, i+3)

#define PP_GET_16(str, i) \
PP_GET_4(str, i+0),   \
PP_GET_4(str, i+4),   \
PP_GET_4(str, i+8),   \
PP_GET_4(str, i+12)

#define PP_GET_64(str, i) \
PP_GET_16(str, i+0),  \
PP_GET_16(str, i+16), \
PP_GET_16(str, i+32), \
PP_GET_16(str, i+48)

#define PP_GET_256(str, i) \
PP_GET_64(str, i+0),  \
PP_GET_64(str, i+64), \
PP_GET_64(str, i+128), \
PP_GET_64(str, i+192)
