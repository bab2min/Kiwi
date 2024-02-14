#include <iostream>
#include <fstream>
#include <string>
#include <kiwi/Types.h>
#include <kiwi/Utils.h>
#include "StrUtils.h"

using namespace std;

namespace kiwi
{
	ifstream& openFile(ifstream& f, const string& filePath, ios_base::openmode mode)
	{
		auto exc = f.exceptions();
		f.exceptions(ifstream::failbit | ifstream::badbit);
		try
		{
#if defined(_WIN32) || defined(_WIN64)
			f.open((const wchar_t*)utf8To16(filePath).c_str(), mode);
#else
			f.open(filePath, mode);
#endif
		}
		catch (const ios_base::failure&)
		{
			throw IOException{ "Cannot open file : " + filePath };
		}
		f.exceptions(exc);
		return f;
	}

	ofstream& openFile(ofstream& f, const string& filePath, ios_base::openmode mode)
	{
		auto exc = f.exceptions();
		f.exceptions(ofstream::failbit | ofstream::badbit);
		try
		{
#if defined(_WIN32) || defined(_WIN64)
			f.open((const wchar_t*)utf8To16(filePath).c_str(), mode);
#else
			f.open(filePath, mode);
#endif
		}
		catch (const ios_base::failure&)
		{
			throw IOException{ "Cannot open file : " + filePath };
		}
		f.exceptions(exc);
		return f;
	}
}
