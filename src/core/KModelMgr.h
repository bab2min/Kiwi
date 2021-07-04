#pragma once

#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "KNLangModel.h"
#include "KForm.h"
#include <kiwi/FrozenTrie.h>
#include <kiwi/Knlm.h>

namespace kiwi
{
	struct FormCond : public std::tuple<KString, CondVowel, CondPolarity>
	{
		using std::tuple<KString, CondVowel, CondPolarity>::tuple;
		
		const KString& form() const { return std::get<0>(*this); }
		const CondVowel& vowel() const { return std::get<1>(*this); }
		const CondPolarity& polar() const { return std::get<2>(*this); }
	};
}

namespace std
{
	template<>
	struct hash<kiwi::FormCond>
	{
		size_t operator()(const kiwi::FormCond& fc) const
		{
			return hash<kiwi::KString>{}(fc.form()) ^ ( (size_t)fc.vowel() | ((size_t)fc.polar() << 8));
		}
	};
}

namespace kiwi
{
	class KModelMgr
	{
	protected:
		const char* modelPath = nullptr;
		std::vector<Form> forms;
		std::vector<Morpheme> morphemes;
		std::unordered_map<FormCond, size_t> formMap;
		size_t baseTrieSize = 0;
		size_t extraTrieSize = 0;
		utils::ContinuousTrie<KTrie> formTrie;
	public:
		utils::FrozenTrie<kchar_t, const Form*> fTrie;
	protected:
		std::shared_ptr<KNLangModel> langMdl;
		std::shared_ptr<lm::KNLangModelBase> langMdl2;

		using MorphemeMap = std::unordered_map<std::pair<KString, POSTag>, size_t>;
		void loadMMFromTxt(std::istream& is, MorphemeMap& morphMap, std::unordered_map<POSTag, float>* posWeightSum, const std::function<bool(float, POSTag)>& selector);
		void loadCMFromTxt(std::istream& is, MorphemeMap& morphMap);
		void loadPCMFromTxt(std::istream& is, MorphemeMap& morphMap);
		KNLangModel::AllomorphSet loadAllomorphFromTxt(std::istream& is, const MorphemeMap& morphMap);
		void loadCorpusFromTxt(std::istream& is, MorphemeMap& morphMap, const KNLangModel::AllomorphSet& ams);
		void addCorpusTo(Vector<Vector<uint16_t>>& out, std::istream& is, MorphemeMap& morphMap, const KNLangModel::AllomorphSet& ams);

		void updateForms();
		void saveMorphBin(std::ostream& os) const;
		size_t estimateTrieSize() const;
		template<class _Istream>
		void loadMorphBin(_Istream& is);
		Form& formMapper(KString form, CondVowel vowel, CondPolarity polar);
	public:
		KModelMgr(const char* modelPath = "");
		KModelMgr(const KModelMgr&) = default;
		KModelMgr(KModelMgr&&) = default;
		void addUserWord(const KString& form, POSTag tag, float userScore = 10);
		void solidify();
		bool ready() const { return !fTrie.empty(); }
		const KTrie* getTrie() const { if (formTrie.empty()) return nullptr; return &formTrie.root(); }

		const KNLangModel* getLangModel() const { return langMdl.get(); }
		const lm::KNLangModelBase* getLangModel2() const { return langMdl2.get(); }
		const Morpheme* getMorphemes() const { return &morphemes[0]; }
		size_t getNumMorphemes() const { return morphemes.size(); }

		const std::vector<Form>& getForms() const { return forms; }

		const Morpheme* getDefaultMorpheme(POSTag tag) const { return &morphemes[1] + (size_t)tag; }
		std::unordered_set<KString> getAllForms() const;
	};

}