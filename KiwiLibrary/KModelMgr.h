#pragma once

struct KTrie;

class KModelMgr
{
protected:
	const char* modelPath = nullptr;
	vector<KForm> forms;
	vector<KMorpheme> morphemes;
	unordered_map<string, size_t> formMap;
#ifdef TRIE_ALLOC_ARRAY
	size_t extraTrieSize = 0;
	vector<KTrie> trieRoot;
#else
	shared_ptr<KTrie> trieRoot;
#endif
	KPOSTag maxiumBtwn[(size_t)KPOSTag::MAX][(size_t)KPOSTag::MAX];
	KPOSTag maxiumVBtwn[(size_t)KPOSTag::MAX][(size_t)KPOSTag::MAX];
	float posTransition[(size_t)KPOSTag::MAX][(size_t)KPOSTag::MAX];
	void loadPOSFromTxt(const char* filename);
	void savePOSBin(const char* filename) const;
	void loadPOSBin(const char* filename);
	void loadMMFromTxt(const char * filename, unordered_map<pair<string, KPOSTag>, size_t>& morphMap);
	void loadCMFromTxt(const char * filename, unordered_map<pair<string, KPOSTag>, size_t>& morphMap);
	void loadPCMFromTxt(const char * filename, unordered_map<pair<string, KPOSTag>, size_t>& morphMap);
	void saveMorphBin(const char* filename) const;
	void loadMorphBin(const char* filename);
	void loadDMFromTxt(const char* filename);
	KForm& formMapper(string form)
	{
		auto it = formMap.find(form);
		if (it != formMap.end()) return forms[it->second];
		size_t id = formMap.size();
		formMap.emplace(form, id);
		forms.emplace_back(form);
		return forms[id];
	};
public:
	KModelMgr(const char* modelPath = "");
	void addUserWord(const string& form, KPOSTag tag);
	void addUserRule(const string& form, const vector<pair<string, KPOSTag>>& morphs);
	void solidify();
#ifdef TRIE_ALLOC_ARRAY
	const KTrie* getTrie() const { return &trieRoot[0]; }
#else
	const KTrie* getTrie() const { return trieRoot.get(); }
#endif
	float getTransitionP(const KMorpheme* a, const KMorpheme* b) const;
	float getTransitionP(KPOSTag a, KPOSTag b) const;
	KPOSTag findMaxiumTag(const KMorpheme* a, const KMorpheme* c) const;
};

