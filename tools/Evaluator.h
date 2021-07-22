#pragma once
#include <kiwi/Kiwi.h>

class Evaluator
{
public:
	using AnswerType = std::vector<kiwi::TokenInfo>;
	struct TestResult
	{
		std::u16string q;
		AnswerType a;
		AnswerType r;
		std::vector<kiwi::TokenInfo> dr, da;
		std::vector<kiwi::TokenResult> cands;
		float score;
		void writeResult(std::ostream& out) const;
	};
protected:
	std::vector<TestResult> errors;
	size_t totalCount = 0;
	double totalScore = 0;
	size_t microCount = 0;
	size_t microCorrect = 0;
public:
	Evaluator(const std::string& testSetFile, kiwi::Kiwi* kw, size_t topN = 3);
	double getMacroScore() const;
	double getMicroScore() const;
	const std::vector<TestResult>& getErrors() const { return errors; }
	size_t getTotalCount() const { return totalCount; }
};

