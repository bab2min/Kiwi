#include "stdafx.h"
#include "Utils.h"
#include "Kiwi.h"
#include "KFeatureTestor.h"
#include "KModelMgr.h"

using namespace std;

Kiwi::Kiwi(const char * modelPath, size_t _maxCache, size_t _numThread) : maxCache(_maxCache),
	numThread(_numThread ? _numThread : thread::hardware_concurrency()), 
	threadPool(_numThread ? _numThread : thread::hardware_concurrency())
{
	mdl = make_shared<KModelMgr>(modelPath);
}

int Kiwi::addUserWord(const u16string & str, KPOSTag tag)
{
	mdl->addUserWord(normalizeHangul({ str.begin(), str.end() }), tag);
	return 0;
}

int Kiwi::addUserRule(const u16string & str, const vector<pair<u16string, KPOSTag>>& morph)
{
	vector<pair<k_string, KPOSTag>> jmMorph;
	jmMorph.reserve(morph.size());
	for (auto& m : morph)
	{
		jmMorph.emplace_back(normalizeHangul({ m.first.begin(), m.first.end() }), m.second);
	}
	mdl->addUserRule(normalizeHangul({ str.begin(), str.end() }), jmMorph);
	return 0;
}

int Kiwi::loadUserDictionary(const char * userDictPath)
{
	FILE* file = nullptr;
	if (fopen_s(&file, userDictPath, "r")) return -1;
	char buf[4096];
	while (fgets(buf, 4096, file))
	{
		if (buf[0] == '#') continue;
		auto wstr = utf8_to_utf16(buf);
		auto chunks = split(wstr, u'\t');
		if (chunks.size() < 2) continue;
		if (!chunks[1].empty()) 
		{
			auto pos = makePOSTag(chunks[1]);
			if (pos != KPOSTag::MAX)
			{
				addUserWord(chunks[0], pos);
				continue;
			}
		}
		
		vector<pair<u16string, KPOSTag>> morphs;
		for (size_t i = 1; i < chunks.size(); i++) 
		{
			auto cc = split(chunks[i], u'/');
			if (cc.size() != 2) goto loopContinue;
			auto pos = makePOSTag(cc[1]);
			if (pos == KPOSTag::MAX) goto loopContinue;
			morphs.emplace_back(cc[0], pos);
		}
		addUserRule(chunks[0], morphs);
	loopContinue:;
	}
	fclose(file);
	return 0;
}

int Kiwi::prepare()
{
	mdl->solidify();
	kt = mdl->getTrie();
	return 0;
}

KResult Kiwi::analyze(const u16string & str) const
{
	return analyze(str, 1)[0];
}

KResult Kiwi::analyze(const string & str) const
{
	return analyze(utf8_to_utf16(str));
}

vector<KResult> Kiwi::analyze(const string & str, size_t topN) const
{
	return analyze(utf8_to_utf16(str), topN);
}

vector<KResult> Kiwi::analyze(const u16string & str, size_t topN) const
{
	auto nodes = kt->split(normalizeHangul(str));
	auto res = mdl->findBestPath(nodes, topN);
	vector<KResult> ret;
	for (auto&& r : res)
	{
		vector<KWordPair> rarr;
		for (auto&& s : r.first)
		{
			rarr.emplace_back(joinHangul(s.second.empty() ? *s.first->kform : s.second), s.first->tag, 0, 0);
		}
		ret.emplace_back(rarr, r.second);
	}
	if (ret.empty()) ret.emplace_back();
	return ret;
}

void Kiwi::clearCache()
{

}

int Kiwi::getVersion()
{
	return 50;
}

std::ostream & operator<<(std::ostream & os, const KWordPair & kp)
{
	return os << utf16_to_utf8({ kp.str().begin(), kp.str().end() }) << '/' << tagToString(kp.tag());
}
