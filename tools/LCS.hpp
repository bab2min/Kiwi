#pragma once

#include <vector>
#include <type_traits>
#include <iterator>
#include <algorithm>

namespace lcs
{
	template<class Type> class matrix : public std::vector<Type>
	{
	public:
		size_t xDim;
		matrix(size_t x, size_t y) : std::vector<Type>(x * y), xDim(x)
		{

		}

		std::pair<size_t, size_t> getDimension() const
		{
			return std::make_pair(xDim, this->size() / xDim);
		}

		Type& at(size_t i, size_t j)
		{
			return this->operator [](i + j * xDim);
		}

		const Type& at(size_t i, size_t j) const
		{
			return this->operator [](i + j * xDim);
		}
	};

	template<class RandomIter, class EqualTo = std::equal_to<typename std::iterator_traits<RandomIter>::value_type>>
	matrix<size_t> computeLCSMatrix(RandomIter xBegin, RandomIter xEnd, RandomIter yBegin, RandomIter yEnd, EqualTo comparator = {})
	{
		size_t m = std::distance(xBegin, xEnd);
		size_t n = std::distance(yBegin, yEnd);
		matrix<size_t> c(m + 1, n + 1);
		for (size_t i = 1; xBegin != xEnd; ++i, ++xBegin)
		{
			auto  yIt = yBegin;
			for (size_t j = 1; yIt != yEnd; ++j, ++yIt)
			{
				if (comparator(*xBegin, *yIt)) c.at(i, j) = c.at(i - 1, j - 1) + 1;
				else c.at(i, j) = std::max(c.at(i, j - 1), c.at(i - 1, j));
			}
		}
		return c;
	}

	template<class RandomIter, class EqualTo = std::equal_to<typename std::iterator_traits<RandomIter>::value_type>>
	std::vector<typename std::iterator_traits<RandomIter>::value_type> _getLCS(const matrix<size_t>& c, RandomIter xBegin, RandomIter xEnd, RandomIter yBegin, RandomIter yEnd, EqualTo comparator = {})
	{
		auto && dim = c.getDimension();
		auto i = dim.first - 1;
		auto j = dim.second - 1;
		std::vector<typename std::iterator_traits<RandomIter>::value_type> lcs(std::min(dim.first, dim.second));
		size_t len = 0;
		while (i && j)
		{
			if (comparator(*(xEnd - 1), *(yEnd - 1)))
			{
				*(lcs.end() - len - 1) = *(xEnd - 1);
				++len;
				--i;
				--j;
				--xEnd;
				--yEnd;
			}
			else if (c.at(i, j - 1) > c.at(i - 1, j))
			{
				--j;
				--yEnd;
			}
			else
			{
				--i;
				--xEnd;
			}
		}
		return { lcs.end() - len, lcs.end() };
	}

	template<class RandomIter, class EqualTo = std::equal_to<typename std::iterator_traits<RandomIter>::value_type>>
	std::vector<std::pair<int, typename std::iterator_traits<RandomIter>::value_type>> _getDiff(const matrix<size_t>& c, RandomIter xBegin, RandomIter xEnd, RandomIter yBegin, RandomIter yEnd, EqualTo comparator = {})
	{
		auto && dim = c.getDimension();
		auto i = dim.first - 1;
		auto j = dim.second - 1;
		std::vector<std::pair<int, typename std::iterator_traits<RandomIter>::value_type>> diff(dim.first + dim.second);
		size_t len = 0;
		while (i || j)
		{
			if (i && j && comparator(*(xEnd - 1), *(yEnd - 1)))
			{
				++len;
				--i;
				--j;
				--xEnd;
				--yEnd;
				*(diff.end() - len) = std::make_pair(0, *xEnd);
			}
			else if (j && (!i || c.at(i, j - 1) >= c.at(i - 1, j)))
			{
				++len;
				--j;
				--yEnd;
				*(diff.end() - len) = std::make_pair(+1, *yEnd);
			}
			else if (i && (!j || c.at(i, j - 1) < c.at(i - 1, j)))
			{
				++len;
				--i;
				--xEnd;
				*(diff.end() - len) = std::make_pair(-1, *xEnd);
			}
		}
		return { diff.end() - len, diff.end() };
	}

	template<class RandomIter, class EqualTo = std::equal_to<typename std::iterator_traits<RandomIter>::value_type>>
	std::vector<typename std::iterator_traits<RandomIter>::value_type> getLCS(RandomIter xBegin, RandomIter xEnd, RandomIter yBegin, RandomIter yEnd, EqualTo comparator = {})
	{
		auto xDiffBegin = xBegin;
		auto yDiffBegin = yBegin;
		auto xDiffEnd = xEnd;
		auto yDiffEnd = yEnd;
		while (comparator(*xDiffBegin, *yDiffBegin))
		{
			++xDiffBegin;
			++yDiffBegin;
		}

		while (comparator(*(xDiffEnd - 1), *(yDiffEnd - 1)) && xDiffBegin != xDiffEnd && yDiffBegin != yDiffEnd)
		{
			--xDiffEnd;
			--yDiffEnd;
		}

		auto c = computeLCSMatrix(xDiffBegin, xDiffEnd, yDiffBegin, yDiffEnd, comparator);
		auto lcs = _getLCS(c, xDiffBegin, xDiffEnd, yDiffBegin, yDiffEnd, comparator);

		std::vector<typename std::iterator_traits<RandomIter>::value_type> before;
		for (; xBegin != xDiffBegin; ++xBegin) before.emplace_back(*xBegin);
		lcs.insert(lcs.begin(), before.begin(), before.end());
		for (; xDiffEnd != xEnd; ++xDiffEnd) lcs.emplace_back(*xDiffEnd);
		return lcs;
	}

	template<class RandomIter, class EqualTo = std::equal_to<typename std::iterator_traits<RandomIter>::value_type>>
	std::vector<std::pair<int, typename std::iterator_traits<RandomIter>::value_type>> getDiff(RandomIter xBegin, RandomIter xEnd, RandomIter yBegin, RandomIter yEnd, EqualTo comparator = {})
	{
		auto xDiffBegin = xBegin;
		auto yDiffBegin = yBegin;
		auto xDiffEnd = xEnd;
		auto yDiffEnd = yEnd;
		while (xDiffBegin != xDiffEnd && yDiffBegin != yDiffEnd  && comparator(*xDiffBegin, *yDiffBegin))
		{
			++xDiffBegin;
			++yDiffBegin;
		}

		while (xDiffBegin != xDiffEnd && yDiffBegin != yDiffEnd && comparator(*(xDiffEnd - 1), *(yDiffEnd - 1)))
		{
			--xDiffEnd;
			--yDiffEnd;
		}

		auto c = computeLCSMatrix(xDiffBegin, xDiffEnd, yDiffBegin, yDiffEnd, comparator);
		auto diff = _getDiff(c, xDiffBegin, xDiffEnd, yDiffBegin, yDiffEnd, comparator);

		std::vector<std::pair<int, typename std::iterator_traits<RandomIter>::value_type>> before;
		for (; xBegin != xDiffBegin; ++xBegin) before.emplace_back(0, *xBegin);
		diff.insert(diff.begin(), before.begin(), before.end());
		for (; xDiffEnd != xEnd; ++xDiffEnd) diff.emplace_back(0, *xDiffEnd);
		return diff;
	}
}
