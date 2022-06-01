#pragma once

#include <vector>
#include <algorithm>
#include <type_traits>
#include <tuple>
#include <functional>

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
			template<class...> struct conjunction : std::true_type {};

			template<class B1> struct conjunction<B1> : B1 {};

			template<class B1, class... Bn>
			struct conjunction<B1, Bn...>
				: std::conditional<bool(B1::value), conjunction<Bn...>, B1>::type
			{};

			template<class T1, class T2>
			class MovingPair
			{
			public:
				T1 first;
				T2 second;

				using first_type = T1;
				using second_type = T2;

				constexpr MovingPair() {}

				template <class U1 = T1, class U2 = T2,
					typename std::enable_if<conjunction<
						std::is_convertible<const U1&, U1>, std::is_convertible<const U2&, U2>
					>::value, int>::type = 0>
				constexpr MovingPair(const T1& _Val1, const T2& _Val2)
					: first(_Val1), second(_Val2) 
				{}

				template <class U1 = T1, class U2 = T2,
					typename std::enable_if<!conjunction<
						std::is_convertible<const U1&, U1>, std::is_convertible<const U2&, U2>
					>::value, int>::type = 0>
				constexpr explicit MovingPair(const T1& _Val1, const T2& _Val2)
					: first(_Val1), second(_Val2) 
				{}

				template <class U1, class U2,
					typename std::enable_if<conjunction<
					std::is_convertible<U1, T1>, std::is_convertible<U2, T2>>::value,
					int>::type = 0>
				constexpr MovingPair(U1&& _Val1, U2&& _Val2)
					: first(std::forward<U1>(_Val1)), second(std::forward<U2>(_Val2)) 
				{}

				template <class U1, class U2,
					typename std::enable_if<!conjunction<
						std::is_convertible<U1, T1>, std::is_convertible<U2, T2>
					>::value, int>::type = 0>
				constexpr explicit MovingPair(U1&& _Val1, U2&& _Val2)
					: first(std::forward<U1>(_Val1)), second(std::forward<U2>(_Val2)) 
				{}

				template <class U1, class U2,
					typename std::enable_if<conjunction<
						std::is_convertible<const U1&, T1>, std::is_convertible<const U2&, T2>
					>::value, int>::type = 0>
				constexpr MovingPair(const MovingPair<U1, U2>& o)
					: first(o.first), second(o.second) 
				{}

				template <class U1, class U2,
					typename std::enable_if<!conjunction<
						std::is_convertible<const U1&, T1>, std::is_convertible<const U2&, T2>
					>::value, int>::type = 0>
				constexpr explicit MovingPair(const MovingPair<U1, U2>& o)
					: first(o.first), second(o.second) 
				{}

				template <class U1, class U2,
					typename std::enable_if<conjunction<
						std::is_convertible<U1, T1>, std::is_convertible<U2, T2>
					>::value, int>::type = 0>
				constexpr MovingPair(MovingPair<U1, U2>&& o)
					: first(std::move(o.first)), second(std::move(o.second)) 
				{}

				template <class U1, class U2,
					typename std::enable_if<!conjunction<
						std::is_convertible<U1, T1>, std::is_convertible<U2, T2>
					>::value, int>::type = 0>
				constexpr explicit MovingPair(MovingPair<U1, U2>&& o) 
					: first(std::move(o.first)), second(std::move(o.second)) 
				{}

				template <class U1, class U2>
				MovingPair& operator=(const MovingPair<U1, U2>& o)
				{
					first = o.first;
					second = o.second;
					return *this;
				}

				template <class U1, class U2>
				MovingPair& operator=(MovingPair<U1, U2>&& o)
				{
					first = std::move(o.first);
					second = std::move(o.second);
					return *this;
				}

				template<class U1, class U2>
				bool operator<(const MovingPair<U1, U2>& o) const
				{
					if (first < o.first) return true;
					if (o.first < first) return false;
					return second < o.second;
				}

				friend void swap(MovingPair& a, MovingPair& b)
				{
					using std::swap;
					swap(a.first, b.first);
					swap(a.second, b.second);
				}
			};

			template<class Iter>
			class RefIdxIterator : public Iter
			{
				using FirstRefTy = typename Iter::value_type::first_type;
				using SecondRefTy = typename Iter::value_type::second_type;
				using FirstValueTy = typename std::remove_reference<FirstRefTy>::type;
				using SecondValueTy = typename std::remove_reference<SecondRefTy>::type;
			public:

				using value_type = MovingPair<FirstValueTy, SecondValueTy>;
				using reference = MovingPair<FirstRefTy, SecondRefTy>&;
				using pointer = MovingPair<FirstRefTy, SecondRefTy>*;
				using difference_type = typename Iter::difference_type;

				RefIdxIterator() {}

				RefIdxIterator(Iter _p) : Iter{ _p }
				{
				}

				RefIdxIterator operator+(difference_type n) const
				{
					return { static_cast<const Iter&>(*this) + n };
				}

				RefIdxIterator operator-(difference_type n) const
				{
					return { static_cast<const Iter&>(*this) - n };
				}

				difference_type operator-(const RefIdxIterator& o) const
				{
					return static_cast<const Iter&>(*this) - static_cast<const Iter&>(o);
				}

				const reference operator*() const
				{
					return Iter::operator*();
				}

				reference operator*()
				{
					return Iter::operator*();
				}
			};

			template<class Ty>
			RefIdxIterator<typename std::remove_reference<Ty>::type> makeRefIdxIterator(Ty it)
			{
				return { it };
			}

			struct Less
			{
				template<class A, class B>
				bool operator()(A&& a, B&& b)
				{
					return a < b;
				}
			};
		}

		template<class It1, class It2, class Cmp = detail::Less>
		void zippedSort(It1 first1, It1 last1, It2 first2, Cmp cmp = {})
		{
			std::vector<detail::MovingPair<typename It1::reference, typename It2::reference>> sorter;
			for (; first1 != last1; ++first1, ++first2)
			{
				sorter.emplace_back(*first1, *first2);
			}

			std::sort(detail::makeRefIdxIterator(sorter.begin()), detail::makeRefIdxIterator(sorter.end()), cmp);
		}

		template<class InIt, class OutIt, class IdxTy = size_t, class Cmp = detail::Less>
		void sortWriteIdx(InIt first, InIt last, OutIt dest, IdxTy startIdx = 0, Cmp cmp = {})
		{
			std::vector<detail::MovingPair<typename InIt::reference, typename OutIt::reference>> sorter;
			for (IdxTy i = startIdx; first != last; ++first, ++dest, ++i)
			{
				*dest = i;
				sorter.emplace_back(*first, *dest);
			}

			std::sort(detail::makeRefIdxIterator(sorter.begin()), detail::makeRefIdxIterator(sorter.end()), cmp);
		}

		template<class InIt, class OutIt, class IdxTy = size_t, class Cmp = detail::Less>
		void sortWriteInvIdx(InIt first, InIt last, OutIt dest, IdxTy startIdx = 0, Cmp cmp = {})
		{
			std::vector<detail::MovingPair<typename InIt::reference, IdxTy>> sorter;
			for (IdxTy i = startIdx; first != last; ++first, ++i)
			{
				sorter.emplace_back(*first, i);
			}

			std::sort(detail::makeRefIdxIterator(sorter.begin()), detail::makeRefIdxIterator(sorter.end()), cmp);
			for (size_t i = 0; i < sorter.size(); ++i)
			{
				dest[sorter[i].second - startIdx] = i + startIdx;
			}
		}
	}
}
