static PyObject* _socket_gaierror = 0;


static PyObject*
_get_socket_exc(PyObject** pobject, const char* name)
{
    if (!*pobject) {
        PyObject* _socket;
        _socket = PyImport_ImportModule("_socket");
        if (_socket) {
            *pobject = PyObject_GetAttrString(_socket, name);
            if (!*pobject) {
                PyErr_WriteUnraisable(_socket);
            }
            Py_DECREF(_socket);
        }
        else {
            PyErr_WriteUnraisable(Py_None);
        }
        if (!*pobject) {
            *pobject = PyExc_IOError;
        }
    }
    return *pobject;
}


static PyObject *
_set_gaierror(int error)
{
    PyObject *v;

    v = Py_BuildValue("(is)", error, gai_strerror(error));
    if (v != NULL) {
        PyErr_SetObject(_get_socket_exc(&_socket_gaierror, "gaierror"), v);
        Py_DECREF(v);
    }

    return NULL;
}
