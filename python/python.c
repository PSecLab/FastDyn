#include <Python.h>
#include <ctype.h>

PyMODINIT_FUNC PyInit_emb(void);



int python_vm_setup() {
	// Shutdown the Python Interpreter if already started.
	Py_Finalize();

    //Register QEMU-API Module
    PyImport_AppendInittab("qemuapi", PyInit_emb);

	//Python Interpreter can be used anywhere now!
	return 0;
}
