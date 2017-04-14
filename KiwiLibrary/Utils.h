#pragma once

#define LEN_ARRAY(p) (sizeof(p)/sizeof(p[0]))

string splitJamo(k_wstring hangul);
k_wstring joinJamo(string jm);
bool verifyHangul(k_wstring hangul);
void splitJamo(k_wchar hangul, string& ret);
void printJM(const char* c, size_t len = -1);
void printJM(const string& c);
void printJM(const KChunk& c, const char* p);

template<typename Iter>
string encodeJamo(Iter begin, Iter end) {
	string ret;
	for (; begin != end; ++begin)
	{
		assert(*begin > 0x3130 && *begin <= 0x3130 + 51);
		ret.push_back(*begin - 0x3130);
	}
	return ret;
}

template<typename Out>
void split(const k_wstring &s, k_wchar delim, Out result) {
	basic_stringstream<k_wchar> ss;
	ss.str(s);
	k_wstring item;
	while (getline(ss, item, delim)) {
		*(result++) = item;
	}
}

inline vector<k_wstring> split(const k_wstring &s, k_wchar delim) {
	vector<k_wstring> elems;
	split(s, delim, back_inserter(elems));
	return elems;
}


template<typename Out>
void split(const string &s, char delim, Out result) {
	stringstream ss;
	ss.str(s);
	string item;
	while (getline(ss, item, delim)) {
		*(result++) = item;
	}
}

inline vector<string> split(const string &s, char delim) {
	vector<string> elems;
	split(s, delim, back_inserter(elems));
	return elems;
}