#include <cstring>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>

using namespace std;

namespace kiwi
{
	namespace
	{
		// kiwipiepy `Kiwi.space`의 space_insertable 정규식을 옮긴 것이다.
		//   (([^SUWX]|X[RS]|S[EH]).* ([NMI]|V[VAX]|VCN|XR|XPN|S[WLHN]))
		//   (SN ([MI]|N[PR]|NN[GP]|V[VAX]|VCN|XR|XPN|S[WH]))
		//   ((S[FPL]).* ([NMI]|V[VAX]|VCN|XR|XPN|S[WH]))
		inline bool spacingLeftOfAlt1(const char* l)
		{
			if (l[0] && l[0] != 'S' && l[0] != 'U' && l[0] != 'W' && l[0] != 'X') return true;
			if (l[0] == 'X' && (l[1] == 'R' || l[1] == 'S')) return true;
			if (l[0] == 'S' && (l[1] == 'E' || l[1] == 'H')) return true;
			return false;
		}

		inline bool spacingRightCommon(const char* r)
		{
			if (r[0] == 'V' && (r[1] == 'V' || r[1] == 'A' || r[1] == 'X')) return true;
			if (r[0] == 'V' && r[1] == 'C' && r[2] == 'N') return true;
			if (r[0] == 'X' && r[1] == 'R') return true;
			if (r[0] == 'X' && r[1] == 'P' && r[2] == 'N') return true;
			return false;
		}

		inline bool spacingRightOfAlt1(const char* r)
		{
			if (r[0] == 'N' || r[0] == 'M' || r[0] == 'I') return true;
			if (spacingRightCommon(r)) return true;
			if (r[0] == 'S' && (r[1] == 'W' || r[1] == 'L' || r[1] == 'H' || r[1] == 'N')) return true;
			return false;
		}

		inline bool spacingRightOfAlt2(const char* r)
		{
			if (r[0] == 'M' || r[0] == 'I') return true;
			if (r[0] == 'N' && (r[1] == 'P' || r[1] == 'R')) return true;
			if (r[0] == 'N' && r[1] == 'N' && (r[2] == 'G' || r[2] == 'P')) return true;
			if (spacingRightCommon(r)) return true;
			if (r[0] == 'S' && (r[1] == 'W' || r[1] == 'H')) return true;
			return false;
		}

		inline bool spacingRightOfAlt3(const char* r)
		{
			if (r[0] == 'N' || r[0] == 'M' || r[0] == 'I') return true;
			if (spacingRightCommon(r)) return true;
			if (r[0] == 'S' && (r[1] == 'W' || r[1] == 'H')) return true;
			return false;
		}

		inline bool isSpaceInsertableForSpacing(const char* l, const char* r)
		{
			if (spacingLeftOfAlt1(l) && spacingRightOfAlt1(r)) return true;
			if (strcmp(l, "SN") == 0 && spacingRightOfAlt2(r)) return true;
			if (l[0] == 'S' && (l[1] == 'F' || l[1] == 'P' || l[1] == 'L') && spacingRightOfAlt3(r)) return true;
			return false;
		}

		inline bool isSpacingResetFollower(char16_t c)
		{
			return isHangulSyllable(c)
				|| c == u'.' || c == u',' || c == u'?' || c == u'!' || c == u':' || c == u';';
		}

		u16string resetWhitespaceInText(const u16string& str)
		{
			u16string ret;
			ret.reserve(str.size());
			for (size_t i = 0; i < str.size();)
			{
				if (isSpace(str[i]) && !ret.empty() && isHangulSyllable(ret.back()))
				{
					size_t j = i;
					while (j < str.size() && isSpace(str[j])) ++j;
					if (j < str.size() && isSpacingResetFollower(str[j]))
					{
						i = j;
						continue;
					}
					ret.append(str, i, j - i);
					i = j;
					continue;
				}
				ret.push_back(str[i++]);
			}
			return ret;
		}

		void appendWithoutSpaces(u16string& ret, const u16string& str, size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; ++i)
			{
				if (!isSpace(str[i])) ret.push_back(str[i]);
			}
		}

		u16string stripSpaces(const u16string& str)
		{
			size_t b = 0, e = str.size();
			while (b < e && isSpace(str[b])) ++b;
			while (e > b && isSpace(str[e - 1])) --e;
			return str.substr(b, e - b);
		}

		inline bool endsWithAlphaNumeric(const u16string& str)
		{
			if (str.empty()) return false;
			const char16_t c = str.back();
			return (u'0' <= c && c <= u'9') || (u'A' <= c && c <= u'Z') || (u'a' <= c && c <= u'z');
		}
	}

	u16string Kiwi::space(const u16string& str, bool resetWhitespace) const
	{
		const u16string src = resetWhitespace ? resetWhitespaceInText(str) : str;
		// kiwipiepy가 사용하는 Match.ALL | Match.Z_CODA와 동일하다. (C++의 Match::all은 zCoda를 포함한다)
		const TokenResult res = analyze(src, AnalyzeOption{ Match::all });
		const auto& tokens = res.first;

		u16string ret;
		ret.reserve(src.size());
		size_t last = 0;
		const char* prevTag = nullptr;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			const auto& t = tokens[i];
			const char* curTag = tagToString(t.tag);
			const bool auxHajiJi = strcmp(curTag, "VX") == 0
				&& (t.str == u"하" || t.str == u"지" || t.str == u"하지");

			if (last < t.position)
			{
				if (curTag[0] == 'E' || curTag[0] == 'J'
					|| (curTag[0] == 'X' && curTag[1] == 'S')
					|| auxHajiJi
					|| (prevTag && strcmp(prevTag, "SN") == 0 && strcmp(curTag, "NNB") == 0))
				{
					appendWithoutSpaces(ret, src, last, t.position);
				}
				else
				{
					ret.append(src, last, t.position - last);
				}
				last = t.position;
			}

			if (prevTag && !auxHajiJi && isSpaceInsertableForSpacing(prevTag, curTag))
			{
				if (!ret.empty() && !isSpace(ret.back())) ret.push_back(u' ');
			}

			if (last < t.endPos())
			{
				if (curTag[0] == 'N' && curTag[1] == 'N'
					&& (i + 1 >= tokens.size() || t.endPos() <= tokens[i + 1].position))
				{
					ret += t.str;
				}
				else
				{
					appendWithoutSpaces(ret, src, last, t.endPos());
				}
			}
			last = t.endPos();
			prevTag = curTag;
		}
		if (last < src.size()) ret.append(src, last, src.size() - last);
		return ret;
	}

	string Kiwi::space(const string& str, bool resetWhitespace) const
	{
		return utf16To8(space(utf8To16(str), resetWhitespace));
	}

	u16string Kiwi::glue(const vector<u16string>& textChunks,
		const vector<bool>& insertNewLines,
		vector<bool>* spaceInsertionsOut) const
	{
		if (spaceInsertionsOut) spaceInsertionsOut->clear();
		if (textChunks.empty()) return {};

		vector<u16string> chunks;
		chunks.reserve(textChunks.size());
		for (auto& c : textChunks) chunks.emplace_back(stripSpaces(c));

		// kiwipiepy가 사용하는 Match.ALL과 동일하다. (C++의 Match::all과 달리 zCoda를 포함하지 않는다)
		const AnalyzeOption option{ Match::url | Match::email | Match::hashtag
			| Match::mention | Match::serial | Match::emoji };

		u16string ret = chunks[0];
		for (size_t i = 0; i + 1 < chunks.size(); ++i)
		{
			// kiwipiepy와 동일하게 insertNewLines가 먼저 소진되면 결합을 중단한다.
			if (!insertNewLines.empty() && i >= insertNewLines.size()) break;

			u16string withSpace = chunks[i];
			withSpace.push_back(u' ');
			withSpace += chunks[i + 1];
			const u16string withoutSpace = chunks[i] + chunks[i + 1];

			const float scoreWithSpace = analyze(withSpace, option).second;
			const float scoreWithoutSpace = analyze(withoutSpace, option).second;

			const bool insertSpace = scoreWithSpace >= scoreWithoutSpace || endsWithAlphaNumeric(chunks[i]);
			if (insertSpace)
			{
				const bool newLine = !insertNewLines.empty() && insertNewLines[i];
				ret.push_back(newLine ? u'\n' : u' ');
			}
			if (spaceInsertionsOut) spaceInsertionsOut->emplace_back(insertSpace);
			ret += chunks[i + 1];
		}
		return ret;
	}

	string Kiwi::glue(const vector<string>& textChunks,
		const vector<bool>& insertNewLines,
		vector<bool>* spaceInsertionsOut) const
	{
		vector<u16string> u16chunks;
		u16chunks.reserve(textChunks.size());
		for (auto& c : textChunks) u16chunks.emplace_back(utf8To16(c));
		return utf16To8(glue(u16chunks, insertNewLines, spaceInsertionsOut));
	}
}
