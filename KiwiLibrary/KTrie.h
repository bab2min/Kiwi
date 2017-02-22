#pragma once

struct KForm;

struct KChunk
{
	union 
	{
		array<char, 16> str;
		struct
		{
			array<char, 8> _;
			const KForm* form;
		};
	};

	KChunk(const KForm* _form) : form(_form) { _.fill(0); }
	KChunk(const char* begin, const char* end)
	{
		if (end - begin < 16)
		{
			copy(begin, end, str.begin());
			str[end - begin] = 0;
		}
	}
	bool isStr() const { return str[0]; }
};

struct KTrie
{
#ifdef  _DEBUG
	static int rootID;
	int id;
#endif //  _DEBUG

	KTrie* next[51] = {nullptr,};
	KTrie* fail = nullptr;
	const KForm* exit = nullptr;
	KTrie();
	~KTrie();
	void build(const char* str, const KForm* form);
	KTrie* findFail(char i) const;
	void fillFail();
	vector<pair<const KForm*, int>> searchAllPatterns(const vector<char>& str) const;
	vector<vector<KChunk>> split(const vector<char>& str) const;
};

