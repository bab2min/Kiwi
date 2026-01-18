#pragma once
#include <kiwi/Kiwi.h>

class Evaluator
{
	virtual std::pair<double, double> eval(const std::string& output, const std::string& file, kiwi::Kiwi& kiwi, 
		kiwi::AnalyzeOption option, int repeat) = 0;
public:

	virtual ~Evaluator() = default;

	static std::unique_ptr<Evaluator> create(const std::string& evalType);

	int operator()(const std::string& modelPath, 
		const std::string& output, 
		const std::vector<std::string>& input,
		bool normCoda, bool zCoda, bool defaultDict, bool multiDict, kiwi::ModelType modelType,
		float typoCostWeight, bool bTypo, bool cTypo, bool lTypo,
		kiwi::Dialect allowedDialect,
		kiwi::Match oovScoringType,
		float unkFormScoreScale, float unkFormScoreBias,
		int repeat);
};

class MorphEvaluator : public Evaluator
{
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

	std::pair<double, double> eval(const std::string& output, const std::string& file, kiwi::Kiwi& kiwi, 
		kiwi::AnalyzeOption option, int repeat) override;

	std::vector<TestResult> loadTestset(const std::string& file) const;
	Score computeScore(std::vector<TestResult>& preds, std::vector<TestResult>& errors) const;
};

class DisambEvaluator : public Evaluator
{
	struct TestResult
	{
		std::u16string text;
		kiwi::TokenInfo target;
		kiwi::TokenResult result;
		float score = 0;
		void writeResult(std::ostream& out) const;
	};

	struct Score
	{
		double acc = 0;
		size_t totalCount = 0;
	};

	std::pair<double, double> eval(const std::string& output, const std::string& file, kiwi::Kiwi& kiwi, 
		kiwi::AnalyzeOption option, int repeat) override;

	std::vector<TestResult> loadTestset(const std::string& file) const;
	Score computeScore(std::vector<TestResult>& preds, std::vector<TestResult>& errors) const;
};

class NounEvaluator : public Evaluator
{
	struct TestResult
	{
		std::u16string text;
		std::vector<std::pair<std::u16string, std::string>> golds;
		kiwi::TokenResult result;
		size_t correct = 0, labeledCorrect = 0, numPreds = 0;
		size_t correctChr = 0, numPredsChr = 0;
		void writeResult(std::ostream& out) const;
	};

	struct Score
	{
		double precision = 0, recall = 0, f1 = 0, labeledRecall = 0;
		double precisionChr = 0, recallChr = 0, f1Chr = 0;
		size_t totalCount = 0;
	};

	std::pair<double, double> eval(const std::string& output, const std::string& file, kiwi::Kiwi& kiwi,
		kiwi::AnalyzeOption option, int repeat) override;

	std::vector<TestResult> loadTestset(const std::string& file) const;
	Score computeScore(std::vector<TestResult>& preds, std::vector<TestResult>& errors) const;
};
