#pragma once

#define LEN_ARRAY(p) (sizeof(p)/sizeof(p[0]))

string splitJamo(wstring hangul);
wstring joinJamo(string jm);
void splitJamo(wchar_t hangul, string& ret);
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
void split(const wstring &s, wchar_t delim, Out result) {
	wstringstream ss;
	ss.str(s);
	wstring item;
	while (getline(ss, item, delim)) {
		*(result++) = item;
	}
}

inline vector<wstring> split(const wstring &s, wchar_t delim) {
	vector<wstring> elems;
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

class Timer
{
public:
	chrono::steady_clock::time_point point;
	Timer()
	{
		reset();
	}
	void reset()
	{
		point = chrono::high_resolution_clock::now();
	}

	double getElapsed() const
	{
		return chrono::duration <double, milli>(chrono::high_resolution_clock::now() - point).count();
	}
};