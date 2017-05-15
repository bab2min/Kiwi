// KiwiPy.cpp : Defines the exported functions for the DLL application.
//

#include "python.h"

static PyObject* test(PyObject* self, PyObject* args)
{
	return Py_BuildValue("i", 12345);
}

PyMODINIT_FUNC PyInit_kiwiPy()
{
	static PyMethodDef methods[] = {
		{"test", test, METH_VARARGS, "test function"},
		{nullptr, nullptr, 0, nullptr}
	};
	static PyModuleDef mod = {
		PyModuleDef_HEAD_INIT,
		"kiwiPy",
		"Kiwi Module for Python",
		-1,
		methods
	};
	return PyModule_Create(&mod);
}