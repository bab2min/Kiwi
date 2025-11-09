#pragma once
#include <kiwi/Kiwi.h>

namespace kiwi
{
	struct SpecialState
	{
		uint8_t singleQuote : 1;
		uint8_t doubleQuote : 1;
		uint8_t bulletHash : 6;

		SpecialState() : singleQuote{ 0 }, doubleQuote{ 0 }, bulletHash{ 0 }
		{
		}

		operator uint8_t() const
		{
			return reinterpret_cast<const uint8_t&>(*this);
		}

		bool operator<(const SpecialState& o) const
		{
			return (uint8_t)(*this) < (uint8_t)o;
		}

		bool operator==(const SpecialState& o) const
		{
			return (uint8_t)(*this) == (uint8_t)o;
		}
	};

	struct PathNode
	{
		const Morpheme* morph = nullptr;
		KString str;
		uint32_t begin = 0, end = 0;
		float wordScore = 0, typoCost = 0, dialectCost = 0;
		uint32_t typoFormId = 0;
		uint32_t nodeId = 0;

		PathNode(const Morpheme* _morph = nullptr,
			const KString& _str = {},
			uint32_t _begin = 0,
			uint32_t _end = 0,
			float _wordScore = 0,
			float _typoCost = 0,
			float _dialectCost = 0,
			uint32_t _typoFormId = 0,
			uint32_t _nodeId = 0
		)
			: morph{ _morph }, str{ _str }, begin{ _begin }, end{ _end },
			wordScore{ _wordScore }, typoCost{ _typoCost }, dialectCost{ _dialectCost },
			typoFormId{ _typoFormId }, nodeId{ _nodeId }
		{
		}

		bool operator==(const PathNode& o) const
		{
			return morph == o.morph
				&& str == o.str
				&& begin == o.begin
				&& end == o.end
				&& wordScore == o.wordScore
				&& typoCost == o.typoCost
				&& dialectCost == o.dialectCost
				&& typoFormId == o.typoFormId;
		}
	};
	using Path = Vector<PathNode>;

	struct PathResult
	{
		Path path;
		float score = 0;
		SpecialState prevState;
		SpecialState curState;

		PathResult(Path&& _path = {}, float _score = 0, SpecialState _prevState = {}, SpecialState _curState = {})
			: path{ move(_path) }, score{ _score }, prevState{ _prevState }, curState{ _curState }
		{
		}

		PathResult(const Path& _path, float _score = 0, SpecialState _prevState = {}, SpecialState _curState = {})
			: path{ _path }, score{ _score }, prevState{ _prevState }, curState{ _curState }
		{
		}
	};

	template<class LangModel>
	struct BestPathFinder
	{
		static Vector<PathResult> findBestPath(const Kiwi* kw,
			const KiwiConfig& config,
			const Vector<SpecialState>& prevSpStates,
			const KString& normForm,
			const KGraphNode* graph,
			const size_t graphSize,
			const size_t topN,
			bool openEnding,
			bool splitComplex = false,
			bool splitSaisiot = false,
			bool mergeSaisiot = false,
			const std::unordered_set<const Morpheme*>* blocklist = nullptr,
			Dialect allowedDialects = Dialect::standard,
			float dialectCost = 0.f
		);
	};

	using FnFindBestPath = decltype(&BestPathFinder<void>::findBestPath);
}
