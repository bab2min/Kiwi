#pragma once

#define LEN_ARRAY(p) (sizeof(p)/sizeof(p[0]))

struct KChunk;

k_string splitJamo(k_wstring hangul);
k_wstring joinJamo(k_string jm);
bool verifyHangul(k_wstring hangul);
void splitJamo(k_wchar hangul, k_string& ret);
void printJM(const char* c, size_t len = -1);
void printJM(const k_string& c);
void printJM(const KChunk& c, const char* p);

template<typename Iter>
k_string encodeJamo(Iter begin, Iter end) {
	k_string ret;
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
void split(const k_string &s, char delim, Out result) {
	k_stringstream ss;
	ss.str(s);
	k_string item;
	while (getline(ss, item, delim)) {
		*(result++) = item;
	}
}

inline vector<k_string> split(const k_string &s, char delim) {
	vector<k_string> elems;
	split(s, delim, back_inserter(elems));
	return elems;
}