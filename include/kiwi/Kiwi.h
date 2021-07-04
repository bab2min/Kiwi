#pragma once

#include <future>
#include "Types.h"
#include "Utils.h"
#include "Trainer.h"
#include "Trie.hpp"
#include "PatternMatcher.h"
#include "FrozenTrie.h"

namespace kiwi
{
	struct Form;
	struct Morpheme;
	struct KTrie;
	struct KGraphNode;
	class KNLangModel;

	namespace v1
	{
		class Kiwi
		{
			bool integrateAllomorph = true;
			float cutOffThreshold = 5;

			std::vector<Form> forms;
			std::vector<Morpheme> morphemes;
			utils::FrozenTrie<kchar_t, const Form*> formTrie;
			std::unique_ptr<PatternMatcher> pm;

			using Path = Vector<std::tuple<const Morpheme*, KString, uint32_t>>;
			auto findBestPath(const Vector<KGraphNode>& graph, const KNLangModel* knlm, size_t topN) const->Vector<std::pair<Path, float>>;
			std::vector<TokenResult> analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, size_t topN, Match matchOptions) const;

			const Morpheme* getDefaultMorpheme(POSTag tag) const { return &morphemes[(size_t)tag + 1]; }

		public:
			Kiwi() = default;
			TokenResult analyze(const std::u16string& str, Match matchOptions) const
			{
				return analyze(str, 1, matchOptions)[0];
			}
			TokenResult analyze(const std::string& str, Match matchOptions) const
			{
				return analyze(utf8To16(str), matchOptions);
			}

			std::vector<TokenResult> analyze(const std::u16string& str, size_t topN, Match matchOptions) const;
			std::vector<TokenResult> analyze(const std::string& str, size_t topN, Match matchOptions) const
			{
				return analyze(utf8To16(str), topN, matchOptions);
			}

			std::future<std::vector<TokenResult>> asyncAnalyze(const std::string& str, size_t topN, Match matchOptions) const;
			void analyze(size_t topN, const U16Reader& reader, const std::function<void(size_t, std::vector<TokenResult>&&)>& receiver, Match matchOptions) const;
		};

		class KiwiBuilder
		{
		public:
			KiwiBuilder(const std::string& modelPath);
			bool addWord(const std::u16string& str, POSTag tag = POSTag::nnp, float score = 0);
			int loadDictionary(const std::string& dictPath);

			Kiwi build() const;
		};
	}
}
