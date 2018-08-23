#pragma once

#include "BakedMap.hpp"

class KNLangModel
{
public:
	typedef uint32_t WID;
	struct Node
	{
		class NodeIterator
		{
		protected:
			const Node * home;
			std::map<WID, int32_t>::const_iterator mBegin;
		public:
			NodeIterator(const Node* _home, const std::map<WID, int32_t>::const_iterator& _mBegin)
				: home(_home), mBegin(_mBegin)
			{
			}

			NodeIterator& operator++()
			{
				++mBegin;
				return *this;
			}
			bool operator==(const NodeIterator& o) const
			{
				return mBegin == o.mBegin;
			}
			bool operator!=(const NodeIterator& o) const
			{
				return !operator==(o);
			}
			std::pair<WID, const Node*> operator*() const
			{
				return std::make_pair(mBegin->first, home + mBegin->second);
			}
		};
	protected:
		typedef std::function<Node*()> Allocator;
		union
		{
			std::map<WID, int32_t> next;
			BakedMap<WID, int32_t> bakedNext;
		};
	public:
		uint8_t depth = 0;
	protected:
		bool baked = false;
	public:
		int32_t parent = 0, lower = 0;
		union
		{
			uint32_t count = 0;
			float ll;
		};
		float gamma = 0;

		Node(bool _baked = false) : baked(_baked)
		{
			if (baked) new (&bakedNext) BakedMap<WID, int32_t>();
			else new (&next) std::map<WID, int32_t>();
		}

		Node(Node&& o)
		{
			if (o.baked) new (&bakedNext) BakedMap<WID, int32_t>(std::move(o.bakedNext));
			else new (&next) std::map<WID, int32_t>(std::move(o.next));

			baked = o.baked;
			std::swap(parent, o.parent);
			std::swap(lower, o.lower);
			std::swap(depth, o.depth);
			std::swap(count, o.count);
			std::swap(gamma, o.gamma);
		}

		~Node()
		{
			if (baked) bakedNext.~BakedMap();
			else next.~map();
		}

		Node* getParent() const
		{
			if (!parent) return nullptr;
			return (Node*)this + parent;
		}

		Node* getLower() const
		{
			if (!lower) return nullptr;
			return (Node*)this + lower;
		}

		inline Node* getNext(WID n) const
		{
			auto it = next.find(n);
			if (it == next.end()) return nullptr;
			return (Node*)this + it->second;
		}

		inline Node* getNextFromBaked(WID n) const
		{
			auto t = bakedNext[n];
			if (!t) return nullptr;
			return (Node*)this + t;
		}

		template<typename It>
		Node* getFromBaked(It begin, It end) const
		{
			if (begin == end) return (Node*)this;
			auto nextNode = getNextFromBaked(*begin);
			if (!nextNode) return nullptr;
			return nextNode->getFromBaked(begin + 1, end);
		}

		float getLL(WID n, size_t endOrder) const
		{
			if (depth == endOrder)
			{
				union { int32_t t; float u; };
				t = bakedNext[n];
				if (t) return u;
			}
			else
			{
				auto* p = getNextFromBaked(n);
				if (p) return p->ll;
			}
			auto* lower = getLower();
			if (!lower) return -100;
			return gamma + lower->getLL(n, endOrder);
		}

		Node* addNextNode(WID n, const Allocator& alloc)
		{
			Node* nextNode = alloc();
			nextNode->depth = depth + 1;
			nextNode->parent = this - nextNode;
			next[n] = nextNode - this;
			if (depth)
			{
				auto* nn = getLower()->getNext(n);
				if (!nn) nn = getLower()->addNextNode(n, alloc);
				nextNode->lower = nn - nextNode;
			}
			else nextNode->lower = nextNode->parent;
			return nextNode;
		}

		template<typename It>
		void increaseCount(It historyBegin, It historyEnd, size_t endOrder, const Allocator& alloc)
		{
			++count;
			if (historyBegin == historyEnd) return;
			if (depth == endOrder)
			{
				next[*historyBegin]++;
				return;
			}
			Node* nextNode = getNext(*historyBegin);
			if (!nextNode) nextNode = addNextNode(*historyBegin, alloc);
			nextNode->increaseCount(historyBegin + 1, historyEnd, endOrder, alloc);
		}

		void optimize()
		{
			std::map<WID, int32_t> tNext = move(next);
			bakedNext = BakedMap<WID, int32_t>{ tNext.begin(), tNext.end() };
			baked = true;
		}

		inline void setLL(WID n, float ll)
		{
			next[n] = *(int32_t*)&ll;
		}

		NodeIterator begin() const
		{
			return { this, next.begin() };
		}
		NodeIterator end() const
		{
			return { this, next.end() };
		}

		void writeToStream(std::ostream& str) const
		{
			str.write((const char*)&parent, sizeof(uint32_t));
			str.write((const char*)&lower, sizeof(uint32_t));
			str.write((const char*)&depth, sizeof(uint32_t));
			str.write((const char*)&ll, sizeof(uint32_t));
			str.write((const char*)&gamma, sizeof(uint32_t));

			uint32_t size = next.size();
			str.write((const char*)&size, sizeof(uint32_t));
			for (auto& p : bakedNext)
			{
				str.write((const char*)&p.first, sizeof(uint32_t));
				str.write((const char*)&p.second, sizeof(uint32_t));
			}
		}

		static Node readFromStream(std::istream& str)
		{
			Node n(true);
			str.read((char*)&n.parent, sizeof(uint32_t));
			str.read((char*)&n.lower, sizeof(uint32_t));
			str.read((char*)&n.depth, sizeof(uint32_t));
			str.read((char*)&n.ll, sizeof(uint32_t));
			str.read((char*)&n.gamma, sizeof(uint32_t));

			uint32_t size;
			str.read((char*)&size, sizeof(uint32_t));
			std::vector<std::pair<WID, int32_t>> tNext;
			tNext.reserve(size);
			for (size_t i = 0; i < size; ++i)
			{
				uint32_t first;
				int32_t second;
				str.read((char*)&first, sizeof(uint32_t));
				str.read((char*)&second, sizeof(uint32_t));
				tNext.emplace_back(first, second);
			}
			n.bakedNext = BakedMap<WID, int32_t>{ tNext.begin(), tNext.end() };
			return n;
		}
	};
protected:

	const std::function<Node*()> nodesAlloc = [this]()
	{
		nodes.emplace_back();
		return &nodes.back();
	};

	std::vector<Node> nodes;
	size_t orderN;
	size_t vocabSize = 0;

	void prepareCapacity(size_t minFreeSize);
	void calcDiscountedValue(size_t order, const std::vector<uint32_t>& cntNodes);
public:
	KNLangModel(size_t _orderN = 3);
	KNLangModel(KNLangModel&& o)
	{
		nodes.swap(o.nodes);
		orderN = o.orderN;
		vocabSize = o.vocabSize;
	}
	void trainSequence(const WID* seq, size_t len);
	void optimize();
	std::vector<float> predictNext(const WID* history, size_t len) const;
	float evaluateLL(const WID* seq, size_t len) const;
	float evaluateLLSent(const WID* seq, size_t len) const;

	void writeToStream(std::ostream&& str) const
	{
		str.write((const char*)&orderN, sizeof(uint32_t));
		str.write((const char*)&vocabSize, sizeof(uint32_t));

		uint32_t size = nodes.size();
		str.write((const char*)&size, sizeof(uint32_t));
		for (auto& p : nodes)
		{
			p.writeToStream(str);
		}
	}

	KNLangModel& operator=(KNLangModel&& o)
	{
		nodes.swap(o.nodes);
		orderN = o.orderN;
		vocabSize = o.vocabSize;
		return *this;
	}

	static KNLangModel readFromStream(std::istream&& str)
	{
		str.exceptions(std::istream::failbit | std::istream::badbit);
		KNLangModel n;
		n.nodes.clear();
		str.read((char*)&n.orderN, sizeof(uint32_t));
		str.read((char*)&n.vocabSize, sizeof(uint32_t));

		uint32_t size;
		str.read((char*)&size, sizeof(uint32_t));
		n.nodes.reserve(size);
		for (size_t i = 0; i < size; ++i)
		{
			n.nodes.emplace_back(Node::readFromStream(str));
		}
		return n;
	}
};
