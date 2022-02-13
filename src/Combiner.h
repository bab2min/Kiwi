#pragma once

#include <mapbox/variant.hpp>
#include <kiwi/Types.h>
#include <kiwi/TemplateUtils.hpp>
#include "bitset.hpp"

namespace kiwi
{
	namespace cmb
	{
		struct Hash
		{
			template<class Ty1, class Ty2>
			size_t operator()(const std::pair<Ty1, Ty2>& p) const
			{
				size_t hash = std::hash<Ty2>{}(p.second);
				hash ^= std::hash<Ty1>{}(p.first) + (hash << 6) + (hash >> 2);
				return hash;
			}

			template<class Ty1, class ...Rest>
			size_t operator()(const std::tuple<Ty1, Rest...>& p) const
			{
				size_t hash = operator()(kiwi::tp::tuple_tail(p));
				hash ^= std::hash<Ty1>{}(std::get<0>(p)) + (hash << 6) + (hash >> 2);
				return hash;
			}

			template<class Ty>
			size_t operator()(const std::tuple<Ty>& p) const
			{
				return std::hash<Ty>{}(std::get<0>(p));
			}

			template<class Ty>
			size_t operator()(const Vector<Ty>& p) const
			{
				size_t hash = p.size();
				for (auto& v : p)
				{
					hash ^= std::hash<Ty>{}(v) + (hash << 6) + (hash >> 2);
				}
				return hash;
			}
		};

		template<class ValueTy>
		class RaggedVector
		{
			Vector<ValueTy> data;
			Vector<size_t> ptrs;
		public:
			size_t size() const { return ptrs.size(); };

			auto operator[](size_t idx) const -> std::pair<decltype(data.begin()), decltype(data.begin())>
			{
				size_t b = idx < ptrs.size() ? ptrs[idx] : data.size();
				size_t e = idx + 1 < ptrs.size() ? ptrs[idx + 1] : data.size();
				return std::make_pair(data.begin() + b, data.begin() + e);
			}

			auto operator[](size_t idx) -> std::pair<decltype(data.begin()), decltype(data.begin())>
			{
				size_t b = idx < ptrs.size() ? ptrs[idx] : data.size();
				size_t e = idx + 1 < ptrs.size() ? ptrs[idx + 1] : data.size();
				return std::make_pair(data.begin() + b, data.begin() + e);
			}

			void emplace_back()
			{
				ptrs.emplace_back(data.size());
			}

			template<class... Args>
			void add_data(Args&&... args)
			{
				data.emplace_back(std::forward<Args>(args)...);
			}

			template<class It>
			void insert_data(It first, It last)
			{
				data.insert(data.end(), first, last);
			}
		};

		struct Replacement
		{
			Vector<KString> repl;
			CondVowel leftVowel;
			CondPolarity leftPolarity;

			Replacement(Vector<KString> _repl = {},
				CondVowel _leftVowel = CondVowel::none,
				CondPolarity _leftPolar = CondPolarity::none
			) : repl{ _repl }, leftVowel{ _leftVowel }, leftPolarity{ _leftPolar }
			{}
		};

		template<class NodeSizeTy, class GroupSizeTy>
		class MultiRuleDFA
		{
			friend class RuleSet;

			Vector<char16_t> vocabs;
			Vector<NodeSizeTy> transition;
			Vector<GroupSizeTy> finishGroup;
			Vector<utils::Bitset> groupInfo;
			Vector<Replacement> finish;
		public:
			std::vector<KString> combine(const KString& left, const KString& right) const;
		};

		namespace detail
		{
			template<typename ... Ty>
			using TupleCat = decltype(std::tuple_cat(std::declval<Ty>()...));

			template<class, class>
			struct MultiRuleDFAUnpack2nd;

			template<class Ty, class... Args>
			struct MultiRuleDFAUnpack2nd<Ty, std::tuple<Args...>>
			{
				using type = std::tuple<MultiRuleDFA<Ty, Args>...>;
			};

			template<class, class>
			struct MultiRuleDFAUnpack;

			template<class... NodeSizeTy, class... GroupSizeTy>
			struct MultiRuleDFAUnpack<std::tuple<NodeSizeTy...>, std::tuple<GroupSizeTy...>>
			{
				using type = TupleCat<typename MultiRuleDFAUnpack2nd<NodeSizeTy, std::tuple<GroupSizeTy...>>::type...>;
			};

			template<class>
			struct VariantFromTuple;

			template<class... Args>
			struct VariantFromTuple<std::tuple<Args...>>
			{
				using type = mapbox::util::variant<Args...>;
			};
		}

		using MultiRuleDFAErased = typename detail::VariantFromTuple<
			typename detail::MultiRuleDFAUnpack<
				std::tuple<uint8_t, uint16_t, uint32_t, uint64_t>, 
				std::tuple<uint8_t, uint16_t, uint32_t, uint64_t>
			>::type
		>::type;

		struct ChrSet
		{
			bool negation = false;
			bool skippable = false;
			Vector<std::pair<char16_t, char16_t>> ranges;

			ChrSet();
			ChrSet(const ChrSet&);
			ChrSet(ChrSet&&) noexcept;
			~ChrSet();
			ChrSet& operator=(const ChrSet&);
			ChrSet& operator=(ChrSet&&) noexcept;

			ChrSet(char16_t chr);
		};

		class Pattern
		{
			friend class RuleSet;
			template<class NodeSizeTy, class GroupSizeTy> friend class MultiRuleDFA;

			enum {
				ruleSep = 0,
				bos = 1,
				eos = 2,
			};

			struct Node
			{
				UnorderedMap<ptrdiff_t, ChrSet> next;
				Vector<ptrdiff_t> getEpsilonTransition(ptrdiff_t thisOffset) const;

				RaggedVector<ptrdiff_t> getTransitions(
					const Vector<char16_t>& vocabs, 
					const Vector<Vector<ptrdiff_t>>& epsilonTable, 
					ptrdiff_t thisOffset
				) const;
			private:
				void getEpsilonTransition(ptrdiff_t thisOffset, Vector<ptrdiff_t>& ret) const;
			};

			Vector<Node> nodes;
		public:
			Pattern();
			Pattern(const Pattern&);
			Pattern(Pattern&&) noexcept;
			~Pattern();
			Pattern& operator=(const Pattern&);
			Pattern& operator=(Pattern&&) noexcept;

			Pattern(const KString& expr);
		};

		class CompiledRule
		{
			friend class RuleSet;
			Vector<MultiRuleDFAErased> dfa;
			UnorderedMap<std::tuple<POSTag, POSTag, uint8_t>, size_t, Hash> map;
		public:
			CompiledRule();
			CompiledRule(const CompiledRule&);
			CompiledRule(CompiledRule&&) noexcept;
			~CompiledRule();
			CompiledRule& operator=(const CompiledRule&);
			CompiledRule& operator=(CompiledRule&&) noexcept;

			std::vector<std::u16string> combine(
				const std::u16string& leftForm, POSTag leftTag, 
				const std::u16string& rightForm, POSTag rightTag,
				CondVowel cv = CondVowel::none, CondPolarity cp = CondPolarity::none
			) const;
		};

		class RuleSet
		{
			struct Rule
			{
				Pattern left, right;
				Replacement repl;

				Rule(const KString& _left = {},
					const KString& _right = {},
					const Vector<KString>& _results = {},
					CondVowel _leftVowel = CondVowel::none,
					CondPolarity _leftPolar = CondPolarity::none)
					: left{ _left }, right { _right }, repl{ _results, _leftVowel, _leftPolar }
				{
				}
			};

			UnorderedMap<std::tuple<POSTag, POSTag, uint8_t>, Vector<size_t>, Hash> ruleset;
			Vector<Rule> rules;

			static Vector<char16_t> getVocabList(const Vector<Pattern::Node>& nodes);
			
			template<class GroupSizeTy>
			static MultiRuleDFAErased buildRules(const Vector<Rule>& rules);
			
			static MultiRuleDFAErased buildRules(const Vector<Rule>& rules);
		public:
			RuleSet();
			RuleSet(const RuleSet&);
			RuleSet(RuleSet&&) noexcept;
			~RuleSet();
			RuleSet& operator=(const RuleSet&);
			RuleSet& operator=(RuleSet&&) noexcept;

			void addRule(const std::string& lTag, const std::string& rTag,
				const KString& lPat, const KString& rPat, const std::vector<KString>& results,
				CondVowel leftVowel, CondPolarity leftPolar
			);
			
			void loadRules(std::istream& istr);

			CompiledRule compile() const;
		};
	}
}
