#pragma once

#define LEN_ARRAY(p) (sizeof(p)/sizeof(p[0]))

struct KChunk;

k_string splitCoda(k_wstring hangul);
//k_string splitJamo(k_wstring hangul);
//k_wstring joinJamo(k_string jm);
bool verifyHangul(k_wstring hangul);
//void splitJamo(k_wchar hangul, k_string& ret);
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
	std::basic_stringstream<k_wchar> ss;
	ss.str(s);
	k_wstring item;
	while (std::getline(ss, item, delim)) {
		*(result++) = item;
	}
}

inline std::vector<k_wstring> split(const k_wstring &s, k_wchar delim) {
	std::vector<k_wstring> elems;
	split(s, delim, std::back_inserter(elems));
	return elems;
}


template<typename Out>
void split(const k_string &s, char delim, Out result) {
	k_stringstream ss;
	ss.str(s);
	k_string item;
	while (std::getline(ss, item, delim)) {
		*(result++) = item;
	}
}

inline std::vector<k_string> split(const k_string &s, char delim) {
	std::vector<k_string> elems;
	split(s, delim, std::back_inserter(elems));
	return elems;
}

template<class _Ty>
void writeToBinStream(std::ostream& os, const _Ty& v)
{
	os.write((const char*)&v, sizeof(_Ty));
}

template<class _Ty>
_Ty readFromBinStream(std::istream& is)
{
	_Ty v;
	is.read((char*)&v, sizeof(_Ty));
	return v;
}

template<class _Ty>
void readFromBinStream(std::istream& is, _Ty& v)
{
	is.read((char*)&v, sizeof(_Ty));
}

template<class _Ty1, class _Ty2>
void writeToBinStream(std::ostream& os, const std::pair<_Ty1, _Ty2>& v)
{
	writeToBinStream(os, v.first);
	writeToBinStream(os, v.second);
}

template<class _Ty1, class _Ty2>
std::pair<_Ty1, _Ty2> readFromBinStream(std::istream& is)
{
	return std::make_pair(readFromBinStream<_Ty1>(is), readFromBinStream<_Ty2>(is));
}


template<>
void writeToBinStream<k_string>(std::ostream& os, const k_string& v)
{
	writeToBinStream<uint32_t>(os, v.size());
	os.write((const char*)&v[0], v.size() * sizeof(k_string::value_type));
}

template<>
k_string readFromBinStream<k_string>(std::istream& is)
{
	k_string v; 
	v.resize(readFromBinStream<uint32_t>(is));
	is.read((char*)&v[0], v.size() * sizeof(k_string::value_type));
	return v;
}