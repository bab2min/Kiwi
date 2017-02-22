#pragma once
class KModelMgr
{
public:
	struct Model
	{
		string str;
		char tag[4];
		size_t freq;
		float vowel;
		float vocalic;
		float vocalicH;
		float positive;
		unordered_map<char[4], float> condP;
	};

	struct CombineRule
	{
		string a;
		string b;
		string result;
	};

	struct CombineRules
	{
		vector<string> tagA;
		vector<string> tagB;
		vector<CombineRule> rules;
	};

protected:

	vector<Model> mMdls;
	vector<CombineRules> mRules;
public:
	void loadModelFromTxt(const char* filename);
	void loadMergeRule(const char* filename);
};

