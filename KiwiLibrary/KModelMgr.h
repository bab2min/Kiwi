#pragma once

#include "KNLangModel.h"
class KModelMgr
{
protected:
	const char* modelPath = nullptr;
	std::vector<KForm> forms;
	std::vector<KMorpheme> morphemes;
	std::unordered_map<k_string, size_t> formMap;
	size_t dictMorphSize;
#ifdef TRIE_ALLOC_ARRAY
	size_t baseTrieSize = 0;
	size_t extraTrieSize = 0;
	std::vector<KTrie> trieRoot;
#else
	shared_ptr<KTrie> trieRoot;
#endif
	typedef std::unordered_map<std::pair<k_string, KPOSTag>, size_t> morphemeMap;
	KNLangModel langMdl;
	void loadMMFromTxt(std::istream& is, morphemeMap& morphMap);
	void loadCMFromTxt(std::istream& is, morphemeMap& morphMap);
	void loadPCMFromTxt(std::istream& is, morphemeMap& morphMap);
	KNLangModel::AllomorphSet loadAllomorphFromTxt(std::istream& is, const morphemeMap& morphMap);
	void loadCorpusFromTxt(std::istream& is, morphemeMap& morphMap, const KNLangModel::AllomorphSet& ams);
	void saveMorphBin(std::ostream& os) const;
	size_t estimateTrieSize() const;
	void loadMorphBin(std::istream& is);
	KForm& formMapper(k_string form);
public:
	KModelMgr(const char* modelPath = "");
	void addUserWord(const k_string& form, KPOSTag tag, float userScore = 10);
	void addUserRule(const k_string& form, const std::vector<std::pair<k_string, KPOSTag>>& morphs);
	void solidify();
#ifdef TRIE_ALLOC_ARRAY
	const KTrie* getTrie() const { return &trieRoot[0]; }
#else
	const KTrie* getTrie() const { return trieRoot.get(); }
#endif

	const KNLangModel* getLangModel() const { return &langMdl; }
	const KMorpheme* getMorphemes() const { return &morphemes[0]; }

	bool isUserWord(const KMorpheme* morph) const { return morph >= &morphemes[0] + dictMorphSize; }
	const KMorpheme* getDefaultMorpheme(KPOSTag tag) const { return &morphemes[1] + (size_t)tag; }
};

