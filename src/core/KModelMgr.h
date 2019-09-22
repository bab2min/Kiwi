#pragma once

#include "KNLangModel.h"
namespace kiwi
{
	class KModelMgr
	{
	protected:
		const char* modelPath = nullptr;
		std::vector<KForm> forms;
		std::vector<KMorpheme> morphemes;
		std::unordered_map<k_string, size_t> formMap;
		size_t baseTrieSize = 0;
		size_t extraTrieSize = 0;
		std::vector<KTrie> trieRoot;
		std::shared_ptr<KNLangModel> langMdl;

		typedef std::unordered_map<std::pair<k_string, KPOSTag>, size_t> morphemeMap;
		void loadMMFromTxt(std::istream& is, morphemeMap& morphMap, std::unordered_map<KPOSTag, float>* posWeightSum, const std::function<bool(float, KPOSTag)>& selector);
		void loadCMFromTxt(std::istream& is, morphemeMap& morphMap);
		void loadPCMFromTxt(std::istream& is, morphemeMap& morphMap);
		KNLangModel::AllomorphSet loadAllomorphFromTxt(std::istream& is, const morphemeMap& morphMap);
		void loadCorpusFromTxt(std::istream& is, morphemeMap& morphMap, const KNLangModel::AllomorphSet& ams);
		void saveMorphBin(std::ostream& os) const;
		size_t estimateTrieSize() const;
		template<class _Istream>
		void loadMorphBin(_Istream& is);
		KForm& formMapper(k_string form);
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