#include "stdafx.h"
#include "KForm.h"

KForm::KForm(const char * _form)
{
	if (_form) form = {_form, _form + strlen(_form)};
}
