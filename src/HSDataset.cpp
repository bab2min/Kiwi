#include <kiwi/HSDataset.h>
#include "RaggedVector.hpp"

using namespace kiwi;

HSDataset::HSDataset(size_t _batchSize, size_t _windowSize, size_t _workers, double _dropoutProb)
	: workers{ make_unique<utils::ThreadPool>(_workers) },
	dropout{ {1 - _dropoutProb * 3, _dropoutProb, _dropoutProb, _dropoutProb} }, 
	locals(workers->size()),
	batchSize{ _batchSize },
	windowSize{ _windowSize }
{
}

HSDataset::~HSDataset()
{
}

HSDataset::HSDataset(HSDataset&& o) /*noexcept*/ = default;

HSDataset& HSDataset::operator=(HSDataset&& o) /*noexcept*/ = default;

size_t HSDataset::numSents() const
{
	return sents.get().size();
}

size_t HSDataset::numTokens() const
{
	return totalTokens;
}

size_t HSDataset::numEstimBatches() const
{
	return (numTokens() + batchSize - 1) / batchSize;
}

void HSDataset::reset()
{
	while (!futures.empty())
	{
		futures.front().get();
		futures.pop_front();
	}

	if (shuffledIdx.size() < numSents())
	{
		size_t s = shuffledIdx.size();
		shuffledIdx.resize(numSents());
		std::iota(shuffledIdx.begin() + s, shuffledIdx.end(), s);
	}
	std::shuffle(shuffledIdx.begin(), shuffledIdx.end(), rng);
	passedSents = 0;
	passedWorkItems = 0;
	for (auto& l : locals)
	{
		l.inData.clear();
		l.outData.clear();
		l.lmLProbsData.clear();
		l.outNgramNodeData.clear();
	}
}

size_t HSDataset::numValidTokensInSent(size_t sentId) const
{
	size_t c = 0;
	for (auto t : sents.get()[sentId])
	{
		if (tokenToVocab[t] == nonVocab) continue;
		++c;
	}
	return c;
}

template<class InTy, class OutTy, class LmTy, class NgramTy>
size_t HSDataset::_next(InTy in, OutTy out, LmTy lmLProbs, NgramTy outNgramNode)
{
	while (passedSents < numSents() && futures.size() < workers->size())
	{
		size_t sentCount = 0, tokenCount = locals[passedWorkItems % workers->size()].outData.size();
		while (tokenCount < batchSize && passedSents + sentCount < numSents())
		{
			tokenCount += numValidTokensInSent(shuffledIdx[passedSents + sentCount++]) - 1;
		}
		
		if (sentCount > 0)
		{
			futures.emplace_back(workers->enqueue([&](size_t, size_t localId, size_t sentFirst, size_t sentLast)
			{
				auto& local = locals[localId];
				auto& tokens = local.tokenBuf;
				tokens.reserve(sents.get()[shuffledIdx[sentFirst]].size());
				for (size_t s = sentFirst; s < sentLast; ++s)
				{
					auto sent = sents.get()[shuffledIdx[s]];
					tokens.clear();
					tokens.emplace_back(sent[0]);
					for (auto p = sent.begin() + 1; p != sent.end() - 1; ++p)
					{
						auto t = *p;
						switch (dropout(local.rng))
						{
						case 0: // no dropout
							tokens.emplace_back(t);
							break;
						case 1: // replacement
							tokens.emplace_back(getDefaultMorphemeId((*morphemes)[t].tag));
							break;
						case 2: // deletion
							break;
						case 3: // insertion
							tokens.emplace_back(getDefaultMorphemeId((*morphemes)[t].tag));
							tokens.emplace_back(t);
							break;
						}
					}
					tokens.emplace_back(sent[sent.size() - 1]);

					local.lmLProbsBuf.resize(tokens.size());
					local.outNgramNodeBuf.resize(tokens.size());
					knlm->evaluate(tokens.begin(), tokens.end(), local.lmLProbsBuf.begin(), local.outNgramNodeBuf.begin());

					auto& history = local.historyBuf;
					history.clear();
					history.resize(windowSize, -1);
					history.back() = tokenToVocab[tokens[0]];
					for (size_t i = 1; i < tokens.size(); ++i)
					{
						int32_t v = tokenToVocab[tokens[i]];
						if (v == nonVocab) continue;
						std::copy(history.begin(), history.end(), std::back_inserter(local.inData));
						local.outData.emplace_back(v);
						local.lmLProbsData.emplace_back(local.lmLProbsBuf[i]);
						local.outNgramNodeData.emplace_back(local.outNgramNodeBuf[i]);

						history.pop_front();
						history.push_back(v);
					}
				}
				return localId;
			}, passedWorkItems++ % workers->size(), passedSents, passedSents + sentCount));
			passedSents += sentCount;
		}
		else
		{
			futures.emplace_back(passedWorkItems++ % workers->size());
		}
	}
	
	size_t localId;
	if (futures.empty())
	{
		for (localId = 0; localId < locals.size(); ++localId)
		{
			if (!locals[localId].outData.empty()) break;
		}

		if (localId >= locals.size())
		{
			return 0;
		}
	}
	else
	{
		localId = futures.front().get();
		futures.pop_front();
	}

	auto& l = locals[localId];

	size_t rest = std::min(l.outData.size(), batchSize);
	std::copy(l.inData.begin(), l.inData.begin() + rest * windowSize, in);
	std::copy(l.outData.begin(), l.outData.begin() + rest, out);
	std::copy(l.lmLProbsData.begin(), l.lmLProbsData.begin() + rest, lmLProbs);
	std::copy(l.outNgramNodeData.begin(), l.outNgramNodeData.begin() + rest, outNgramNode);

	l.inData.erase(l.inData.begin(), l.inData.begin() + rest * windowSize);
	l.outData.erase(l.outData.begin(), l.outData.begin() + rest);
	l.lmLProbsData.erase(l.lmLProbsData.begin(), l.lmLProbsData.begin() + rest);
	l.outNgramNodeData.erase(l.outNgramNodeData.begin(), l.outNgramNodeData.begin() + rest);
	return rest;
}

size_t HSDataset::next(int32_t* in, int32_t* out, float* lmLProbs, uint32_t* outNgramNode)
{
	return _next(in, out, lmLProbs, outNgramNode);
}

size_t HSDataset::next(int64_t* in, int64_t* out, float* lmLProbs, int64_t* outNgramNode)
{
	return _next(in, out, lmLProbs, outNgramNode);
}

size_t HSDataset::ngramNodeSize() const
{
	return knlm->nonLeafNodeSize();
}

const MorphemeRaw& HSDataset::vocabInfo(uint32_t vocab) const
{
	return (*morphemes)[vocabToToken[vocab]];
}

std::u16string HSDataset::vocabForm(uint32_t vocab) const
{
	return joinHangul((*forms)[(*morphemes)[vocabToToken[vocab]].kform].form);
}
