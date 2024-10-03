#pragma once
#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#define MODEL_PATH "./models/base"
#define KWORD u"킼윜"
#define KWORD8 u8"킼윜"

inline std::vector<std::string> loadTestCorpus()
{
	std::ifstream ifs{ "./data/evaluation/web.txt" };
	std::string str;
	std::vector<std::string> ret;
	while (std::getline(ifs, str))
	{
		size_t sep = str.find('\t');
		ret.emplace_back(str.substr(0, sep));
	}
	return ret;
}