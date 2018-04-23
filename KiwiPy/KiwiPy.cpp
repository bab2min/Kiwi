// KiwiPy.cpp : Defines the exported functions for the DLL application.
//

#include "Python.h"
#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Kiwi.h"

static PyObject* initKiwi(PyObject* self, PyObject* args)
{
	char* modelPath;
	int cacheSize = -1;
	int numThread = 0;
	if(!PyArg_ParseTuple(args, "sii", &modelPath, &cacheSize, &numThread)) return nullptr;
	try
	{
		Kiwi* inst = new Kiwi(modelPath, cacheSize, numThread);
		return Py_BuildValue("n", inst);
	}
	catch (const exception& e)
	{
		PyErr_SetString(PyExc_RuntimeError, e.what());
		return nullptr;
	}
}

static PyObject* loadUserDictKiwi(PyObject* self, PyObject* args)
{
	Kiwi* inst;
	char* userDictPath;
	if (!PyArg_ParseTuple(args, "ns", &inst, &userDictPath)) return nullptr;
	return Py_BuildValue("i", inst->loadUserDictionary(userDictPath));
}

static PyObject* prepareKiwi(PyObject* self, PyObject* args)
{
	Kiwi* inst;
	if (!PyArg_ParseTuple(args, "n", &inst)) return nullptr;
	try
	{
		int res = inst->prepare();
		return Py_BuildValue("i", res);
	}
	catch (const exception& e)
	{
		printf(e.what());
		PyErr_SetString(PyExc_RuntimeError, e.what());
		return nullptr;
	}
}

static PyObject* analyzeKiwi(PyObject* self, PyObject* args)
{
	Kiwi* inst;
	char* text;
	int topN = 1;
	if (!PyArg_ParseTuple(args, "nsi", &inst, &text, &topN)) return nullptr;
	try
	{
		auto res = inst->analyze(text, topN);
		PyObject* resList = PyList_New(res.size());
		size_t idx = 0;
		wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;
		for (auto r : res)
		{
			PyObject* t = PyList_New(r.first.size());
			size_t jdx = 0;
			for (auto w : r.first)
			{
				PyList_SetItem(t, jdx++, Py_BuildValue("(ss)", converter.to_bytes(w.str()).c_str(), tagToString(w.tag())));
			}
			PyList_SetItem(resList, idx++, Py_BuildValue("(Of)", t, r.second));
			Py_DECREF(t);
		}
		return resList;
	}
	catch (const exception& e)
	{
		printf(e.what());
		PyErr_SetString(PyExc_RuntimeError, e.what());
		return nullptr;
	}
}

static PyObject* closeKiwi(PyObject* self, PyObject* args)
{
	Kiwi* inst;
	if (!PyArg_ParseTuple(args, "n", &inst)) return nullptr;
	delete inst;
	return Py_BuildValue("s", nullptr);
}


PyMODINIT_FUNC PyInit_kiwiPyRaw()
{
	static PyMethodDef methods[] = {
		{ "initKiwi", initKiwi, METH_VARARGS, "" },
		{ "closeKiwi", closeKiwi, METH_VARARGS, "" },
		{ "loadUserDictKiwi", loadUserDictKiwi, METH_VARARGS, "" },
		{ "prepareKiwi", prepareKiwi, METH_VARARGS, "" },
		{ "analyzeKiwi", analyzeKiwi, METH_VARARGS, "" },
		{nullptr, nullptr, 0, nullptr}
	};
	static PyModuleDef mod = {
		PyModuleDef_HEAD_INIT,
		"kiwiPyRaw",
		"Kiwi Module for Python",
		-1,
		methods
	};
	return PyModule_Create(&mod);
}