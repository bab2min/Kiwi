#pragma once

#include <string>
#include <vector>
#include <functional>

using namespace std;

class KEndingMaker
{
public:
	typedef pair<wstring, function<bool(wchar_t,bool)>> FormCond;
protected:
	vector<FormCond> vMorphList[7];
	enum {HONOR, TENSE, REAL, FORMAL, SMOOD, PMOOD, POLITE};
public:
	KEndingMaker();
};

