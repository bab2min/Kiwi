#pragma once

#include <kiwi/Types.h>

namespace kiwi
{
	struct WordInfo
	{
		std::u16string form;
		float score, lBranch, rBranch, lCohesion, rCohesion;
		uint32_t freq;
		std::map<POSTag, float> posScore;

		WordInfo(std::u16string _form = {},
			float _score = 0, float _lBranch = 0, float _rBranch = 0,
			float _lCohesion = 0, float _rCohesion = 0, uint32_t _freq = 0,
			std::map<POSTag, float>&& _posScore = {})
			: form(_form), score(_score), lBranch(_lBranch), rBranch(_rBranch),
			lCohesion(_lCohesion), rCohesion(_rCohesion), freq(_freq), posScore(_posScore)
		{}
	};

	class WordDetector
	{
		struct Counter;
	protected:
		size_t numThreads = 0;
		std::map<std::pair<POSTag, bool>, std::map<char16_t, float>> posScore;
		std::map<std::u16string, float> nounTailScore;

		void loadPOSModelFromTxt(std::istream& is);
		void loadNounTailModelFromTxt(std::istream& is);

		void countUnigram(Counter&, const U16Reader& reader, size_t minCnt) const;
		void countBigram(Counter&, const U16Reader& reader, size_t minCnt) const;
		void countNgram(Counter&, const U16Reader& reader, size_t minCnt, size_t maxWordLen) const;
		float branchingEntropy(const std::map<std::u16string, uint32_t>& cnt, std::map<std::u16string, uint32_t>::iterator it, size_t minCnt, float defaultPerp = 1.f) const;
		std::map<POSTag, float> getPosScore(Counter&, const std::map<std::u16string, uint32_t>& cnt, std::map<std::u16string, uint32_t>::iterator it, bool coda, const std::u16string& realForm) const;
	public:

		struct FromRawData {};
		static constexpr FromRawData fromRawDataTag = {};

		WordDetector() = default;
		WordDetector(const std::string& modelPath, size_t _numThreads = 0);
		WordDetector(FromRawData, const std::string& modelPath, size_t _numThreads = 0);

		bool ready() const
		{
			return !posScore.empty();
		}

		void saveModel(const std::string& modelPath) const;
		std::vector<WordInfo> extractWords(const U16MultipleReader& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.1f) const;
	};

}

