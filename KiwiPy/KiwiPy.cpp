// KiwiPy.cpp : Defines the exported functions for the DLL application.
//
#ifdef _WIN32
#include "stdafx.h"
#endif

#include <stdexcept>
#include <Python.h>
#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Kiwi.h"

using namespace std;

static PyObject* gModule;

static PyObject* kiwi__init(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	const char* modelPath = "./";
	size_t numThread = 0, options = 1;
	if (!PyArg_ParseTuple(args, "O|nsn", &argSelf, &numThread, &modelPath, &options)) return nullptr;
	try
	{
		Kiwi* inst = nullptr;
		try
		{
			inst = new Kiwi{ modelPath, 0, numThread, options };
		}
		catch (const exception& e)
		{
			PyObject* filePath = PyModule_GetFilenameObject(PyImport_AddModule("kiwipiepy"));
			string spath = PyUnicode_AsUTF8(filePath);
			Py_DECREF(filePath);
			if (spath.rfind('/') != spath.npos)
			{
				spath = spath.substr(0, spath.rfind('/') + 1);
			}
			else
			{
				spath = spath.substr(0, spath.rfind('\\') + 1);
			}
			inst = new Kiwi{ (spath + modelPath).c_str(), 0, numThread, options };
		}
		PyObject_SetAttrString(argSelf, "_inst", PyLong_FromLongLong((long long)inst));
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* kiwi__close(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	if (!PyArg_ParseTuple(args, "O", &argSelf)) return nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		if (inst) delete inst;
		PyObject_SetAttrString(argSelf, "_inst", PyLong_FromLongLong(0));
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* kiwi__addUserWord(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	const char* word;
	const char* tag = "NNP";
	float score = 10;
	if (!PyArg_ParseTuple(args, "Os|sf", &argSelf, &word, &tag, &score)) return nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		return Py_BuildValue("n", inst->addUserWord(Kiwi::toU16(word), makePOSTag(Kiwi::toU16(tag)), score));
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}
}

static PyObject* kiwi__loadUserDictionary(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	const char* path;
	if (!PyArg_ParseTuple(args, "Os", &argSelf, &path)) return nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		return Py_BuildValue("n", inst->loadUserDictionary(path));
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}
}

static PyObject* kiwi__extractWords(PyObject* self, PyObject* args)
{
	PyObject* argSelf, *argReader;
	size_t minCnt = 10, maxWordLen = 10;
	float minScore = 0.25f;
	if (!PyArg_ParseTuple(args, "OO|nnf", &argSelf, &argReader, &minCnt, &maxWordLen, &minScore)) return nullptr;
	if (!PyCallable_Check(argReader)) return PyErr_SetString(PyExc_TypeError, "extractWords requires 1st parameter which is callable"), nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		auto res = inst->extractWords([argReader](size_t id) -> u16string
		{
			PyObject* argList = Py_BuildValue("(n)", id);
			PyObject* retVal = PyEval_CallObject(argReader, argList);
			Py_DECREF(argList);
			if (!retVal) throw bad_exception();
			if (PyObject_Not(retVal))
			{
				Py_DECREF(retVal);
				return {};
			}
			auto p = Kiwi::toU16(PyUnicode_AsUTF8(retVal));
			Py_DECREF(retVal);
			return p;
		}, minCnt, maxWordLen, minScore);

		PyObject* retList = PyList_New(res.size());
		size_t idx = 0;
		for (auto& r : res)
		{
			PyList_SetItem(retList, idx++, Py_BuildValue("(sfnf)", Kiwi::toU8(r.form).c_str(), r.score, r.freq, r.posScore[KPOSTag::NNP]));
		}
		return retList;
	}
	catch (const bad_exception& e)
	{
		return nullptr;
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* kiwi__extractFilterWords(PyObject* self, PyObject* args)
{
	PyObject* argSelf, *argReader;
	size_t minCnt = 10, maxWordLen = 10;
	float minScore = 0.25f, posScore = -3;
	if (!PyArg_ParseTuple(args, "OO|nnff", &argSelf, &argReader, &minCnt, &maxWordLen, &minScore, &posScore)) return nullptr;
	if (!PyCallable_Check(argReader)) return PyErr_SetString(PyExc_TypeError, "extractFilterWords requires 1st parameter which is callable"), nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		auto res = inst->extractWords([argReader](size_t id) -> u16string
		{
			PyObject* argList = Py_BuildValue("(n)", id);
			PyObject* retVal = PyEval_CallObject(argReader, argList);
			Py_DECREF(argList);
			if (!retVal) throw bad_exception();
			if (PyObject_Not(retVal))
			{
				Py_DECREF(retVal);
				return {};
			}
			auto p = Kiwi::toU16(PyUnicode_AsUTF8(retVal));
			Py_DECREF(retVal);
			return p;
		}, minCnt, maxWordLen, minScore);

		res = inst->filterExtractedWords(move(res), posScore);
		PyObject* retList = PyList_New(res.size());
		size_t idx = 0;
		for (auto& r : res)
		{
			PyList_SetItem(retList, idx++, Py_BuildValue("(sfnf)", Kiwi::toU8(r.form).c_str(), r.score, r.freq, r.posScore[KPOSTag::NNP]));
		}
		return retList;
	}
	catch (const bad_exception& e)
	{
		return nullptr;
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* kiwi__extractAddWords(PyObject* self, PyObject* args)
{
	PyObject* argSelf, *argReader;
	size_t minCnt = 10, maxWordLen = 10;
	float minScore = 0.25f, posScore = -3;
	if (!PyArg_ParseTuple(args, "OO|nnff", &argSelf, &argReader, &minCnt, &maxWordLen, &minScore, &posScore)) return nullptr;
	if (!PyCallable_Check(argReader)) return PyErr_SetString(PyExc_TypeError, "extractAddWords requires 1st parameter which is callable"), nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		auto res = inst->extractAddWords([argReader](size_t id) -> u16string
		{
			PyObject* argList = Py_BuildValue("(n)", id);
			PyObject* retVal = PyEval_CallObject(argReader, argList);
			Py_DECREF(argList);
			if (!retVal) throw bad_exception();
			if (PyObject_Not(retVal))
			{
				Py_DECREF(retVal);
				return {};
			}
			auto p = Kiwi::toU16(PyUnicode_AsUTF8(retVal));
			Py_DECREF(retVal);
			return p;
		}, minCnt, maxWordLen, minScore, posScore);

		PyObject* retList = PyList_New(res.size());
		size_t idx = 0;
		for (auto& r : res)
		{
			PyList_SetItem(retList, idx++, Py_BuildValue("(sfnf)", Kiwi::toU8(r.form).c_str(), r.score, r.freq, r.posScore[KPOSTag::NNP]));
		}
		return retList;
	}
	catch (const bad_exception& e)
	{
		return nullptr;
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* kiwi__setCutOffThreshold(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	float threshold;
	if (!PyArg_ParseTuple(args, "Of", &argSelf, &threshold)) return nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		inst->setCutOffThreshold(threshold);
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* kiwi__prepare(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	if (!PyArg_ParseTuple(args, "O", &argSelf)) return nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		return Py_BuildValue("n", inst->prepare());
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}
}

PyObject* resToPyList(const vector<KResult>& res)
{
	PyObject* retList = PyList_New(res.size());
	size_t idx = 0;
	for (auto& p : res)
	{
		PyObject* rList = PyList_New(p.first.size());
		size_t jdx = 0;
		for (auto& q : p.first)
		{
			PyList_SetItem(rList, jdx++, Py_BuildValue("(ssnn)", Kiwi::toU8(q.str()).c_str(), tagToString(q.tag()), (size_t)q.pos(), (size_t)q.len()));
		}
		PyList_SetItem(retList, idx++, Py_BuildValue("(Of)", rList, p.second));
	}
	return retList;
}

static PyObject* kiwi__analyze(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	size_t topN = 1;
	{
		char* text;
		if (PyArg_ParseTuple(args, "Os|n", &argSelf, &text, &topN))
		{
			try
			{
				PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
				if (!instObj) throw runtime_error{ "_inst is null" };
				Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
				Py_DECREF(instObj);

				auto res = inst->analyze(text, topN);
				return resToPyList(res);
			}
			catch (const exception& e)
			{
				PyErr_SetString(PyExc_Exception, e.what());
				return nullptr;
			}
		}
	}
	{
		PyObject* reader, *receiver;
		if (PyArg_ParseTuple(args, "OOO|n", &argSelf, &reader, &receiver, &topN))
		{
			try
			{
				if (!PyCallable_Check(reader)) return PyErr_SetString(PyExc_TypeError, "analyze requires 1st parameter which is callable"), nullptr;
				if (!PyCallable_Check(receiver)) return PyErr_SetString(PyExc_TypeError, "analyze requires 2nd parameter which is callable"), nullptr;
				PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
				Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
				Py_DECREF(instObj);
				inst->analyze(topN, [&reader](size_t id)->u16string
				{
					PyObject* argList = Py_BuildValue("(n)", id);
					PyObject* retVal = PyEval_CallObject(reader, argList);
					Py_DECREF(argList);
					if (!retVal) throw bad_exception();
					if (PyObject_Not(retVal))
					{
						Py_DECREF(retVal);
						return {};
					}
					auto p = Kiwi::toU16(PyUnicode_AsUTF8(retVal));
					Py_DECREF(retVal);
					return p;
				}, [&receiver](size_t id, vector<KResult>&& res)
				{
					PyObject* l = resToPyList(res);
					PyObject* argList = Py_BuildValue("(nO)", id, l);
					PyObject* ret = PyEval_CallObject(receiver, argList);
					if(!ret) throw bad_exception();
					Py_DECREF(ret);
					Py_DECREF(argList);
				});
				Py_INCREF(Py_None);
				return Py_None;
			}
			catch (const bad_exception& e)
			{
				return nullptr;
			}
			catch (const exception& e)
			{
				PyErr_SetString(PyExc_Exception, e.what());
				return nullptr;
			}
		}
	}
	return nullptr;
}


static PyObject* kiwi__perform(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	size_t topN = 1;
	PyObject* reader, *receiver;
	size_t minCnt = 10, maxWordLen = 10;
	float minScore = 0.25f, posScore = -3;
	if (!PyArg_ParseTuple(args, "OOO|nnnff", &argSelf, &reader, &receiver, &topN, &minCnt, &maxWordLen, &minScore, &posScore)) return nullptr;
	try
	{
		if (!PyCallable_Check(reader)) return PyErr_SetString(PyExc_TypeError, "perform requires 1st parameter which is callable"), nullptr;
		if (!PyCallable_Check(receiver)) return PyErr_SetString(PyExc_TypeError, "perform requires 2nd parameter which is callable"), nullptr;
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);

		inst->perform(topN, [&reader](size_t id)->u16string
		{
			PyObject* argList = Py_BuildValue("(n)", id);
			PyObject* retVal = PyEval_CallObject(reader, argList);
			Py_DECREF(argList);
			if (!retVal) throw bad_exception();
			if (PyObject_Not(retVal))
			{
				Py_DECREF(retVal);
				return {};
			}
			auto p = Kiwi::toU16(PyUnicode_AsUTF8(retVal));
			Py_DECREF(retVal);
			return p;
		}, [&receiver](size_t id, vector<KResult>&& res)
		{
			PyObject* l = resToPyList(res);
			PyObject* argList = Py_BuildValue("(nO)", id, l);
			PyObject* ret = PyEval_CallObject(receiver, argList);
			if (!ret) throw bad_exception();
			Py_DECREF(ret);
			Py_DECREF(argList);
		}, minCnt, maxWordLen, minScore, posScore);
		Py_INCREF(Py_None);
		return Py_None;
	}
	catch (const bad_exception& e)
	{
		return nullptr;
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}
	return nullptr;
}


static PyObject* kiwi__version(PyObject* self, PyObject* args)
{
	PyObject* argSelf;
	if (!PyArg_ParseTuple(args, "O", &argSelf)) return nullptr;
	try
	{
		PyObject* instObj = PyObject_GetAttrString(argSelf, "_inst");
		if (!instObj) throw runtime_error{ "_inst is null" };
		Kiwi* inst = (Kiwi*)PyLong_AsLongLong(instObj);
		Py_DECREF(instObj);
		return Py_BuildValue("n", inst->getVersion());
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		PyErr_SetString(PyExc_Exception, e.what());
		return nullptr;
	}
}

static PyObject *createClassObject(const char *name, PyMethodDef methods[])
{
	PyObject *pClassName = PyUnicode_FromString(name);
	PyObject *pClassBases = PyTuple_New(0);
	PyObject *pClassDic = PyDict_New();


	PyMethodDef *def;
	for (def = methods; def->ml_name != NULL; def++)
	{
		PyObject *func = PyCFunction_New(def, NULL);
		PyObject *method = PyInstanceMethod_New(func);
		PyDict_SetItemString(pClassDic, def->ml_name, method);
		Py_DECREF(func);
		Py_DECREF(method);
	}

	PyObject *pClass = PyObject_CallFunctionObjArgs((PyObject *)&PyType_Type, pClassName, pClassBases, pClassDic, NULL);

	Py_DECREF(pClassName);
	Py_DECREF(pClassBases);
	Py_DECREF(pClassDic);

	return pClass;
}

PyMODINIT_FUNC PyInit_kiwipiepycore()
{
	static PyMethodDef methods[] =
	{
		{ nullptr, nullptr, 0, nullptr }
	};
	static PyModuleDef mod =
	{
		PyModuleDef_HEAD_INIT,
		"kiwipy",
		"Kiwi Module for Python",
		-1,
		methods
	};
	static PyMethodDef clsMethods[] =
	{
		{ "__init__", kiwi__init, METH_VARARGS, "initializer" },
		{ "addUserWord", kiwi__addUserWord, METH_VARARGS, "add custom word into model" },
		{ "loadUserDictionary", kiwi__loadUserDictionary, METH_VARARGS, "load custom dictionary file into model" },
		{ "extractWords", kiwi__extractWords, METH_VARARGS, "extract words from corpus" },
		{ "extractFilterWords", kiwi__extractFilterWords, METH_VARARGS, "extract words from corpus and filter the results" },
		{ "extractAddWords", kiwi__extractAddWords, METH_VARARGS, "extract words from corpus and add them into model" },
		{ "perform", kiwi__perform, METH_VARARGS, "extractAddWords + prepare + analyze" },
		{ "setCutOffThreshold", kiwi__setCutOffThreshold, METH_VARARGS, "prepare for analyzing text" },
		{ "prepare", kiwi__prepare, METH_VARARGS, "prepare for analyzing text" },
		{ "analyze", kiwi__analyze, METH_VARARGS, "analyze text and return topN results" },
		{ "version", kiwi__version, METH_VARARGS, "return version" },
		{ "__del__", kiwi__close, METH_VARARGS, "destructor" },
		{ nullptr, nullptr, 0, nullptr }
	};
	gModule = PyModule_Create(&mod);
	PyObject *pModuleDic = PyModule_GetDict(gModule);
	PyDict_SetItemString(pModuleDic, "Kiwi", createClassObject("Kiwi", clsMethods));
	if (!PyEval_ThreadsInitialized()) {
		PyEval_InitThreads();
	}
	return gModule;
}
