#pragma once

#include <mapbox/variant.hpp>
#include <kiwi/Types.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Joiner.h>
#include "string_view.hpp"
#include "bitset.hpp"

namespace kiwi
{
	class KiwiBuilder;

	template<class Ty> class RaggedVector;

	namespace cmb
	{
		struct ReplString
		{
			KString str;
			size_t leftEnd;
			size_t rightBegin;
			float score;

			ReplString(const KString& _str = {}, size_t _leftEnd = -1, size_t _rightBegin = 0, float _score = 0)
				: str{ _str }, leftEnd{ std::min(_leftEnd, str.size()) }, rightBegin{ _rightBegin }, score{ _score }
			{
			}
		};

		struct Replacement
		{
			Vector<ReplString> repl;
			CondVowel leftVowel;
			CondPolarity leftPolarity;
			bool ignoreRCond;

			Replacement(Vector<ReplString> _repl = {},
				CondVowel _leftVowel = CondVowel::none,
				CondPolarity _leftPolar = CondPolarity::none,
				bool _ignoreRCond = false
			) : repl{ _repl }, leftVowel{ _leftVowel }, leftPolarity{ _leftPolar }, ignoreRCond{ _ignoreRCond }
			{}
		};

		struct Result
		{
			KString str;
			size_t leftEnd;
			size_t rightBegin;
			CondVowel vowel;
			CondPolarity polar;
			bool ignoreRCond;
			float score;

			Result(const KString& _str = {},
				size_t _leftEnd = 0,
				size_t _rightBegin = 0,
				CondVowel _vowel = CondVowel::none,
				CondPolarity _polar = CondPolarity::none,
				bool _ignoreRCond = false,
				float _score = 0
			) : str{ _str }, leftEnd{ _leftEnd }, rightBegin{ _rightBegin }, vowel{ _vowel }, polar{ _polar }, ignoreRCond{ _ignoreRCond }, score{_score}
			{
			}
		};

		template<class NodeSizeTy, class GroupSizeTy>
		class MultiRuleDFA
		{
			friend class RuleSet;

			template<class _NodeSizeTy, class _GroupSizeTy>
			friend class MultiRuleDFA;

			Vector<char16_t> vocabs;
			Vector<NodeSizeTy> transition;
			Vector<GroupSizeTy> finishGroup;
			Vector<GroupSizeTy> sepGroupFlatten;
			Vector<NodeSizeTy> sepGroupPtrs;
			Vector<utils::Bitset> groupInfo;
			Vector<Replacement> finish;

			template<class _NodeSizeTy>
			static MultiRuleDFA fromOther(MultiRuleDFA<_NodeSizeTy, GroupSizeTy>&& o);

		public:
			Vector<Result> combine(U16StringView left, U16StringView right) const;
			Vector<std::tuple<size_t, size_t, CondPolarity>> searchLeftPat(U16StringView left, bool matchRuleSep = true) const;
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
			ChrSet& operator=(ChrSet&&);

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
			Pattern& operator=(Pattern&&);

			Pattern(const KString& expr);
		};

		class CompiledRule
		{
			friend class RuleSet;
			friend class kiwi::KiwiBuilder;
			friend class Joiner;

			struct Allomorph
			{
				KString form;
				CondVowel cvowel;
				uint8_t priority;

				Allomorph(const KString& _form = {}, CondVowel _cvowel = CondVowel::none, uint8_t _priority = 0)
					: form{ _form }, cvowel{ _cvowel }, priority{ _priority }
				{
				}
			};

			Vector<MultiRuleDFAErased> dfa, dfaRight;
			UnorderedMap<std::tuple<POSTag, POSTag, uint8_t>, size_t> map;
			Vector<Allomorph> allomorphData;
			UnorderedMap<std::pair<KString, POSTag>, std::pair<size_t, size_t>> allomorphPtrMap;

			auto findRule(POSTag leftTag, POSTag rightTag,
				CondVowel cv = CondVowel::none, CondPolarity cp = CondPolarity::none
			) const -> decltype(map.end());

			Vector<KString> combineImpl(
				U16StringView leftForm, POSTag leftTag,
				U16StringView rightForm, POSTag rightTag,
				CondVowel cv = CondVowel::none, CondPolarity cp = CondPolarity::none
			) const;

			/**
			* @return tuple(combinedForm, leftFormBoundary, rightFormBoundary)
			*/
			std::tuple<KString, size_t, size_t> combineOneImpl(
				U16StringView leftForm, POSTag leftTag,
				U16StringView rightForm, POSTag rightTag,
				CondVowel cv = CondVowel::none, CondPolarity cp = CondPolarity::none
			) const;

			/**
			 * @return vector of tuple(replaceGroupId, capturedStartPos, replaceGroupCondition)
			 */
			Vector<std::tuple<size_t, size_t, CondPolarity>> testLeftPattern(
				U16StringView leftForm, size_t ruleId
			) const;

			/**
			 * @return vector of tuple(replaceGroupId, capturedEndPos, replaceGroupCondition)
			 */
			Vector<std::tuple<size_t, size_t, CondPolarity>> testRightPattern(
				U16StringView rightForm, size_t ruleId
			) const;

			UnorderedMap<std::tuple<POSTag, uint8_t>, Vector<size_t>> getRuleIdsByLeftTag() const;
			UnorderedMap<POSTag, Vector<size_t>> getRuleIdsByRightTag() const;

			Vector<Result> combine(U16StringView leftForm, U16StringView rightForm, size_t ruleId) const;

			template<class FormsTy>
			void addAllomorphImpl(const FormsTy& forms, POSTag tag);

			static uint8_t toFeature(CondVowel cv, CondPolarity cp);

		public:

			CompiledRule();
			CompiledRule(const CompiledRule&);
			CompiledRule(CompiledRule&&) noexcept;
			~CompiledRule();
			CompiledRule& operator=(const CompiledRule&);
			CompiledRule& operator=(CompiledRule&&);

			bool isReady() const { return !dfa.empty(); }

			std::vector<std::u16string> combine(
				U16StringView leftForm, POSTag leftTag,
				U16StringView rightForm, POSTag rightTag,
				CondVowel cv = CondVowel::none, CondPolarity cp = CondPolarity::none
			) const;

			std::vector<std::u16string> combine(
				const char16_t* leftForm, POSTag leftTag,
				const char16_t* rightForm, POSTag rightTag,
				CondVowel cv = CondVowel::none, CondPolarity cp = CondPolarity::none
			) const;

			Joiner newJoiner() const
			{
				return Joiner{ *this };
			}

			void addAllomorph(const std::vector<std::tuple<U16StringView, CondVowel, uint8_t>>& forms, POSTag tag);

			/**
			 * @return vector of tuple(replaceGroupId, capturedStartPos, replaceGroupCondition)
			 */
			std::vector<std::tuple<size_t, size_t, CondPolarity>> testLeftPattern(
				U16StringView leftForm, POSTag leftTag, POSTag rightTag,
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
					const Vector<ReplString>& _results = {},
					CondVowel _leftVowel = CondVowel::none,
					CondPolarity _leftPolar = CondPolarity::none,
					bool _ignoreRCond = false
				)
					: left{ _left }, right { _right }, repl{ _results, _leftVowel, _leftPolar, _ignoreRCond }
				{
				}
			};

			UnorderedMap<std::tuple<POSTag, POSTag, uint8_t>, Vector<size_t>> ruleset;
			Vector<Rule> rules;

			static Vector<char16_t> getVocabList(const Vector<Pattern::Node>& nodes);
			
			template<class GroupSizeTy>
			static MultiRuleDFAErased buildRules(
				size_t ruleSize,
				const Vector<Pattern::Node>& nodes, 
				const Vector<size_t>& ends, 
				const Vector<size_t>& startingGroup, 
				const Vector<size_t>& sepPositions = {},
				Vector<Replacement>&& finish = {}
			);

			template<class GroupSizeTy>
			static MultiRuleDFAErased buildRules(const Vector<Rule>& rules);
			
			static MultiRuleDFAErased buildRules(const Vector<Rule>& rules);

			static MultiRuleDFAErased buildRightPattern(const Vector<Rule>& rules);
		public:
			RuleSet();
			RuleSet(const RuleSet&);
			RuleSet(RuleSet&&) noexcept;
			~RuleSet();
			RuleSet& operator=(const RuleSet&);
			RuleSet& operator=(RuleSet&&);

			explicit RuleSet(std::istream& ruleFile)
			{
				loadRules(ruleFile);
			}

			void addRule(const std::string& lTag, const std::string& rTag,
				const KString& lPat, const KString& rPat, const std::vector<ReplString>& results,
				CondVowel leftVowel, CondPolarity leftPolar, bool ignoreRCond
			);
			
			void loadRules(std::istream& istr);

			CompiledRule compile() const;
		};
	}
}
