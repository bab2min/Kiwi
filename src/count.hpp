#pragma once

#include <vector>
#include <map>
#include <unordered_set>
#include <unordered_map>

#include <kiwi/Trie.hpp>
#include <kiwi/ThreadPool.h>

#ifdef KIWI_USE_BTREE

#ifdef _WIN32
using ssize_t = ptrdiff_t;
#else
#include <sys/types.h>
#endif

#include <btree/map.h>
#else
#endif

namespace kiwi
{
	namespace utils
	{
#ifdef KIWI_USE_BTREE
		template<typename K, typename V> using map = btree::map<K, V>;
#else
		template<typename K, typename V> using map = std::map<K, V>;
#endif
		using Vid = uint16_t;
		using CTrieNode = TrieNodeEx<Vid, size_t, ConstAccess<map<Vid, int32_t>>>;

		static constexpr Vid non_vocab_id = (Vid)-1;

		template <typename _Iterator>
		class StrideIter : public _Iterator
		{
			size_t stride;
			const _Iterator end;
		public:
			StrideIter(const _Iterator& iter, size_t _stride = 1, const _Iterator& _end = {})
				: _Iterator{ iter }, stride{ _stride }, end{ _end }
			{
			}

			StrideIter(const StrideIter&) = default;
			StrideIter(StrideIter&&) = default;

			StrideIter& operator++()
			{
				for (size_t i = 0; i < stride && *this != end; ++i)
				{
					_Iterator::operator++();
				}
				return *this;
			}

			StrideIter& operator--()
			{
				for (size_t i = 0; i < stride && *this != end; ++i)
				{
					_Iterator::operator--();
				}
				return *this;
			}
		};

		template <typename _Iterator>
		StrideIter<_Iterator> makeStrideIter(const _Iterator& iter, size_t stride, const _Iterator& end = {})
		{
			return { iter, stride, end };
		}

		namespace detail
		{
			struct vvhash
			{
				size_t operator()(const std::pair<Vid, Vid>& k) const
				{
					return std::hash<Vid>{}(k.first) ^ std::hash<Vid>{}(k.second);
				}
			};
		}

		template<typename _DocIter>
		void countUnigrams(std::vector<size_t>& unigramCf, std::vector<size_t>& unigramDf,
			_DocIter docBegin, _DocIter docEnd
		)
		{
			for (auto docIt = docBegin; docIt != docEnd; ++docIt)
			{
				auto doc = *docIt;
				if (!doc.size()) continue;
				std::unordered_set<Vid> uniqs;
				for (size_t i = 0; i < doc.size(); ++i)
				{
					if (doc[i] == non_vocab_id) continue;
					if (unigramCf.size() <= doc[i])
					{
						unigramCf.resize(doc[i] + 1);
						unigramDf.resize(doc[i] + 1);
					}
					unigramCf[doc[i]]++;
					uniqs.emplace(doc[i]);
				}

				for (auto w : uniqs) unigramDf[w]++;
			}
		}

		template<typename _DocIter, typename _Freqs>
		void countBigrams(map<std::pair<Vid, Vid>, size_t>& bigramCf,
			map<std::pair<Vid, Vid>, size_t>& bigramDf,
			_DocIter docBegin, _DocIter docEnd,
			_Freqs&& vocabFreqs, _Freqs&& vocabDf,
			size_t candMinCnt, size_t candMinDf,
			const std::vector<Vid>* historyTransformer = nullptr
		)
		{
			for (auto docIt = docBegin; docIt != docEnd; ++docIt)
			{
				std::unordered_set<std::pair<Vid, Vid>, detail::vvhash> uniqBigram;
				auto doc = *docIt;
				if (!doc.size()) continue;
				Vid prevWord = doc[0];
				if (historyTransformer) prevWord = (*historyTransformer)[prevWord];
				for (size_t j = 1; j < doc.size(); ++j)
				{
					Vid curWord = doc[j];
					if (curWord != non_vocab_id && vocabFreqs[curWord] >= candMinCnt && vocabDf[curWord] >= candMinDf)
					{
						if (historyTransformer || prevWord != non_vocab_id && vocabFreqs[prevWord] >= candMinCnt && vocabDf[prevWord] >= candMinDf)
						{
							bigramCf[std::make_pair(prevWord, curWord)]++;
							uniqBigram.emplace(prevWord, curWord);
						}
						if (historyTransformer)
						{
							bigramCf[std::make_pair(prevWord, (*historyTransformer)[curWord])]++;
							uniqBigram.emplace(prevWord, (*historyTransformer)[curWord]);
						}
					}
					if (historyTransformer) prevWord = (*historyTransformer)[curWord];
					else prevWord = curWord;
				}

				for (auto& p : uniqBigram) bigramDf[p]++;
			}
		}

		template<bool _reverse, typename _DocIter, typename _Freqs, typename _BigramPairs>
		void countNgrams(ContinuousTrie<CTrieNode>& dest,
			_DocIter docBegin, _DocIter docEnd,
			_Freqs&& vocabFreqs, _Freqs&& vocabDf, _BigramPairs&& validPairs,
			size_t candMinCnt, size_t candMinDf, size_t maxNgrams,
			const std::vector<Vid>* historyTransformer = nullptr
		)
		{
			if (dest.empty())
			{
				dest = ContinuousTrie<CTrieNode>{ 1, 1024 };
			}
			const auto& allocNode = [&]() { return dest.newNode(); };

			for (auto docIt = docBegin; docIt != docEnd; ++docIt)
			{
				auto doc = *docIt;
				if (!doc.size()) continue;
				dest.reserveMore(doc.size() * maxNgrams * 2);
				
				Vid prevWord = _reverse ? *doc.rbegin() : *doc.begin();
				size_t labelLen = 0;
				auto node = &dest[0];
				if (prevWord != non_vocab_id && vocabFreqs[prevWord] >= candMinCnt && vocabDf[prevWord] >= candMinDf)
				{
					node = dest[0].makeNext(prevWord, allocNode);
					node->val++;
					labelLen = 1;
				}

				if (historyTransformer)
				{
					prevWord = (*historyTransformer)[prevWord];
					node = dest[0].makeNext(prevWord, allocNode);
					node->val++;
					labelLen = 1;
				}

				const auto func = [&](Vid curWord)
				{
					if (curWord != non_vocab_id && (vocabFreqs[curWord] < candMinCnt || vocabDf[curWord] < candMinDf))
					{
						node = &dest[0];
						labelLen = 0;
					}
					else
					{
						if (labelLen >= maxNgrams)
						{
							node = node->getFail();
							labelLen--;
						}

						if (validPairs.count(_reverse ? std::make_pair(curWord, prevWord) : std::make_pair(prevWord, curWord)))
						{
							auto nnode = node->makeNext(curWord, allocNode);
							node = nnode;
							do
							{
								nnode->val++;
							} while ((nnode = nnode->getFail()));
							labelLen++;

							if (historyTransformer)
							{
								curWord = (*historyTransformer)[curWord];
								nnode = node->getParent()->makeNext(curWord, allocNode);
								node = nnode;
								do
								{
									nnode->val++;
								} while ((nnode = nnode->getFail()));
							}
						}
						else
						{
							node = dest[0].makeNext(curWord, allocNode);
							node->val++;
							labelLen = 1;

							if (historyTransformer)
							{
								curWord = (*historyTransformer)[curWord];
								node = dest[0].makeNext(curWord, allocNode);
								node->val++;
								labelLen = 1;
							}
						}
					}
					prevWord = curWord;
				};

				if (_reverse) std::for_each(doc.rbegin() + 1, doc.rend(), func);
				else std::for_each(doc.begin() + 1, doc.end(), func);
			}
		}

		inline void mergeNgramCounts(ContinuousTrie<CTrieNode>& dest, ContinuousTrie<CTrieNode>&& src)
		{
			if (src.empty()) return;
			if (dest.empty()) dest = ContinuousTrie<CTrieNode>{ 1 };

			std::vector<Vid> rkeys;
			src.traverseWithKeys([&](const CTrieNode* node, const std::vector<Vid>& rkeys)
			{
				dest.build(rkeys.begin(), rkeys.end(), 0)->val += node->val;
			}, rkeys);
		}

		inline float branchingEntropy(const CTrieNode* node, size_t minCnt)
		{
			float entropy = 0;
			size_t rest = node->val;
			for (auto n : *node)
			{
				float p = n.second->val / (float)node->val;
				entropy -= p * std::log(p);
				rest -= n.second->val;
			}
			if (rest > 0)
			{
				float p = rest / (float)node->val;
				entropy -= p * std::log(std::min(std::max(minCnt, (size_t)1), (size_t)rest) / (float)node->val);
			}
			return entropy;
		}

		template<typename _LocalData, typename _ReduceFn>
		_LocalData parallelReduce(std::vector<_LocalData>&& data, _ReduceFn&& fn, ThreadPool* pool = nullptr)
		{
			if (pool)
			{
				for (size_t s = data.size(); s > 1; s = (s + 1) / 2)
				{
					std::vector<std::future<void>> futures;
					size_t h = (s + 1) / 2;
					for (size_t i = h; i < s; ++i)
					{
						futures.emplace_back(pool->enqueue([&, i, h](size_t)
						{
							_LocalData d = std::move(data[i]);
							fn(data[i - h], std::move(d));
						}));
					}
					for (auto& f : futures) f.get();
				}
			}
			else
			{
				for (size_t i = 1; i < data.size(); ++i)
				{
					_LocalData d = std::move(data[i]);
					fn(data[0], std::move(d));
				}
			}
			return std::move(data[0]);
		}

		template<typename _DocIter>
		ContinuousTrie<CTrieNode> count(_DocIter docBegin, _DocIter docEnd,
			size_t minCf, size_t minDf, size_t maxNgrams,
			ThreadPool* pool = nullptr, std::vector<std::pair<Vid, Vid>>* bigramList = nullptr,
			const std::vector<Vid>* historyTransformer = nullptr
		)
		{
			// counting unigrams & bigrams
			std::vector<size_t> unigramCf, unigramDf;
			map<std::pair<Vid, Vid>, size_t> bigramCf, bigramDf;

			if (pool && pool->size() > 1)
			{
				using LocalCfDf = std::pair<
					decltype(unigramCf),
					decltype(unigramDf)
				>;
				std::vector<LocalCfDf> localdata(pool->size());
				std::vector<std::future<void>> futures;
				const size_t stride = pool->size() * 8;
				auto docIt = docBegin;
				for (size_t i = 0; i < stride && docIt != docEnd; ++i, ++docIt)
				{
					futures.emplace_back(pool->enqueue([&, docIt, stride](size_t tid)
					{
						countUnigrams(localdata[tid].first, localdata[tid].second,
							makeStrideIter(docIt, stride, docEnd),
							makeStrideIter(docEnd, stride, docEnd)
						);
					}));
				}

				for (auto& f : futures) f.get();

				auto r = parallelReduce(std::move(localdata), [](LocalCfDf& dest, LocalCfDf&& src)
				{
					if (dest.first.size() < src.first.size())
					{
						dest.first.resize(src.first.size());
						dest.second.resize(src.first.size());
					}

					for (size_t i = 0; i < src.first.size(); ++i) dest.first[i] += src.first[i];
					for (size_t i = 0; i < src.second.size(); ++i) dest.second[i] += src.second[i];
				}, pool);

				unigramCf = std::move(r.first);
				unigramDf = std::move(r.second);
			}
			else
			{
				countUnigrams(unigramCf, unigramDf, docBegin, docEnd);
			}

			if (pool && pool->size() > 1)
			{
				using LocalCfDf = std::pair<
					decltype(bigramCf),
					decltype(bigramDf)
				>;
				std::vector<LocalCfDf> localdata(pool->size());
				std::vector<std::future<void>> futures;
				const size_t stride = pool->size() * 8;
				auto docIt = docBegin;
				for (size_t i = 0; i < stride && docIt != docEnd; ++i, ++docIt)
				{
					futures.emplace_back(pool->enqueue([&, docIt, stride](size_t tid)
					{
						countBigrams(localdata[tid].first, localdata[tid].second,
							makeStrideIter(docIt, stride, docEnd),
							makeStrideIter(docEnd, stride, docEnd),
							unigramCf, unigramDf,
							minCf, minDf,
							historyTransformer
						);
					}));
				}

				for (auto& f : futures) f.get();

				auto r = parallelReduce(std::move(localdata), [](LocalCfDf& dest, LocalCfDf&& src)
				{
					for (auto& p : src.first) dest.first[p.first] += p.second;
					for (auto& p : src.second) dest.second[p.first] += p.second;
				}, pool);

				bigramCf = std::move(r.first);
				bigramDf = std::move(r.second);
			}
			else
			{
				countBigrams(bigramCf, bigramDf, docBegin, docEnd, unigramCf, unigramDf, minCf, minDf, historyTransformer);
			}

			if (bigramList)
			{
				for (auto& p : bigramCf)
				{
					bigramList->emplace_back(p.first);
				}
			}

			ContinuousTrie<CTrieNode> trieNodes{ 1 };
			if (maxNgrams <= 2)
			{
				trieNodes.reserveMore(unigramCf.size() + bigramCf.size() * (historyTransformer ? 2 : 1) + 1);
				const auto& allocNode = [&]() { return trieNodes.newNode(); };

				for (size_t i = 0; i < unigramCf.size(); ++i)
				{
					if (unigramCf[i] && unigramCf[i] >= minCf && unigramDf[i] >= minDf)
					{
						trieNodes[0].makeNext(i, allocNode)->val = unigramCf[i];
					}
				}

				for (auto& p : bigramCf)
				{
					trieNodes[0].makeNext(p.first.first, allocNode)->val += p.second;
					trieNodes[0].getNext(p.first.first)->makeNext(p.first.second, allocNode)->val = p.second;
				}
			}
			// counting ngrams
			else
			{
				std::unordered_set<std::pair<Vid, Vid>, detail::vvhash> validPairs;
				for (auto& p : bigramCf)
				{
					if (p.second >= minCf && bigramDf[p.first] >= minDf) validPairs.emplace(p.first);
				}

				if (pool && pool->size() > 1)
				{
					using LocalFw = ContinuousTrie<CTrieNode>;
					std::vector<LocalFw> localdata(pool->size());
					std::vector<std::future<void>> futures;
					const size_t stride = pool->size() * 8;
					auto docIt = docBegin;
					for (size_t i = 0; i < stride && docIt != docEnd; ++i, ++docIt)
					{
						futures.emplace_back(pool->enqueue([&, docIt, stride](size_t tid)
						{
							countNgrams<false>(localdata[tid],
								makeStrideIter(docIt, stride, docEnd),
								makeStrideIter(docEnd, stride, docEnd),
								unigramCf, unigramDf, validPairs, minCf, minDf, maxNgrams,
								historyTransformer
							);
						}));
					}

					for (auto& f : futures) f.get();

					auto r = parallelReduce(std::move(localdata), [&](LocalFw& dest, LocalFw&& src)
					{
						mergeNgramCounts(dest, std::move(src));
					}, pool);

					trieNodes = std::move(r);
				}
				else
				{
					countNgrams<false>(trieNodes,
						docBegin, docEnd,
						unigramCf, unigramDf, validPairs, minCf, minDf, maxNgrams,
						historyTransformer
					);
				}
			}
			return trieNodes;
		}

	}
}
