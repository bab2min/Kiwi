#include "stdafx.h"
#include "KModelMgr.h"
#include "Utils.h"

void KModelMgr::loadModelFromTxt(const char * filename)
{
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	int noForm = 0;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		auto fields = split(wstr, '\t');
		if (fields.size() < 8) continue;

		auto form = encodeJamo(fields[0].begin(), fields[0].end());
		vector<char> tag;
		for (auto c : fields[1]) tag.push_back(c);
		size_t freq = _wtoi(fields[2].c_str());
		float vowel = _wtof(fields[3].c_str());
		float vocalic = _wtof(fields[4].c_str());
		float vocalicH = _wtof(fields[5].c_str());
		float positive = _wtof(fields[6].c_str());
		
	}
	fclose(file);
}

void KModelMgr::loadMergeRule(const char * filename)
{
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw exception();
	char buf[2048];
	int noForm = 0;
	while (fgets(buf, 2048, file))
	{
		auto fields = split(buf, '\t');
		if (fields.size() < 3) continue;

		auto a = fields[0];
		auto b = fields[1];
		float p = atof(fields[2].c_str());
	}
	fclose(file);
}
