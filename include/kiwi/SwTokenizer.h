/**
 * @file SwTokenizer.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Subword Tokenizer
 * @version 0.14.1
 * @date 2022-07-28
 *
 *
 */

#pragma once

#include "Types.h"
#include "FrozenTrie.h"
#include "Utils.h"
#include "Trie.hpp"

namespace kiwi
{
	class Kiwi;
	template<class Ty> class RaggedVector;

	struct SwToken
	{
		std::string form;
		POSTag pos = POSTag::unknown;
		uint8_t special = 0;

		SwToken(const std::string& _form = {}, POSTag _pos = POSTag::unknown, uint8_t _special = 0)
			: form{ _form }, pos{ _pos }, special{ _special }
		{
		}
	};

	struct SwTokenizerConfig
	{
		std::string unkToken = "[UNK]";
		std::string clsToken;
		std::string sepToken;
		std::string padToken;
		std::string maskToken;
		std::string bosToken;
		std::string eosToken;
		size_t multipleUnkTokens = 0;
		size_t vocabSize = 0;
		bool doLowercase = false;
		bool splitChinese = true;
		bool wholeTokenUnk = false;
		bool integrateAllomoprh = true;
		bool splitPunct = true;
		bool simpleTag = true;
		bool splitVerb = true;
		bool splitEomi = true;
		bool useGlueToken = true;

		size_t numSpecialTokens() const
		{
			size_t ret = 0;
			for (auto p : { &unkToken, &clsToken, &sepToken, &padToken, &maskToken, &bosToken, &eosToken })
			{
				ret += p->empty() ? 0 : 1;
			}
			return ret;
		}
	};

	struct UnigramSwTrainerConfig
	{
		double chrCoverage = 0.9995;
		bool reduceStrict = false;
		bool removeRepetitive = true;
	};

	class SwTokenizer
	{
		const Kiwi* kiwi = nullptr;
		Vector<SwToken> vocab;
		utils::FrozenTrie<kchar_t, uint32_t> trie;
		SwTokenizerConfig config;
		Vector<uint32_t> morphToSw;
		Vector<uint32_t> swToMorph;

		bool tokenizeSubword(const KString& str, std::vector<uint32_t>& out) const;

	public:
		SwTokenizer(const Kiwi& kiwi, const SwTokenizerConfig& config, const std::vector<SwToken>& tokens);
		SwTokenizer(const Kiwi& kiwi, const SwTokenizerConfig& config, size_t numMorphemes, const std::vector<std::string>& subwordTokens);
		SwTokenizer(const SwTokenizer&);
		SwTokenizer(SwTokenizer&&);
		~SwTokenizer();
		SwTokenizer& operator=(const SwTokenizer&);
		SwTokenizer& operator=(SwTokenizer&&);

		size_t vocabSize() const { return vocab.size(); }

		std::vector<uint32_t> encode(const std::string& str, std::vector<std::pair<uint32_t, uint32_t>>* offset = nullptr) const;
		std::string decode(const std::vector<uint32_t>& ids) const;
	};

	class UnigramSwTrainer
	{
		enum class PrefixAvailability : uint8_t;

		struct WordCand
		{
			const Morpheme* morph;
			const Morpheme* suffix;
			const Morpheme* baseEomi = nullptr;
			bool hasBoundaries = false;
			HiddenMember<RaggedVector<int32_t>, sizeof(Vector<size_t>) * 2> tokenizations;

			WordCand(const Morpheme* _morph = nullptr, const Morpheme* _suffix = nullptr);
			WordCand(const WordCand&);
			WordCand(WordCand&&);
			WordCand& operator=(const WordCand&);
			WordCand& operator=(WordCand&&);
			~WordCand();
		};

		const Kiwi* kiwi = nullptr;
		SwTokenizerConfig config;
		UnigramSwTrainerConfig trainConfig;
		size_t knownPrefixSize = 0;
		size_t currentVocabSize = 0;

		UnorderedMap<std::u16string, size_t> wordMap;
		Vector<std::pair<const std::u16string, size_t>*> invWordMap;
		Vector<size_t> wordCnts;
		UnorderedMap<size_t, WordCand> wordSuffix;
		UnorderedMap<std::pair<KString, POSTag>, const Morpheme*> reprMorphMap;
		HiddenMember<RaggedVector<int32_t>, sizeof(Vector<size_t>) * 2> sents;
		Vector<size_t> tokenFreqs;

		Vector<std::u16string> chrPrefix;
		utils::FrozenTrie<char16_t, size_t> chrTrie;
		Vector<Vector<uint32_t>> wordBestTokenizations;

		Vector<uint32_t> prefixFreqs;
		Vector<double> prefixLProbs;
		Vector<PrefixAvailability> prefixAvailable;

		void addWord(const std::u16string& s, const Vector<const Morpheme*>& morphs, const Vector<size_t>& boundaries);

		template<class Feeder>
		size_t _addSentences(Feeder&& feeder);

		Vector<uint32_t> tokenizeShort(nonstd::u16string_view s) const;
		Vector<uint32_t> tokenizeShort(nonstd::u16string_view s, const Vector<int32_t>& boundaries) const;
		std::pair<Vector<uint32_t>, float> tokenizeBest(nonstd::u16string_view s, const Vector<int32_t>* boundaries = nullptr) const;
		std::pair<Vector<uint32_t>, float> tokenizeBest(const WordCand& m) const;

		const Morpheme* toReprMorph(const Morpheme* m);

	public:
		UnigramSwTrainer(const Kiwi& kiwi, const SwTokenizerConfig& config, const UnigramSwTrainerConfig& trainConfig);
		UnigramSwTrainer(const UnigramSwTrainer&);
		UnigramSwTrainer(UnigramSwTrainer&&);
		~UnigramSwTrainer();
		UnigramSwTrainer& operator=(const UnigramSwTrainer&);
		UnigramSwTrainer& operator=(UnigramSwTrainer&&);

		size_t addSentences(const std::function<std::string()>& feeder);
		size_t addSentences(const std::function<std::u16string()>& feeder);

		float buildSubwordVocabs(const size_t minCnt = 5, const size_t maxPrefixLength = 15);
		float updateProb(bool init = false);
		size_t reduceVocab(float ratio, size_t minVocabSize = 0);
		void updateTokenization();
		
		std::vector<std::u16string> getUnkExamples() const;

		size_t getCurrentVocabSize() const { return currentVocabSize + config.numSpecialTokens(); }

		std::ostream& writeVocabs(std::ostream& os) const;
	};
}
