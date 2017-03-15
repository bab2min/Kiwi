#pragma once
#include "Kiwi.h"

class KTest
{
public:
	typedef vector<KWordPair> AnswerType;
	struct TestResult
	{
		wstring q;
		AnswerType a;
		AnswerType r;
	};
protected:
	vector<TestResult> wrongList;
	size_t totalCount;
public:
	KTest(const char* testSetFile, Kiwi* kw);
	float getScore() const ;
	const vector<TestResult>& getWrongList() const { return wrongList; }
	size_t getTotalCount() const { return totalCount; }
};

