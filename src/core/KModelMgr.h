#pragma once

#include <tuple>
#include <unordered_map>

#include "KNLangModel.h"
#include "KForm.h"

namespace kiwi
{
	struct FormCond : public std::tuple<k_string, KCondVowel, KCondPolarity>
	{
		using std::tuple<k_string, KCondVowel, KCondPolarity>::tuple;
		
		const k_string& form() const { return std::get<0>(*this); }
		const KCondVowel& vowel() const { return std::get<1>(*this); }
		const KCondPolarity& polar() const { return std::get<2>(*this); }
	};
}

namespace std
{
	template<>
	struct hash<kiwi::FormCond>
	{
		size_t operator()(const kiwi::FormCond& fc) const
		{
			return hash<kiwi::k_string>{}(fc.form()) ^ ( (size_t)fc.vowel() | ((size_t)fc.polar() << 8));
		}
	};
}

namespace kiwi
{
	class KModelMgr
	{
	protected:
		const char* modelPath = nullptr;
		std::vector<KForm> forms;
		std::vector<KMorpheme> morphemes;
		std::unordered_map<FormCond, size_t> formMap;
		size_t baseTrieSize = 0;
		size_t extraTrieSize = 0;
		std::vector<KTrie> trieRoot;
		std::shared_ptr<KNLangModel> langMdl;

		using MorphemeMap = std::unordered_map<std::pair<k_string, KPOSTag>, size_t>;
		void loadMMFromTxt(std::istream& is, MorphemeMap& morphMap, std::unordered_map<KPOSTag, float>* posWeightSum, const std::function<bool(float, KPOSTag)>& selector);
		void loadCMFromTxt(std::istream& is, MorphemeMap& morphMap);
		void loadPCMFromTxt(std::istream& is, MorphemeMap& morphMap);
		KNLangModel::AllomorphSet loadAllomorphFromTxt(std::istream& is, const MorphemeMap& morphMap);
		void loadCorpusFromTxt(std::istream& is, MorphemeMap& morphMap, const KNLangModel::AllomorphSet& ams);
		void updateForms();
		void saveMorphBin(std::ostream& os) const;
		size_t estimateTrieSize() const;
		template<class _Istream>
		void loadMorphBin(_Istream& is);
		KForm& formMapper(k_string form, KCondVowel vowel, KCondPolarity polar);
	public:
		KModelMgr(const char* modelPath = "");
		KModelMgr(const KModelMgr&) = default;
		void addUserWord(const k_string& form, KPOSTag tag, float userScore = 10);
		void solidify();
		const KTrie* getTrie() const { if (trieRoot.empty()) return nullptr; return &trieRoot[0]; }

		const KNLangModel* getLangModel() const { return langMdl.get(); }
		const KMorpheme* getMorphemes() const { return &morphemes[0]; }

		const KMorpheme* getDefaultMorpheme(KPOSTag tag) const { return &morphemes[1] + (size_t)tag; }
		std::unordered_set<k_string> getAllForms() const;
	};

}