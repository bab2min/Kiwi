#include <kiwi/Dataset.h>
#include <kiwi/SubstringExtractor.h>
#include "FrozenTrie.hpp"
#include "RaggedVector.hpp"

using namespace kiwi;

HSDataset::HSDataset(size_t _batchSize, 
	size_t _causalContextSize, 
	size_t _windowSize, 
	bool _exclusiveWindow, 
	size_t _workers, 
	HSDatasetOption option)
	: workers{ _workers ? make_unique<utils::ThreadPool>(_workers) : nullptr },
	dropout{ {1 - option.dropoutProb, option.dropoutProb / 3, option.dropoutProb / 3, option.dropoutProb / 6, option.dropoutProb / 6} },
	dropoutProbOnHistory{ (float)option.dropoutProbOnHistory },
	nounAugmentor{ {
			1 - option.nounAugmentingProb,
			option.nounAugmentingProb / 12,
			option.nounAugmentingProb / 12,
			option.nounAugmentingProb / 12,
			option.nounAugmentingProb / 4,
			option.nounAugmentingProb / 4,
			option.nounAugmentingProb / 4} },
	emojiAugmentor{ {
			1 - option.emojiAugmentingProb,
			option.emojiAugmentingProb / 4,
			option.emojiAugmentingProb / 4,
			option.emojiAugmentingProb / 4,
			option.emojiAugmentingProb / 4} },
	sbAugmentor{ {
			1 - option.sbAugmentingProb,
			option.sbAugmentingProb / 4,
			option.sbAugmentingProb / 4,
			option.sbAugmentingProb / 4,
			option.sbAugmentingProb / 4} },
	locals( _workers ? workers->size() : 1),
	batchSize{ _batchSize },
	causalContextSize{ _causalContextSize },
	windowSize{ _windowSize },
	exclusiveWindow{ _exclusiveWindow },
	generateUnlikelihoods{ option.generateUnlikelihoods }
{
}

HSDataset::~HSDataset()
{
}

HSDataset::HSDataset(HSDataset&& o) /*noexcept*/ = default;

HSDataset& HSDataset::operator=(HSDataset&& o) /*noexcept*/ = default;

constexpr int32_t HSDataset::nonVocab;

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
		l.restLmLProbsData.clear();
		l.restLmLProbsCntData.clear();
		l.rng.seed(rng());
	}
}

size_t HSDataset::numValidTokensInSent(size_t sentId) const
{
	size_t c = 0;
	for (auto t : sents.get()[sentId])
	{
		if (oovDict && t < 0)
		{
			POSTag tag = (*oovDict)[-t - 1].second;
			t = getDefaultMorphemeId(clearIrregular(tag));
		}

		if (tokenToVocab[t] == nonVocab) continue;
		++c;
	}
	return c;
}

bool HSDataset::tokenizeUnlikely(Vector<std::pair<int32_t, int32_t>>& out, int32_t prefix, int32_t target, int32_t suffix, std::mt19937_64& rng) const
{
	auto form = (oovDict && target < 0) ? (*oovDict)[-target - 1].first : joinHangul((*forms)[(*morphemes)[target].kform].form);
	
	if (oovDict && prefix < 0) prefix = getDefaultMorphemeId((*oovDict)[-prefix - 1].second);
	if (oovDict && suffix < 0) suffix = getDefaultMorphemeId((*oovDict)[-suffix - 1].second);
	auto prefixForm = joinHangul((*forms)[(*morphemes)[prefix].kform].form);
	auto suffixForm = joinHangul((*forms)[(*morphemes)[suffix].kform].form);
	if (form.size() < 2) return false;
	auto blocklist = kiwiInst->findMorphemes(form);
	std::unordered_set<const Morpheme*> blockset(blocklist.begin(), blocklist.end());
	
	thread_local std::vector<PretokenizedSpan> pretokenized;
	pretokenized.clear();
	pretokenized.emplace_back(0, 1, std::vector<BasicToken>{ BasicToken(prefixForm, -1, -1, (*morphemes)[prefix].tag) });
	pretokenized.emplace_back(form.size() + 1, form.size() + 2, std::vector<BasicToken>{ BasicToken(suffixForm, -1, -1, (*morphemes)[suffix].tag) });

	form.insert(form.begin(), ' ');
	form.push_back(' ');
	auto res = kiwiInst->analyze(form, 8, AnalyzeOption{ Match::allWithNormalizing, &blockset, false, Dialect::all, }, pretokenized);
	thread_local Vector<size_t> validResIdx;
	validResIdx.clear();
	for (size_t i = 0; i < res.size(); ++i)
	{
		auto& tokens = res[i].first;
		if (tokens.size() <= 3) continue;
		if (std::all_of(tokens.begin() + 1, tokens.end() - 1, [&](const TokenInfo& t) 
			{ 
				return t.morph && !t.morph->getForm().empty() /*&& t.morph->lmMorphemeId != getDefaultMorphemeId(t.morph->tag)*/;
			}))
		{
			validResIdx.emplace_back(i);
		}
	}
	if (validResIdx.empty()) return false;
	const float r = std::generate_canonical<float, 32>(rng);
	auto& tokens = res[validResIdx[(size_t)(r * (float)validResIdx.size())]].first;
	for (size_t i = 1; i < tokens.size() - 1; ++i)
	{
		out.emplace_back(tokens[i].morph->lmMorphemeId, i > 1 ? tokens[i].morph->lmMorphemeId : 0);
	}
	return true;
}

inline int32_t getInput(int32_t t, const Vector<std::pair<std::u16string, POSTag>>* oovDict)
{
	if (oovDict && t < 0)
	{
		POSTag tag = (*oovDict)[-t - 1].second;
		return getDefaultMorphemeId(clearIrregular(tag));
	}
	return t;
}

inline int32_t getOutput(int32_t t, const Vector<std::pair<std::u16string, POSTag>>* oovDict)
{
	return getInput(t, oovDict);
}

inline int32_t getInput(const std::pair<int32_t, int32_t>& t, const Vector<std::pair<std::u16string, POSTag>>* oovDict)
{
	return getInput(t.first, oovDict);
}

inline int32_t getOutput(const std::pair<int32_t, int32_t>& t, const Vector<std::pair<std::u16string, POSTag>>* oovDict)
{
	return getOutput(t.second, oovDict);
}

template<class Token>
void HSDataset::prepareInOutData(Deque<int32_t>& inData, Deque<int32_t>& outData, const Vector<Token>& tokens, std::mt19937_64& rng) const
{
	thread_local Deque<int32_t> history;
	thread_local Vector<uint32_t> contextualTokens;
	if (windowSize)
	{
		history.clear();
		history.resize(windowSize, -1);
		if (windowTokenValidness[getInput(tokens[0], oovDict.get())])
		{
			history.back() = tokenToVocab[getInput(tokens[0], oovDict.get())];
		}
	}

	if (causalContextSize && contextualMapper.size())
	{
		auto* node = contextualMapper.root();
		contextualTokens.clear();
		contextualTokens.reserve(tokens.size());
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			const int32_t v = tokenToVocab[getInput(tokens[i], oovDict.get())];
			auto* next = node->template nextOpt<ArchType::balanced>(contextualMapper, v);
			while (!next)
			{
				node = node->fail();
				if (!node) break;
				next = node->template nextOpt<ArchType::balanced>(contextualMapper, v);
			}
			if (next)
			{
				auto val = next->val(contextualMapper);
				if (contextualMapper.hasMatch(val))
				{
					contextualTokens.emplace_back(val - 1);
				}
				else if (contextualMapper.hasSubmatch(val))
				{
					auto sub = next->fail();
					for (; sub; sub = sub->fail())
					{
						val = sub->val(contextualMapper);
						if (contextualMapper.hasMatch(val))
						{
							break;
						}
					}
					if (sub) contextualTokens.emplace_back(val - 1);
					else contextualTokens.emplace_back(nonVocab);
				}
				node = next;
			}
			else
			{
				contextualTokens.emplace_back(nonVocab);
				node = contextualMapper.root();
			}
		}
	}

	int32_t lastV = nonVocab;
	for (size_t i = 1; i < tokens.size(); ++i)
	{
		const int32_t v = tokenToVocab[getInput(tokens[i], oovDict.get())];
		if (v == nonVocab)
		{
			continue;
		}
		const int32_t outV = getOutput(tokens[i], oovDict.get()) == 0 ? nonVocab : tokenToVocab[getOutput(tokens[i], oovDict.get())];

		if (causalContextSize)
		{
			for (size_t j = 0; j < causalContextSize; ++j)
			{
				if (i + j < causalContextSize)
				{
					if (outV != nonVocab) inData.emplace_back(nonVocab);
				}
				else if (contextualMapper.size())
				{
					if (outV != nonVocab) inData.emplace_back(contextualTokens[i + j - causalContextSize]);
				}
				else
				{
					auto t = getInput(tokens[i + j - causalContextSize], oovDict.get());
					if (dropoutProbOnHistory > 0 && std::generate_canonical<float, 32>(rng) < dropoutProbOnHistory)
					{
						t = getDefaultMorphemeId((*morphemes)[t].tag);
					}
					if (outV != nonVocab) inData.emplace_back(tokenToVocab[t]);
				}
			}
		}
		if (windowSize)
		{
			if (windowTokenValidness[v])
			{
				if (outV != nonVocab) std::copy(history.begin(), history.end(), std::back_inserter(inData));
				if (exclusiveWindow)
				{
					if (lastV != nonVocab)
					{
						history.pop_front();
						history.push_back(lastV);
					}
					lastV = v;
				}
				else
				{
					history.pop_front();
					history.push_back(v);
				}
			}
			else
			{
				if (outV != nonVocab) inData.resize(inData.size() + windowSize, -1);
				if (exclusiveWindow)
				{
					if (lastV != nonVocab)
					{
						history.pop_front();
						history.push_back(lastV);
					}
					lastV = nonVocab;
				}
			}
		}

		if (outV != nonVocab) outData.emplace_back(v);
	}
}

void HSDataset::fillSbTokenIds()
{
	if (!sbTokenIds.empty()) return;

	for (auto& m : *morphemes)
	{
		if (m.tag != POSTag::sb || m.senseId == 0) continue;
		if (m.senseId > sbTokenIds.size())
		{
			sbTokenIds.resize(m.senseId, 0);
		}
		sbTokenIds[m.senseId - 1] = m.lmMorphemeId;
	}
}

template<class InTy, class OutTy, class LmTy, class NgramTy, class UlInTy, class UlOutTy>
size_t HSDataset::_next(InTy in, OutTy out, LmTy lmLProbs, NgramTy outNgramNode, float& restLmOut, uint32_t& restLmCntOut, 
	UlInTy unlikelihoodIn, UlOutTy unlikelihoodOut, size_t* unlikelihoodSize)
{
	const auto& prepareNext = [&](size_t, size_t localId, size_t sentFirst, size_t sentLast)
	{
		auto knlm = std::dynamic_pointer_cast<lm::KnLangModelBase>(langModel);
		auto& local = locals[localId];
		auto& tokens = local.tokenBuf;
		const auto& morphs = *morphemes;
		tokens.reserve(sents.get()[shuffledIdx[sentFirst]].size());
		for (size_t s = sentFirst; s < sentLast; ++s)
		{
			auto sent = sents.get()[shuffledIdx[s]];
			tokens.clear();
			tokens.emplace_back(sent[0]);
			for (auto p = sent.begin() + 1; p != sent.end() - 1; ++p)
			{
				int32_t t = *p;
				int32_t tWithOOV = *p;
				if (oovDict && t < 0)
				{
					t = getDefaultMorphemeId((*oovDict)[-t - 1].second);
				}
				int32_t t1 = *(p + 1);
				if (oovDict && t1 < 0)
				{
					t1 = getDefaultMorphemeId((*oovDict)[-t1 - 1].second);
				}
				const auto nounAugment = (morphs[t].tag == POSTag::nnp && !isSpecialClass(morphs[t1].tag)) ? nounAugmentor(local.rng) : 0;
				const auto emojiAugment = 
					(morphs[t].tag == POSTag::nnp && isJClass(morphs[t1].tag)) ? emojiAugmentor(local.rng) :
					((morphs[t].tag == POSTag::ef && morphs[t1].tag == POSTag::sf) ? emojiAugmentor(local.rng) + 5 : 0);
				const bool isBOS = (tokens.size() == 1 && tokens[0] == 0);
				const auto sbAugment =
					(((tokens.size() > 1 && (morphs[t].tag == POSTag::nng || morphs[t].tag == POSTag::nnp) && morphs[t1].tag == morphs[t].tag)
						|| isBOS
					 ) && (tokens.back() < 0 || morphs[tokens.back()].tag != POSTag::sb)) ? sbAugmentor(local.rng) : 0;
				size_t sbToken = 0;

				if (sbAugment)
				{
					sbToken = (size_t)(std::generate_canonical<float, 32>(local.rng) * (float)(sbTokenIds.size() - 1));
					tokens.emplace_back(sbTokenIds[sbToken]);
				}

				switch (nounAugment)
				{
				case 1: // circumfix with sso and ssc
					tokens.emplace_back(getDefaultMorphemeId(POSTag::sso));
					break;
				case 2:
					tokens.emplace_back(specialMorphIds[(size_t)Kiwi::SpecialMorph::singleQuoteOpen]);
					break;
				case 3:
					tokens.emplace_back(specialMorphIds[(size_t)Kiwi::SpecialMorph::doubleQuoteOpen]);
					break;
				case 4: // circumfix with sw
					tokens.emplace_back(getDefaultMorphemeId(POSTag::sw));
					break;
				case 5: // replace with w_hashtag
					tokens.emplace_back(getDefaultMorphemeId(POSTag::w_hashtag));
					break;
				case 6: // replace with sh
					tokens.emplace_back(getDefaultMorphemeId(POSTag::sh));
					break;
				}

				if (nounAugment < 5)
				{
					switch (dropout(local.rng))
					{
					case 0: // no dropout
						tokens.emplace_back(knlm ? t : tWithOOV);
						break;
					case 1: // replacement
						tokens.emplace_back(getDefaultMorphemeId(morphs[t].tag));
						break;
					case 2: // deletion
						break;
					case 3: // insertion
						tokens.emplace_back(getDefaultMorphemeId(morphs[t].tag));
						tokens.emplace_back(knlm ? t : tWithOOV);
						break;
					case 4: // insertion
						tokens.emplace_back(knlm ? t : tWithOOV);
						tokens.emplace_back(getDefaultMorphemeId(morphs[t].tag));
						break;
					}
				}

				switch (nounAugment)
				{
				case 1: // circumfix with sso and ssc
					tokens.emplace_back(getDefaultMorphemeId(POSTag::ssc));
					break;
				case 2:
					tokens.emplace_back(specialMorphIds[(size_t)Kiwi::SpecialMorph::singleQuoteClose]);
					break;
				case 3:
					tokens.emplace_back(specialMorphIds[(size_t)Kiwi::SpecialMorph::doubleQuoteClose]);
					break;
				case 4: // circumfix with sw
					tokens.emplace_back(getDefaultMorphemeId(POSTag::sw));
					break;
				}

				if (emojiAugment > 0 && emojiAugment < 5)
				{
					for (int i = 0; i < emojiAugment; ++i)
					{
						tokens.emplace_back(getDefaultMorphemeId(POSTag::w_emoji));
					}
				}
				else if (emojiAugment > 5)
				{
					for (int i = 5; i < emojiAugment; ++i)
					{
						tokens.emplace_back(getDefaultMorphemeId(POSTag::w_emoji));
					}
					++p; // skip the following punctuation
				}

				if (sbAugment && !isBOS)
				{
					switch (sbAugment)
					{
					case 1:
						tokens.emplace_back(getDefaultMorphemeId(POSTag::sp));
						break;
					case 2:
						tokens.emplace_back(getDefaultMorphemeId(POSTag::nnp));
						break;
					case 3:
						tokens.emplace_back(getDefaultMorphemeId(POSTag::nnp));
						tokens.emplace_back(getDefaultMorphemeId(POSTag::sp));
						break;
					}
					tokens.emplace_back(sbTokenIds[sbToken + 1]);
				}
			}
			tokens.emplace_back(sent[sent.size() - 1]);
			const size_t offset = local.outData.size();
			prepareInOutData(local.inData, local.outData, tokens, local.rng);

			local.lmLProbsBuf.resize(tokens.size());
			local.outNgramNodeBuf.resize(tokens.size());
			if (knlm)
			{
				knlm->evaluate(tokens.begin(), tokens.end(), local.lmLProbsBuf.begin(), local.outNgramNodeBuf.begin());
			}
			for (size_t i = 1; i < tokens.size(); ++i)
			{
				int32_t t = tokens[i];
				if (oovDict && t < 0)
				{
					t = getDefaultMorphemeId((*oovDict)[-t - 1].second);
				}
				int32_t v = tokenToVocab[t];
				if (v == nonVocab)
				{
					size_t r = (offset + i - 1) / batchSize;
					if (local.restLmLProbsData.size() <= r)
					{
						local.restLmLProbsData.resize(r + 1);
						local.restLmLProbsCntData.resize(r + 1);
					}
					local.restLmLProbsData[r] += local.lmLProbsBuf[i];
					local.restLmLProbsCntData[r] += 1;
					continue;
				}

				local.lmLProbsData.emplace_back(local.lmLProbsBuf[i]);
				local.outNgramNodeData.emplace_back(local.outNgramNodeBuf[i]);
			}

			size_t r = local.outData.size() / batchSize;
			if (local.restLmLProbsData.size() <= r)
			{
				local.restLmLProbsData.resize(r + 1);
				local.restLmLProbsCntData.resize(r + 1);
			}

			if (doesGenerateUnlikelihoods())
			{
				local.unlikelihoodBuf.clear();
				local.unlikelihoodBuf.emplace_back(tokens[0], 0);
				for (size_t i = 1; i < tokens.size() - 1; ++i)
				{
					if (oovDict && tokens[i] < 0)
					{
						if (!tokenizeUnlikely(local.unlikelihoodBuf, tokens[i - 1], tokens[i], tokens[i + 1], local.rng))
						{
							local.unlikelihoodBuf.emplace_back(tokens[i], 0);
						}
						continue;
					}

					auto& morph = (*morphemes)[tokens[i]];
					if (tokens[i] < generateUnlikelihoods
						|| !(morph.tag == POSTag::nng || morph.tag == POSTag::nnp)
						|| getDefaultMorphemeId(morph.tag) == tokens[i]
						|| !tokenizeUnlikely(local.unlikelihoodBuf, tokens[i - 1], tokens[i], tokens[i + 1], local.rng))
					{
						local.unlikelihoodBuf.emplace_back(tokens[i], 0);
					}
				}
				local.unlikelihoodBuf.emplace_back(tokens.back(), 0);

				prepareInOutData(local.unlikelihoodInData, local.unlikelihoodOutData, local.unlikelihoodBuf, local.rng);
			}
		}
		return localId;
	};

	fillSbTokenIds();

	size_t localId;
	if (workers)
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
				futures.emplace_back(workers->enqueue(prepareNext, passedWorkItems++ % workers->size(), passedSents, passedSents + sentCount));
				passedSents += sentCount;
			}
			else
			{
				futures.emplace_back(passedWorkItems++ % workers->size());
			}
		}

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
	}
	else
	{
		if (passedSents < numSents())
		{
			size_t sentCount = 0, tokenCount = locals[0].outData.size();
			while (tokenCount < batchSize && passedSents + sentCount < numSents())
			{
				tokenCount += numValidTokensInSent(shuffledIdx[passedSents + sentCount++]) - 1;
			}

			if (sentCount > 0)
			{
				prepareNext(0, 0, passedSents, passedSents + sentCount);
				passedSents += sentCount;
			}
		}
		localId = 0;

		if (locals[0].outData.empty()) return 0;
	}

	auto& l = locals[localId];

	const size_t rest = std::min(l.outData.size(), batchSize);
	const size_t unlikelihoodRest = std::min(l.unlikelihoodOutData.size(), batchSize);
	std::copy(l.inData.begin(), l.inData.begin() + rest * (causalContextSize + windowSize), in);
	std::copy(l.outData.begin(), l.outData.begin() + rest, out);
	std::copy(l.lmLProbsData.begin(), l.lmLProbsData.begin() + rest, lmLProbs);
	std::copy(l.outNgramNodeData.begin(), l.outNgramNodeData.begin() + rest, outNgramNode);
	restLmOut = l.restLmLProbsData.front();
	restLmCntOut = l.restLmLProbsCntData.front();
	if (doesGenerateUnlikelihoods() && unlikelihoodIn && unlikelihoodOut)
	{
		std::copy(l.unlikelihoodInData.begin(), l.unlikelihoodInData.begin() + unlikelihoodRest * (causalContextSize + windowSize), unlikelihoodIn);
		std::copy(l.unlikelihoodOutData.begin(), l.unlikelihoodOutData.begin() + unlikelihoodRest, unlikelihoodOut);
		if (unlikelihoodSize) *unlikelihoodSize = unlikelihoodRest;
	}

	l.inData.erase(l.inData.begin(), l.inData.begin() + rest * (causalContextSize + windowSize));
	l.outData.erase(l.outData.begin(), l.outData.begin() + rest);
	l.lmLProbsData.erase(l.lmLProbsData.begin(), l.lmLProbsData.begin() + rest);
	l.outNgramNodeData.erase(l.outNgramNodeData.begin(), l.outNgramNodeData.begin() + rest);
	l.restLmLProbsData.pop_front();
	l.restLmLProbsCntData.pop_front();
	if (doesGenerateUnlikelihoods() && unlikelihoodIn && unlikelihoodOut)
	{
		l.unlikelihoodInData.erase(l.unlikelihoodInData.begin(), l.unlikelihoodInData.begin() + unlikelihoodRest * (causalContextSize + windowSize));
		l.unlikelihoodOutData.erase(l.unlikelihoodOutData.begin(), l.unlikelihoodOutData.begin() + unlikelihoodRest);
	}
	return rest;
}

size_t HSDataset::next(int32_t* in, int32_t* out, float* lmLProbs, uint32_t* outNgramNode, float& restLmOut, uint32_t& restLmCntOut,
	int32_t* unlikelihoodIn, int32_t* unlikelihoodOut, size_t* unlikelihoodSize)
{
	return _next(in, out, lmLProbs, outNgramNode, restLmOut, restLmCntOut, unlikelihoodIn, unlikelihoodOut, unlikelihoodSize);
}

size_t HSDataset::next(int64_t* in, int64_t* out, float* lmLProbs, int64_t* outNgramNode, float& restLmOut, uint32_t& restLmCntOut,
	int64_t* unlikelihoodIn, int64_t* unlikelihoodOut, size_t* unlikelihoodSize)
{
	return _next(in, out, lmLProbs, outNgramNode, restLmOut, restLmCntOut, unlikelihoodIn, unlikelihoodOut, unlikelihoodSize);
}

size_t HSDataset::ngramNodeSize() const
{
	auto knlm = std::dynamic_pointer_cast<lm::KnLangModelBase>(langModel);
	return knlm ? knlm->nonLeafNodeSize() : 0;
}

const MorphemeRaw& HSDataset::vocabInfo(uint32_t vocab) const
{
	return (*morphemes)[vocabToToken[vocab]];
}

std::u16string HSDataset::vocabForm(uint32_t vocab) const
{
	return joinHangul((*forms)[(*morphemes)[vocabToToken[vocab]].kform].form);
}

size_t HSDataset::getKnlmVocabSize() const
{
	return knlmVocabSize;
}

std::vector<size_t> kiwi::HSDataset::estimVocabFrequency() const
{
	std::vector<size_t> ret(vocabSize()), augs(getDefaultMorphemeId(POSTag::max));
	for (auto t : sents.get().raw())
	{
		auto v = tokenToVocab[t];
		auto fv = tokenToVocab[getDefaultMorphemeId((*morphemes)[t].tag)];
		if (v == nonVocab) v = fv;
		if (fv == nonVocab) continue;
		ret[v]++;
		augs[fv]++;
	}

	double augProbs = dropout.param().probabilities().back();
	for (size_t i = 0; i < augs.size(); ++i)
	{
		ret[i] += (size_t)(augs[i] * augProbs);
	}
	return ret;
}

Range<Vector<int32_t>::const_iterator> HSDataset::getSent(size_t idx) const
{
	return sents.get()[idx];
}

void HSDataset::seed(size_t newSeed)
{
	rng.seed(newSeed);
}

std::vector<uint32_t> HSDataset::getAugmentedSent(size_t idx)
{
	std::vector<uint32_t> ret;
	auto sent = sents.get()[idx];
	ret.emplace_back(*sent.begin());
	for (auto p = sent.begin() + 1; p != sent.end() - 1; ++p)
	{
		auto t = *p;
		switch (dropout(rng))
		{
		case 0:
		case 1:
		case 2:
			ret.emplace_back(t);
			break;
		case 3: // insertion
			ret.emplace_back(getDefaultMorphemeId((*morphemes)[t].tag));
			ret.emplace_back(t);
			break;
		case 4: // insertion
			ret.emplace_back(t);
			ret.emplace_back(getDefaultMorphemeId((*morphemes)[t].tag));
			break;
		}
	}
	ret.emplace_back(*sent.rbegin());
	return ret;
}

std::vector<std::pair<std::vector<uint32_t>, size_t>> HSDataset::extractPrefixes(size_t minCnt, size_t maxLength, size_t numWorkers, bool exclusiveCnt) const
{
	using Pair = std::pair<std::vector<uint32_t>, size_t>;
	std::vector<Pair> ret;
	PrefixCounter counter{ maxLength, minCnt, numWorkers };
	for (auto sent : sents.get())
	{
		counter.addArray(&*sent.begin(), &*sent.end());
	}
	auto trie = counter.count();
	if (exclusiveCnt)
	{
		Vector<UnorderedMap<Vector<uint32_t>, size_t>> cnts_by_length(maxLength);
		trie.traverse([&](size_t cnt, const std::vector<uint32_t>& prefix)
		{
			if (cnt < minCnt) return;
			if (std::find_if(prefix.begin(), prefix.end(), [](uint32_t t) { return t < 2; }) != prefix.end())
			{
				return;
			}
			Vector<uint32_t> p(prefix.begin(), prefix.end());
			cnts_by_length[p.size() - 1].emplace(move(p), cnt);
		});

		Vector<uint32_t> suffix;
		suffix.reserve(maxLength);
		for (size_t i = 1; i < maxLength; ++i)
		{
			for (auto& p : cnts_by_length[i])
			{
				suffix.clear();
				suffix.insert(suffix.end(), p.first.begin() + 1, p.first.end());
				auto it = cnts_by_length[i - 1].find(suffix);
				if (it == cnts_by_length[i - 1].end() || it->second < p.second)
				{
					throw std::runtime_error("This should not happen");
				}
				it->second -= p.second;
			}
		}
		
		for (auto& cnts : cnts_by_length)
		{
			for (auto& p : cnts)
			{
				if (p.second < minCnt) continue;
				ret.emplace_back(std::vector<uint32_t>{ p.first.begin(), p.first.end() }, p.second);
			}
		}
	}
	else
	{
		trie.traverse([&](size_t cnt, const std::vector<uint32_t>& prefix)
		{
			if (cnt < minCnt) return;
			if (std::find_if(prefix.begin(), prefix.end(), [](uint32_t t) { return t < 2; }) != prefix.end())
			{
				return;
			}
			ret.emplace_back(prefix, cnt);
		});
	}

	std::sort(ret.begin(), ret.end(), [](const Pair& a, const Pair& b)
	{
		return a.second > b.second;
	});
	return ret;
}
