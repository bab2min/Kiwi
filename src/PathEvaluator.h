#pragma once
#include <kiwi/Kiwi.h>
#include "UnkFormScorer.h"

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

		explicit SpecialState(uint8_t val)
		{
			reinterpret_cast<uint8_t&>(*this) = val;
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

	struct PackedState
	{
		uint32_t data = 0;
		PackedState() = default;
		PackedState(SpecialState state, uint32_t oovCntArenaPtr = 0)
		{
			data = (oovCntArenaPtr << 8) | (uint8_t)state;
		}

		SpecialState specialState() const
		{
			return (SpecialState)(uint8_t)(data & 0xFF);
		}

		uint32_t oovCntArenaPtr() const
		{
			return data >> 8;
		}

		void setSpecialState(SpecialState state)
		{
			data = (data & 0xFFFFFF00) | (uint8_t)state;
		}

		void setOovCntArenaPtr(uint32_t ptr)
		{
			data = (data & 0xFF) | (ptr << 8);
		}

		bool operator<(const PackedState& o) const
		{
			return data < o.data;
		}

		bool operator==(const PackedState& o) const
		{
			return data == o.data;
		}
	};

	template<>
	struct Hash<PackedState>
	{
		size_t operator()(const PackedState& s) const
		{
			return std::hash<uint32_t>{}(s.data);
		}
	};

	struct PathResult
	{
		Path path;
		float score = 0;
		PackedState prevState;
		PackedState curState;

		PathResult(Path&& _path = {}, float _score = 0, PackedState _prevState = {}, PackedState _curState = {})
			: path{ move(_path) }, score{ _score }, prevState{ _prevState }, curState{ _curState }
		{
			sizeof(PathResult);
		}

		PathResult(const Path& _path, float _score = 0, PackedState _prevState = {}, PackedState _curState = {})
			: path{ _path }, score{ _score }, prevState{ _prevState }, curState{ _curState }
		{
		}
	};

	class OovOrForm : public U16StringView
	{
	public:
		explicit OovOrForm(const char16_t* str, size_t len) : U16StringView{ len ? str : nullptr, len } {}
		OovOrForm(U16StringView str) : OovOrForm{ str.data(), str.size() } {}
		explicit OovOrForm(const Form* form) : U16StringView{ reinterpret_cast<const char16_t*>(form), 0 } {}

		const Form* asForm() const
		{
			if (size() > 0) return nullptr;
			return reinterpret_cast<const Form*>(data());
		}

		U16StringView asOov() const
		{
			return *this;
		}

		bool operator==(const OovOrForm& o) const
		{
			const Form* form1 = asForm();
			const Form* form2 = o.asForm();
			if (form1 && form2)
			{
				return form1 == form2;
			}
			else if (!form1 && !form2)
			{
				return asOov() == o.asOov();
			}
			else
			{
				return false;
			}
		}
	};

	template<>
	struct Hash<OovOrForm>
	{
		size_t operator()(const OovOrForm& o) const
		{
			const Form* form = o.asForm();
			if (form)
			{
				return Hash<const Form*>{}(form);
			}
			else
			{
				return Hash<U16StringView>{}(o.asOov());
			}
		}
	};

	struct FindBestPathArgs
	{
		const Kiwi* kw;
		const KiwiConfig& config;
		const Vector<PackedState>& prevSpStates;
		const KString& normForm;
		const KGraphNode* graph;
		size_t graphSize;
		size_t topN;
		size_t oovScoringType;
		UnorderedMap<U16StringView, size_t>* oovTotalMap;
		Vector<uint8_t>* oovTotalCnt;
		UnorderedMap<OovOrForm, Vector<uint16_t>>* oovPrefixLists;
		const std::vector<TokenResult>* prevResults = nullptr;
		bool openEnding;
		bool splitComplex = false;
		bool splitSaisiot = false;
		bool mergeSaisiot = false;
		const std::unordered_set<const Morpheme*>* blocklist = nullptr;
		Dialect allowedDialects = Dialect::standard;
		float dialectCost = 0.f;
		const SubstringCounter* substringCounter = nullptr;
	};

	class OovUnigramScorer
	{
		const UnorderedMap<U16StringView, size_t>* oovTotalMap = nullptr;
		const Vector<uint8_t>* oovTotalCnt = nullptr;
		const KGraphNode* graph = nullptr;
		const uint32_t* oovCands = nullptr;
		size_t oovCandSize = 0;
		float smoothness = 0;

	public:
		OovUnigramScorer(
			const UnorderedMap<U16StringView, size_t>* _oovTotalMap,
			const Vector<uint8_t>* _oovTotalCnt,
			const KGraphNode* _graph,
			const uint32_t* _oovCands,
			size_t _oovCandSize,
			float _smoothness
		)
			: oovTotalMap{ _oovTotalMap }, oovTotalCnt{ _oovTotalCnt }, graph{ _graph }, oovCands{ _oovCands }, oovCandSize{ _oovCandSize }, smoothness{ _smoothness }
		{
		}

		bool empty() const
		{
			return oovCandSize == 0;
		}

		float score(uint32_t cntArenaPtr, uint32_t nodeIdx) const;
	};

	template<class LangModel>
	struct BestPathFinder : public FindBestPathArgs
	{
		using LmState = typename LangModel::LmStateType;

		size_t insertOovPrefices(size_t targetNodeIdx, size_t oovIdx);

		template<class WordLL, class Func>
		void traverseNodesWithEndPos(
			Vector<WordLL>& pathes,
			const Vector<size_t>& pathIndices,
			size_t targetNodeIdx,
			Func&& func
		) const;

		template<class WordLL>
		void updateOovTotalMap(
			Vector<WordLL>& pathes,
			Vector<size_t>& pathIndices,
			size_t prevOovIdx, size_t bit, size_t i = -1);

		template<class WordLL>
		void updatePrefixCnts(
			Vector<WordLL>& pathes,
			Vector<size_t>& pathIndices,
			size_t nodeIdx,
			const Vector<uint32_t>& currentOovNodeIdcs);

		void findOovNodes(
			size_t nodeIdx,
			Vector<uint32_t>& oovNodeIdcs
		) const;

		template<bool useOovTotalConsistency>
		Vector<PathResult> findBestPathDispatched();

		static Vector<PathResult> findBestPath(const FindBestPathArgs& args)
		{
			BestPathFinder<LangModel> finder{ args };
			if (args.oovTotalMap)
			{
				return finder.findBestPathDispatched<true>();
			}
			else
			{
				return finder.findBestPathDispatched<false>();
			}
		}
	};

	using FnFindBestPath = Vector<PathResult>(*)(const FindBestPathArgs&);
}
