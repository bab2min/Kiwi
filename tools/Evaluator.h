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
	
	struct Score
	{
		double micro = 0;
		double macro = 0;
		size_t totalCount = 0;
	};

private:
	const kiwi::Kiwi* kw = nullptr;
	size_t topN = 3;
	std::vector<TestResult> testsets, errors;
public:
	Evaluator(const std::string& testSetFile, const kiwi::Kiwi* _kw, size_t topN = 3);
	void run();
	Score evaluate();
	const std::vector<TestResult>& getErrors() const { return errors; }
};

