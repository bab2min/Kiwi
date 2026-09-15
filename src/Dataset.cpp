#include <kiwi/Dataset.h>
#include <kiwi/BpeTokenizer.h>
#include <kiwi/SubstringExtractor.h>
#include "FrozenTrie.hpp"
#include "RaggedVector.hpp"
#include "StrUtils.h"
#include "Joiner.hpp"

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
	ssAugmentor{ {
			1 - option.ssAugmentingProb,
			option.ssAugmentingProb / 3,
			option.ssAugmentingProb / 3,
			option.ssAugmentingProb / 3} },
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

HSDataset::~HSDataset() = default;

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
		auto& sents = this->sents.get();
		tokens.reserve(sents[shuffledIdx[sentFirst]].size());
		for (size_t s = sentFirst; s < sentLast; ++s)
		{
			auto sent = sents[shuffledIdx[s]];
			tokens.clear();
			tokens.emplace_back(sent[0]);
			auto ssAugment = sent.size() >= 5 ? ssAugmentor(local.rng) : 0;
			if (ssAugment && (
				sent[1] >= 0 && morphs[sent[1]].tag == POSTag::sso ||
				sent[sent.size() - 2] >= 0 && morphs[sent[sent.size() - 2]].tag == POSTag::ssc
			))
			{
				ssAugment = 0;
			}

			switch (ssAugment)
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
			}

			for (auto p = sent.begin() + 1; p < sent.end() - 1; ++p)
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
				auto nounAugment = (morphs[t].tag == POSTag::nnp && !isSpecialClass(morphs[t1].tag)) ? nounAugmentor(local.rng) : 0;
				if (ssAugment && ssAugment == nounAugment)
				{
					nounAugment = 0;
				}

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
			
			switch (ssAugment)
			{
			case 1:
				tokens.emplace_back(getDefaultMorphemeId(POSTag::ssc));
				break;
			case 2:
				tokens.emplace_back(specialMorphIds[(size_t)Kiwi::SpecialMorph::singleQuoteClose]);
				break;
			case 3:
				tokens.emplace_back(specialMorphIds[(size_t)Kiwi::SpecialMorph::doubleQuoteClose]);
				break;
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
		if (oovDict && t < 0) t = getDefaultMorphemeId((*oovDict)[-t - 1].second);
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

size_t ChrTokenizer::encodeOne(char32_t c) const
{
	if (isHangulSyllable(c))
	{
		int32_t i = (c - 0xAC00) / 28;
		return (int32_t)Token::hangulSyllableStart + i;
	}
	else if (isHangulCoda(c))
	{
		int32_t i = c - 0x11A8;
		return (int32_t)Token::hangulCodaStart + i;
	}
	else if (0x21 <= c && c < 0x7F)
	{
		return (int32_t)Token::asciiStart + (c - 0x21);
	}
	else
	{
		const POSTag type = identifySpecialChr(c);
		switch (type)
		{
		case POSTag::sf:
			return (int32_t)Token::sf;
		case POSTag::sp:
			return (int32_t)Token::sp;
		case POSTag::ss:
			return (int32_t)Token::ss;
		case POSTag::sso:
			return (int32_t)Token::sso;
		case POSTag::ssc:
			return (int32_t)Token::ssc;
		case POSTag::se:
			return (int32_t)Token::se;
		case POSTag::so:
			return (int32_t)Token::so;
		case POSTag::sh:
			return (int32_t)Token::sh;
		default:
			return (int32_t)Token::sw;
		}
	}
	return 0;
}

size_t ChrTokenizer::encode(std::string_view text, int32_t* outBuf, size_t bufSize) const
{
	size_t written = 0;
	const auto normalizedText = normalizeHangul(utf8To16(text));
	for (auto c : normalizedText)
	{
		if (written >= bufSize) break;

		outBuf[written++] = encodeOne(c);
	}
	return written;
}

std::u16string ChrTokenizer::decode(const int32_t* tokenBuf, size_t tokenCnt) const
{
	KString result;
	for (size_t i = 0; i < tokenCnt; ++i)
	{
		int32_t t = tokenBuf[i];
		if (Token::hangulSyllableStart <= (Token)t && (Token)t < Token::hangulCodaStart)
		{
			char16_t c = 0xAC00 + (uint16_t)(t - (int32_t)Token::hangulSyllableStart) * 28;
			result.push_back(c);
		}
		else if (Token::hangulCodaStart <= (Token)t && (Token)t < Token::asciiStart)
		{
			char16_t c = 0x11A8 + (uint16_t)(t - (int32_t)Token::hangulCodaStart);
			result.push_back(c);
		}
		else if (Token::asciiStart <= (Token)t && (Token)t < Token::max)
		{
			char16_t c = 0x21 + (uint16_t)(t - (int32_t)Token::asciiStart);
			result.push_back(c);
		}
		else
		{
			switch ((Token)t)
			{
			case Token::sf:
				result.push_back(u'.');
				break;
			case Token::sp:
				result.push_back(u',');
				break;
			case Token::ss:
				result.push_back(u'"');
				break;
			case Token::sso:
				result.push_back(u'(');
				break;
			case Token::ssc:
				result.push_back(u')');
				break;
			case Token::se:
				result.push_back(u'\u2026');
				break;
			case Token::so:
				result.push_back(u'\u223c');
				break;
			case Token::sh:
				result.push_back(u'漢');
				break;
			case Token::sw:
				result.push_back(u'※');
				break;
			default:
				break;
			}
		}
	}
	return joinHangul(result);
}

ChrDataset::ChrDataset(size_t _batchSize, size_t _causalContextSize, size_t _windowSize, float _prefixDropoutProb, bool _sampleWithoutWeights,
	const std::vector<std::pair<size_t, std::vector<uint32_t>>>& _contextualMapper
	)
	: batchSize(_batchSize), causalContextSize(_causalContextSize), windowSize(_windowSize), prefixDropoutProb(_prefixDropoutProb), sampleWithoutWeights(_sampleWithoutWeights)
{
	rng.seed(currentSeed);

	if (!_contextualMapper.empty())
	{
		utils::ContinuousTrie<utils::TrieNodeEx<uint32_t, uint32_t>> cmTrie(1);
		for (auto& p : _contextualMapper)
		{
			cmTrie.build(p.second.begin(), p.second.end(), p.first + 1);
		}
		cmTrie.fillFail();
		contextualMapper = utils::FrozenTrie<uint32_t, uint32_t>{ cmTrie, ArchTypeHolder<ArchType::balanced>{} };
	}
}

ChrDataset::~ChrDataset() = default;

ChrDataset::ChrDataset(ChrDataset&&) = default;

ChrDataset& ChrDataset::operator=(ChrDataset&&) = default;


void ChrDataset::addSentence(std::string_view sentence, float weight, std::string_view nonLabelPrefix, bool reverse)
{
	ChrTokenizer tokenizer;
	thread_local Vector<int32_t> tokenBuf;
	tokenBuf.resize(sentence.size() + nonLabelPrefix.size());
	std::string joined;
	joined += nonLabelPrefix;
	joined += sentence;
	const size_t prefixSize = reverse ? 0 : tokenizer.encode(nonLabelPrefix, tokenBuf.data(), tokenBuf.size());
	const size_t tokenCnt = tokenizer.encode(joined, tokenBuf.data(), tokenBuf.size());
	auto& sents = this->sents.get();
	sents.emplace_back();
	if (reverse)
	{
		std::reverse(tokenBuf.begin(), tokenBuf.begin() + tokenCnt);
	}
	sents.insert_data(tokenBuf.begin(), tokenBuf.begin() + tokenCnt);
	sentWeights.emplace_back(weight);
	nonLabelPrefixSizes.emplace_back(prefixSize);
	totalWeight += weight;
}

size_t ChrDataset::numSents() const
{
	return sents.get().size();
}

void ChrDataset::seed(size_t newSeed)
{
	currentSeed = newSeed;
	rng.seed(newSeed);
}

void ChrDataset::reset()
{
	seed(currentSeed);
	sentSampled.clear();
	shuffledIdcs.clear();
	totalSampled = 0;
	consumedSents = 0;
}

class InputTokenMapper
{
	const utils::FrozenTrie<uint32_t, uint32_t>& cmTrie;
	const utils::FrozenTrie<uint32_t, uint32_t>::Node* node = nullptr;
public:
	InputTokenMapper(const utils::FrozenTrie<uint32_t, uint32_t>& trie)
		: cmTrie{ trie }
	{
		if (!cmTrie.empty())
		{
			node = cmTrie.root();
		}
	}

	int32_t operator()(int32_t inputToken)
	{
		if (cmTrie.empty() || inputToken == 0)
		{
			return inputToken;
		}
		
		auto* next = node->template nextOpt<ArchType::balanced>(cmTrie, inputToken);
		while (!next)
		{
			node = node->fail();
			if (!node) break;
			next = node->template nextOpt<ArchType::balanced>(cmTrie, inputToken);
		}

		if (next)
		{
			node = next;
			auto val = next->val(cmTrie);
			if (cmTrie.hasMatch(val))
			{
				return val - 1;
			}
			else if (cmTrie.hasSubmatch(val))
			{
				auto sub = next->fail();
				for (; sub; sub = sub->fail())
				{
					val = sub->val(cmTrie);
					if (cmTrie.hasMatch(val))
					{
						break;
					}
				}
				if (sub) return val - 1;
				else return -1;
			}
			return -1;
		}
		else
		{
			node = cmTrie.root();
			return -1;
		}
	}
};

template<class InTy, class OutTy>
size_t ChrDataset::_next(InTy in, OutTy out)
{
	if (sentSampled.size() != sentWeights.size())
	{
		sentSampled.resize(sentWeights.size());
	}

	if (sampleWithoutWeights)
	{
		if (shuffledIdcs.size() != sentWeights.size())
		{
			shuffledIdcs.resize(sentWeights.size());
			std::iota(shuffledIdcs.begin(), shuffledIdcs.end(), 0);
			std::shuffle(shuffledIdcs.begin(), shuffledIdcs.end(), rng);
			consumedSents = 0;
		}
	}
	else
	{
		if (totalSampled <= 0)
		{
			shuffledIdcs.resize(sentWeights.size());
			std::iota(shuffledIdcs.begin(), shuffledIdcs.end(), 0);
		}
		else
		{
			shuffledIdcs.clear();
			const float totalWeight = this->totalWeight,
				totalSampled = this->totalSampled;
			for (size_t i = 0; i < sentWeights.size(); ++i)
			{
				const float w = sentWeights[i] / totalWeight;
				const float s = sentSampled[i] / totalSampled;
				if (s < w)
				{
					shuffledIdcs.emplace_back(i);
				}
			}
		}
		std::shuffle(shuffledIdcs.begin(), shuffledIdcs.end(), rng);
	}
	
	auto& sents = this->sents.get();
	size_t b;
	for (b = 0; b < batchSize; ++b)
	{
		if (sampleWithoutWeights && b + consumedSents >= shuffledIdcs.size())
		{
			break;
		}

		const size_t i = sampleWithoutWeights ? shuffledIdcs[b + consumedSents] : shuffledIdcs[b % shuffledIdcs.size()];
		sentSampled[i] += 1.f;
		totalSampled += 1;

		size_t start = 0;
		if (prefixDropoutProb > 0 && std::generate_canonical<float, 32>(rng) < prefixDropoutProb)
		{
			start = (size_t)((std::max(sents[i].size(), (size_t)2) - 2) * std::generate_canonical<float, 32>(rng));
		}
		const size_t nonLabelPrefixSize = nonLabelPrefixSizes[i];
		const size_t end = std::min(sents[i].size() + 1, causalContextSize);

		InputTokenMapper tokenMapper{ contextualMapper };
		for (size_t j = start; j < end; ++j)
		{
			const auto inputToken = j > 0 ? sents[i][j - 1] : 0;
			*in = tokenMapper(inputToken);
			++in;
			*out = j < nonLabelPrefixSize ? nonVocab : (j < sents[i].size() ? sents[i][j] : 0);
			++out;
		}
		for (size_t j = end - start; j < causalContextSize; ++j)
		{
			*in = nonVocab;
			++in;
			*out = nonVocab;
			++out;
		}
	}
	if (sampleWithoutWeights)
	{
		consumedSents += b;
	}
	return b;
}

size_t ChrDataset::next(int32_t* in, int32_t* out)
{
	return _next(in, out);
}

size_t ChrDataset::next(int64_t* in, int64_t* out)
{
	return _next(in, out);
}

std::vector<float> ChrDataset::getVocabProbs(double epsilon) const
{
	Vector<double> weights(vocabSize(), epsilon);
	
	for (size_t i = 0; i < sentWeights.size(); ++i)
	{
		auto sent = sents.get()[i];
		for (auto token : sent)
		{
			auto v = token;
			if (v < 0 || v >= vocabSize())
			{
				continue;
			}
			weights[v] += sentWeights[i];
		}
		weights[0] += sentWeights[i]; // for EOS
	}

	const double totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0);
	std::vector<float> probs(vocabSize());
	for (size_t i = 0; i < vocabSize(); ++i)
	{
		probs[i] = (float)(weights[i] / totalWeight);
	}
	return probs;
}

std::vector<std::pair<std::vector<uint32_t>, double>> ChrDataset::extractPrefixes(
	float resolution, float minWeight,
	size_t maxLength, size_t numWorkers, bool exclusiveCnt,
	const std::vector<std::pair<uint32_t, uint32_t>>* mergeTargets) const
{
	using Pair = std::pair<std::vector<uint32_t>, double>;
	std::vector<Pair> ret;
	const size_t minCnt = (size_t)ceil(minWeight / resolution);
	PrefixCounter counter{ maxLength, minCnt, numWorkers };
	Vector<int32_t> tokenBuf;
	for (size_t i = 0; i < sents.get().size(); ++i)
	{
		const auto sent = sents.get()[i];
		tokenBuf.clear();
		tokenBuf.emplace_back(0);
		tokenBuf.insert(tokenBuf.end(), sent.begin(), sent.end());
		const size_t n = (size_t)ceil(sentWeights[i] / resolution);
		for (size_t j = 0; j < n; ++j)
		{
			counter.addArray(tokenBuf.data(), tokenBuf.data() + tokenBuf.size());
		}
	}
	auto trie = counter.count();
	if (exclusiveCnt)
	{
		Vector<UnorderedMap<Vector<uint32_t>, size_t>> cntsByLength(maxLength);
		trie.traverse([&](size_t cnt, const std::vector<uint32_t>& prefix)
		{
			if (cnt < minCnt) return;
			if (std::find_if(prefix.begin() + 1, prefix.end(), [](uint32_t t) { return t == 0; }) != prefix.end())
			{
				return;
			}
			Vector<uint32_t> p(prefix.begin(), prefix.end());
			cntsByLength[p.size() - 1].emplace(move(p), cnt);
		});

		Vector<uint32_t> suffix;
		suffix.reserve(maxLength);
		for (size_t i = 1; i < maxLength; ++i)
		{
			for (auto& p : cntsByLength[i])
			{
				suffix.clear();
				suffix.insert(suffix.end(), p.first.begin() + 1, p.first.end());
				auto it = cntsByLength[i - 1].find(suffix);
				if (it == cntsByLength[i - 1].end() || it->second < p.second)
				{
					throw std::runtime_error("This should not happen");
				}
				it->second -= p.second;
			}
		}

		for (auto& cnts : cntsByLength)
		{
			for (auto& p : cnts)
			{
				if (p.second < minCnt) continue;
				ret.emplace_back(std::vector<uint32_t>{ p.first.begin(), p.first.end() }, (double)p.second * resolution);
			}
		}
	}
	else
	{
		trie.traverse([&](size_t cnt, const std::vector<uint32_t>& prefix)
		{
			if (cnt < minCnt) return;
			if (std::find_if(prefix.begin() + 1, prefix.end(), [](uint32_t t) { return t == 0; }) != prefix.end())
			{
				return;
			}
			ret.emplace_back(prefix, (double)cnt * resolution);
		});
	}

	std::sort(ret.begin(), ret.end(), [](const Pair& a, const Pair& b)
	{
		return a.second > b.second;
	});
	return ret;
}

// 한 행에서 bos, 구분자, eos가 차지하는 자리
static constexpr size_t rowOverhead = 3;

constexpr int32_t GenerativeMADataset::padToken;

GenerativeMADataset::GenerativeMADataset(
	const BpeTokenizer& _tokenizer,
	const GenerativeMAOption& _option,
	size_t _batchSize,
	size_t _maxSeqLength,
	size_t _workers,
	const TypoTransformer& _typos
)
	: tokenizer{ std::make_shared<BpeTokenizer>(_tokenizer) },
	// 내부 포인터가 자기 문자열 풀을 가리키므로 옮기지 않고 제자리에서 만든다.
	typoGenerator{ _option.typoProb > 0 && !_typos.empty() ? std::make_shared<PreparedTypoTransformer>(_typos, false) : nullptr },
	workers{ _workers ? make_unique<utils::ThreadPool>(_workers) : nullptr },
	locals( _workers ? _workers : 1 ),
	option{ _option },
	batchSize{ _batchSize },
	maxSeqLength{ _maxSeqLength }
{
	rng.seed(currentSeed);
}

GenerativeMADataset::~GenerativeMADataset() = default;

GenerativeMADataset::GenerativeMADataset(GenerativeMADataset&&) /*noexcept*/ = default;

GenerativeMADataset& GenerativeMADataset::operator=(GenerativeMADataset&&) /*noexcept*/ = default;

void GenerativeMADataset::addSentence(std::string_view sentence)
{
	thread_local std::u16string buf;
	utf8To16(sentence, buf);
	addSentence(std::u16string_view{ buf });
}

void GenerativeMADataset::addSentence(std::u16string_view sentence)
{
	pushItem(sentence, {}, {});
}

void GenerativeMADataset::pushItem(std::u16string_view surface, std::u16string_view forms, const Vector<uint32_t>& infos)
{
	auto& s = sents.get();
	s.emplace_back();
	s.insert_data(surface.begin(), surface.end());
	auto& f = morphemeForms.get();
	f.emplace_back();
	f.insert_data(forms.begin(), forms.end());
	auto& m = morphemeInfos.get();
	m.emplace_back();
	m.insert_data(infos.begin(), infos.end());
}

void GenerativeMADataset::appendMorpheme(std::vector<uint32_t>& out, std::string& buf, std::u16string_view form, POSTag tag, POSTag prevTag) const
{
	// Joiner가 띄어쓰는 자리에만 공백을 붙인다. 실제 문장에서의 모습대로 토큰화되어 토큰 수도 줄어든다.
	const bool insertSpace = !out.empty()
		&& cmb::isSpaceInsertable(clearIrregular(prevTag), clearIrregular(tag), form);
	buf.assign(insertSpace ? 1 : 0, ' ');
	buf += utf16To8(form);
	tokenizer->encode(out, buf);
	out.emplace_back(tagTokenId(tag));
}

size_t GenerativeMADataset::addAnalyzedCorpus(std::istream& is)
{
	struct Sentence
	{
		std::u16string surface, forms;
		Vector<uint32_t> infos; // (forms 안에서 형태가 끝나는 위치 << 8 | 품사)
	};

	std::u16string packSurface, packForms;
	Vector<uint32_t> packInfos;
	std::vector<uint32_t> packMorphemeTokens, surfaceTokens;
	std::string buf;
	size_t packSurfaceTokens = 0;

	const auto emitPack = [&]()
	{
		if (packInfos.empty()) return;
		pushItem(packSurface, packForms, packInfos);
		packSurface.clear();
		packForms.clear();
		packInfos.clear();
		packMorphemeTokens.clear();
		packSurfaceTokens = 0;
	};

	const auto appendToPack = [&](const Sentence& sent)
	{
		if (!packSurface.empty()) packSurface += u' ';
		packSurface += sent.surface;
		const size_t formOffset = packForms.size();
		packForms += sent.forms;
		POSTag prevTag = packInfos.empty() ? POSTag::unknown : (POSTag)(packInfos.back() & 0xFF);
		size_t start = 0;
		for (const uint32_t info : sent.infos)
		{
			const size_t end = info >> 8;
			const POSTag tag = (POSTag)(info & 0xFF);
			appendMorpheme(packMorphemeTokens, buf, std::u16string_view{ sent.forms }.substr(start, end - start), tag, prevTag);
			packInfos.emplace_back((uint32_t)((formOffset + end) << 8) | (uint8_t)tag);
			prevTag = tag;
			start = end;
		}
		surfaceTokens.clear();
		tokenizer->encode(surfaceTokens, utf16To8(packSurface));
		packSurfaceTokens = surfaceTokens.size();
	};

	// 같은 문서의 문장은 행이 maxSeqLength를 넘지 않는 만큼 이어붙이고, 넘치면 그때까지를 내보낸 뒤 새로 시작한다.
	// 길이는 buildWorkItem과 같은 방식으로 인코딩해서 재므로, 잡음이 없다면 이어붙인 데이터는 잘리지 않는다.
	const auto addToPack = [&](const Sentence& sent)
	{
		if (!packInfos.empty())
		{
			const size_t surfaceLen = packSurface.size(), formsLen = packForms.size(), infosLen = packInfos.size(),
				morphemeTokenLen = packMorphemeTokens.size(), prevSurfaceTokens = packSurfaceTokens;
			appendToPack(sent);
			if (rowOverhead + packSurfaceTokens + packMorphemeTokens.size() <= maxSeqLength) return;

			packSurface.resize(surfaceLen);
			packForms.resize(formsLen);
			packInfos.resize(infosLen);
			packMorphemeTokens.resize(morphemeTokenLen);
			packSurfaceTokens = prevSurfaceTokens;
			emitPack();
		}
		appendToPack(sent);
	};

	Sentence sent;
	bool dropSentence = false;
	size_t numAdded = 0;
	const auto finishSentence = [&]()
	{
		if (!dropSentence && !sent.infos.empty())
		{
			addToPack(sent);
			++numAdded;
		}
		sent = Sentence{};
		dropSentence = false;
	};

	std::string line;
	std::u16string u16Line;
	size_t lineNo = 0, blankLines = 0;
	while (std::getline(is, line))
	{
		++lineNo;
		if (line.find_first_not_of(" \t\r") == std::string::npos)
		{
			++blankLines;
			continue;
		}
		if (blankLines)
		{
			// 빈 줄 하나는 문장 경계, 두 개 이상은 문서 경계다.
			finishSentence();
			if (blankLines >= 2) emitPack();
			blankLines = 0;
		}
		if (dropSentence) continue;

		if (line.back() == '\r') line.pop_back();
		utf8To16(line, u16Line);
		const auto fields = split(std::u16string_view{ u16Line }, u'\t');
		if (fields.size() < 3 || fields.size() % 2 == 0)
		{
			std::cerr << "GenerativeMADataset::addAnalyzedCorpus: dropped a sentence with a malformed line " << lineNo << ": " << line << std::endl;
			dropSentence = true;
			continue;
		}

		if (!sent.surface.empty()) sent.surface += u' ';
		sent.surface += fields[0];
		for (size_t i = 1; i < fields.size(); i += 2)
		{
			auto form = fields[i];
			const auto tagStr = fields[i + 1];
			// 의미 번호(형태__N)는 형태에 포함시키지 않는다.
			const size_t sensePos = form.find(u"__");
			if (sensePos != form.npos) form = form.substr(0, sensePos);
			const POSTag tag = toPOSTag(tagStr);
			if (tag == POSTag::unknown || tag == POSTag::max || form.empty())
			{
				std::cerr << "GenerativeMADataset::addAnalyzedCorpus: dropped a sentence with the morpheme `"
					<< utf16To8(form) << "/" << utf16To8(tagStr) << "` at line " << lineNo << std::endl;
				dropSentence = true;
				break;
			}
			// Kiwi는 어미의 첫 글자 '아'를 '어'로 통일해서 분석하므로 그에 맞춘다.
			KString normForm = normalizeHangul(form);
			if (normForm[0] == u'아' && tagStr[0] == u'E') normForm[0] = u'어';
			sent.forms += joinHangul(normForm);
			sent.infos.emplace_back((uint32_t)(sent.forms.size() << 8) | (uint8_t)tag);
		}
	}
	finishSentence();
	emitPack();
	return numAdded;
}

size_t GenerativeMADataset::numSents() const
{
	return sents.get().size();
}

size_t GenerativeMADataset::numEstimBatches() const
{
	if (!batchSize) return 0;
	return (numSents() * 2 + batchSize - 1) / batchSize;
}

size_t GenerativeMADataset::vocabSize() const
{
	return tokenizer ? tokenizer->getVocab().size() : 0;
}

void GenerativeMADataset::seed(size_t newSeed)
{
	currentSeed = newSeed;
	rng.seed(newSeed);
}

void GenerativeMADataset::reset()
{
	while (!futures.empty())
	{
		futures.front().get();
		futures.pop_front();
	}
	current = WorkItem{};
	consumedRows = 0;
	passedSents = 0;
	truncatedSents = 0;
	insertedTypos = 0;
	removedSpaces = 0;
	insertedSpaces = 0;

	// rng를 다시 seed하지 않아야 epoch마다 다른 순서로 섞인다.
	if (shuffledIdx.size() < numSents())
	{
		const size_t s = shuffledIdx.size();
		shuffledIdx.resize(numSents());
		std::iota(shuffledIdx.begin() + s, shuffledIdx.end(), (uint32_t)s);
	}
	std::shuffle(shuffledIdx.begin(), shuffledIdx.end(), rng);
}

uint32_t GenerativeMADataset::tagTokenId(POSTag tag) const
{
	const auto id = option.posTagTokenIds[(uint8_t)tag];
	if (id) return id;
	return option.posTagTokenIds[(uint8_t)clearIrregular(tag)];
}

size_t GenerativeMADataset::sentsPerWorkItem() const
{
	// 문장 하나가 2행을 만든다. 작업 단위가 너무 잘게 나뉘지 않도록 하한을 둔다.
	return std::max((size_t)8, (batchSize + 1) / 2);
}

static void appendRow(Vector<int32_t>& out,
	uint32_t bos,
	const uint32_t* first, size_t firstSize,
	uint32_t sep,
	const uint32_t* second, size_t secondSize,
	uint32_t eos,
	size_t maxSeqLength)
{
	const size_t base = out.size();
	out.resize(base + maxSeqLength, GenerativeMADataset::padToken);
	auto p = out.begin() + base;
	const auto end = p + maxSeqLength;
	const auto putOne = [&](uint32_t t) { if (p != end) *p++ = (int32_t)t; };
	const auto putAll = [&](const uint32_t* b, size_t n)
	{
		n = std::min(n, (size_t)(end - p));
		p = std::copy(b, b + n, p);
	};
	putOne(bos);
	putAll(first, firstSize);
	putOne(sep);
	putAll(second, secondSize);
	putOne(eos);
}

// 어절 사이의 공백 덩어리를 지우거나 공백이 아닌 두 글자 사이에 공백을 넣는다.
// 서로게이트 쌍의 뒷글자나 결합 문자처럼 앞 글자에 붙어야 하는 글자 앞에는 넣지 않는다.
static void perturbSpaces(std::u16string& out, std::u16string_view text,
	float removeProb, float insertProb, std::mt19937_64& rng,
	size_t& numRemoved, size_t& numInserted)
{
	const auto attachesToPrev = [](char16_t c)
	{
		return isLowSurrogate(c)
			|| (0x0300 <= c && c <= 0x036F) // 결합 분음 부호
			|| c == 0x200D // 폭 없는 결합자(ZWJ)
			|| (0xFE00 <= c && c <= 0xFE0F) // 이체자 선택자
			|| (0x1160 <= c && c <= 0x11FF) // 첫가끝 한글의 중성, 종성
			|| (0xD7B0 <= c && c <= 0xD7FF);
	};
	std::bernoulli_distribution removeSpace{ removeProb }, insertSpace{ insertProb };

	out.clear();
	for (size_t i = 0; i < text.size(); )
	{
		if (isSpace(text[i]))
		{
			size_t j = i;
			while (j < text.size() && isSpace(text[j])) ++j;
			if (i > 0 && j < text.size() && removeProb > 0 && removeSpace(rng)) ++numRemoved;
			else out.append(text.begin() + i, text.begin() + j);
			i = j;
			continue;
		}

		out.push_back(text[i]);
		const size_t next = i + 1;
		if (insertProb > 0 && next < text.size() && !isSpace(text[next])
			&& !isHighSurrogate(text[i]) && text[i] != 0x200D && !attachesToPrev(text[next])
			&& insertSpace(rng))
		{
			out.push_back(u' ');
			++numInserted;
		}
		i = next;
	}
}

GenerativeMADataset::WorkItem GenerativeMADataset::buildWorkItem(size_t localId, size_t sentFirst, size_t sentLast, uint64_t seed)
{
	auto& local = locals[localId];
	const auto& allSents = sents.get();
	const auto& allForms = morphemeForms.get();
	const auto& allInfos = morphemeInfos.get();
	const AnalyzeOption analyzeOption;
	std::mt19937_64 itemRng{ seed };
	WorkItem ret;
	ret.data.reserve((sentLast - sentFirst) * 2 * maxSeqLength);

	for (size_t i = sentFirst; i < sentLast; ++i)
	{
		const auto sent = allSents[shuffledIdx[i]];
		local.u16Buf.assign(sent.begin(), sent.end());
		if (local.u16Buf.empty()) continue;

		local.textBuf = utf16To8(local.u16Buf);
		local.surfaceBuf.clear();
		tokenizer->encode(local.surfaceBuf, local.textBuf);
		if (local.surfaceBuf.empty()) continue;

		local.morphemeBuf.clear();
		POSTag prevTag = POSTag::unknown;
		const auto infos = allInfos[shuffledIdx[i]];
		if (infos.begin() == infos.end())
		{
			const auto res = kiwiInst->analyze(local.u16Buf, analyzeOption);
			for (auto& t : res.first)
			{
				if (t.str.empty()) continue;
				appendMorpheme(local.morphemeBuf, local.formBuf, t.str, t.tag, prevTag);
				prevTag = t.tag;
			}
		}
		else
		{
			const auto forms = allForms[shuffledIdx[i]];
			local.formsBuf.assign(forms.begin(), forms.end());
			size_t start = 0;
			for (const uint32_t info : infos)
			{
				const size_t end = info >> 8;
				const POSTag tag = (POSTag)(info & 0xFF);
				appendMorpheme(local.morphemeBuf, local.formBuf, std::u16string_view{ local.formsBuf }.substr(start, end - start), tag, prevTag);
				prevTag = tag;
				start = end;
			}
		}
		if (local.morphemeBuf.empty()) continue;

		// 잡음은 ToMorpheme 방향의 입력에만 넣는다. ToSurface 방향의 출력까지 더럽히면 모델이 잡음을 만들어내도록 배운다.
		const std::u16string* inputText = &local.u16Buf;
		if (typoGenerator)
		{
			const size_t numTypos = typoGenerator->sampleTypos(local.noisyBuf, local.u16Buf, option.typoProb, itemRng, option.typoCostThreshold, option.typoCostScale);
			if (numTypos)
			{
				ret.numTypos += numTypos;
				inputText = &local.noisyBuf;
			}
		}
		// 오타 규칙의 조건이 원래 띄어쓰기로 판단되도록 띄어쓰기는 오타 다음에 흐트러뜨린다.
		if (option.spaceRemoveProb > 0 || option.spaceInsertProb > 0)
		{
			size_t removed = 0, inserted = 0;
			perturbSpaces(local.spacedBuf, *inputText, option.spaceRemoveProb, option.spaceInsertProb, itemRng, removed, inserted);
			if (removed || inserted)
			{
				ret.numRemovedSpaces += removed;
				ret.numInsertedSpaces += inserted;
				inputText = &local.spacedBuf;
			}
		}

		const std::vector<uint32_t>* inputSurface = &local.surfaceBuf;
		if (inputText != &local.u16Buf)
		{
			local.noisySurfaceBuf.clear();
			tokenizer->encode(local.noisySurfaceBuf, utf16To8(*inputText));
			inputSurface = &local.noisySurfaceBuf;
		}

		if (std::max(inputSurface->size(), local.surfaceBuf.size()) + local.morphemeBuf.size() + rowOverhead > maxSeqLength)
		{
			++ret.numTruncatedSents;
		}

		appendRow(ret.data, option.bosTokenId,
			inputSurface->data(), inputSurface->size(), option.toMorphemeTokenId,
			local.morphemeBuf.data(), local.morphemeBuf.size(), option.eosTokenId, maxSeqLength);
		appendRow(ret.data, option.bosTokenId,
			local.morphemeBuf.data(), local.morphemeBuf.size(), option.toSurfaceTokenId,
			local.surfaceBuf.data(), local.surfaceBuf.size(), option.eosTokenId, maxSeqLength);
		ret.numRows += 2;
	}
	return ret;
}

bool GenerativeMADataset::prepareMore()
{
	const size_t total = shuffledIdx.size();
	if (workers)
	{
		const size_t maxInFlight = workers->size() * 2;
		while (futures.size() < maxInFlight && passedSents < total)
		{
			const size_t first = passedSents;
			const size_t last = std::min(first + sentsPerWorkItem(), total);
			passedSents = last;
			const uint64_t seed = rng();
			futures.emplace_back(workers->enqueue([this, first, last, seed](size_t threadId)
			{
				return buildWorkItem(threadId, first, last, seed);
			}));
		}
		if (futures.empty()) return false;
		current = futures.front().get();
		futures.pop_front();
	}
	else
	{
		if (passedSents >= total) return false;
		const size_t first = passedSents;
		passedSents = std::min(first + sentsPerWorkItem(), total);
		current = buildWorkItem(0, first, passedSents, rng());
	}
	consumedRows = 0;
	truncatedSents += current.numTruncatedSents;
	insertedTypos += current.numTypos;
	removedSpaces += current.numRemovedSpaces;
	insertedSpaces += current.numInsertedSpaces;
	return true;
}

template<class Ty>
size_t GenerativeMADataset::_next(Ty* inputIds)
{
	if (!batchSize || !maxSeqLength)
	{
		throw std::invalid_argument{ "`batchSize` and `maxSeqLength` must be greater than 0" };
	}

	if (!kiwiInst || !kiwiInst->ready() || !tokenizer || !tokenizer->ready())
	{
		throw std::runtime_error{ "GenerativeMADataset must be created by `KiwiBuilder::makeGenerativeMADataset`" };
	}

	// reset() 전이거나 reset() 이후 문장이 추가된 경우
	if (shuffledIdx.size() != numSents()) reset();

	size_t written = 0;
	while (written < batchSize)
	{
		// 공백뿐인 문장만 모여 행이 0개인 작업 단위라면 다시 이 조건에 걸려 다음 것을 채운다.
		if (consumedRows >= current.numRows)
		{
			if (!prepareMore()) break;
			continue;
		}

		const size_t n = std::min(current.numRows - consumedRows, batchSize - written);
		std::copy(current.data.begin() + consumedRows * maxSeqLength,
			current.data.begin() + (consumedRows + n) * maxSeqLength,
			inputIds + written * maxSeqLength);
		consumedRows += n;
		written += n;
	}
	return written;
}

size_t GenerativeMADataset::next(int32_t* input_ids)
{
	return _next(input_ids);
}

size_t GenerativeMADataset::next(int64_t* input_ids)
{
	return _next(input_ids);
}
