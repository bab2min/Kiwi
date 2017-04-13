#pragma once

class KTest
{
public:
	typedef vector<KWordPair> AnswerType;
	struct TestResult
	{
		wstring q;
		AnswerType a;
		AnswerType r;
		vector<KResult> cands;
		float score;
		void writeResult(FILE* output) const;
	};
protected:
	vector<TestResult> wrongList;
	size_t totalCount;
	float totalScore;
public:
	KTest(const char* testSetFile, Kiwi* kw, size_t topN = 3);
	float getScore() const ;
	const vector<TestResult>& getWrongList() const { return wrongList; }
	size_t getTotalCount() const { return totalCount; }
};

