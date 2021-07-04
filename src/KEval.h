#pragma once

class KEval
{
public:
	typedef std::vector<kiwi::TokenInfo> AnswerType;
	struct TestResult
	{
		std::u16string q;
		AnswerType a;
		AnswerType r;
		std::vector<kiwi::TokenInfo> dr, da;
		std::vector<kiwi::KResult> cands;
		float score;
		void writeResult(std::ostream& out) const;
	};
protected:
	std::vector<TestResult> wrongList;
	size_t totalCount;
	float totalScore;
public:
	KEval(const char* testSetFile, kiwi::Kiwi* kw, size_t topN = 3);
	float getScore() const ;
	const std::vector<TestResult>& getWrongList() const { return wrongList; }
	size_t getTotalCount() const { return totalCount; }
};

