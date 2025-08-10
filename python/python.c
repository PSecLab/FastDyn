#if ENABLE_LIBPY
#include <Python.h>
#include <python.h>
#include <ctype.h>

PyMODINIT_FUNC PyInit_emb(void);



int python_vm_setup(void) {
	// Shutdown the Python Interpreter if already started.
	Py_Finalize();

    //Register QEMU-API Module
    PyImport_AppendInittab("qemuapi", PyInit_emb);

	//Python Interpreter can be used anywhere now!
	return 0;
}
		//Expose read/write registers/memory API from here...
		uint32_t read_reg(int reg);
		uint32_t read_reg(int reg){
			uint32_t reg_val = qemu_get_register(reg);
			return reg_val;
		}
		static PyObject *read_reg_callback(PyObject *self, PyObject *args) {
			return PyCapsule_New((void *)read_reg, "read_reg_func", NULL);
		}

		uint32_t read_floating_reg(int reg);
		uint32_t read_floating_reg(int reg){
			FloatConverter fc;
			fc.i = qemu_get_register(reg);
			DEBUG_LOG("Read_REG value %f\n", fc.f);
			return fc.f;
		}
		static PyObject *read_floating_reg_callback(PyObject *self, PyObject *args) {
			return PyCapsule_New((void *)read_floating_reg, "read_floating_reg_func", NULL);
		}

		void write_reg(int reg, uint32_t val);
		void write_reg(int reg, uint32_t val){
			qemu_set_register(val, reg);
		}
		static PyObject *write_reg_callback(PyObject *self, PyObject *args) {
			return PyCapsule_New((void *)write_reg, "write_reg_func", NULL);
		}

		void write_floating_reg(int reg, float val);
		void write_floating_reg(int reg, float val){
			FloatConverter fc;
			fc.f = val;
			DEBUG_LOG("the value from c code is %f\n", fc.f);
			qemu_set_register(fc.i, reg);
		}
		static PyObject *write_floating_reg_callback(PyObject *self, PyObject *args) {
			return PyCapsule_New((void *)write_floating_reg, "write_floating_reg_func", NULL);
		}

		int read_memory(unsigned long long addr, uint8_t *mem_buf, int len);
		int read_memory(unsigned long long addr, uint8_t *mem_buf, int len){
			return qemu_plugin_read_memory(addr, mem_buf, len);
		}
		static PyObject *read_mem_callback(PyObject *self, PyObject *args) {
			return PyCapsule_New((void *)read_memory, "read_mem_func", NULL);
		}

		int write_memory(unsigned long long addr, uint8_t *mem_buf, int len);
		int write_memory(unsigned long long addr, uint8_t *mem_buf, int len){
			return qemu_plugin_write_memory(addr, mem_buf, len);
		}
		static PyObject *write_mem_callback(PyObject *self, PyObject *args) {
			return PyCapsule_New((void *)write_memory, "write_mem_func", NULL);
		}

		unsigned long long virtual_clock(void);
		unsigned long long virtual_clock(void){
			return (unsigned long long)qemu_plugin_get_virtual_timer();
		}

		static PyObject *virtual_clock_callback(PyObject *self, PyObject *args) {
			return PyCapsule_New((void *)virtual_clock, "virtual_clock_func", NULL);
		}

		// Python method definitions for both read and write
		static PyMethodDef EmbMethods[] = {
			{"read_reg_callback", read_reg_callback, METH_NOARGS, "Returns a pointer to the C read_reg callback function"},
			{"write_reg_callback", write_reg_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
			{"read_mem_callback", read_mem_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
			{"write_mem_callback", write_mem_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
			{"virtual_clock_callback", virtual_clock_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
			{"write_floating_reg_callback", write_floating_reg_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
			{"read_floating_reg_callback", read_floating_reg_callback, METH_NOARGS, "Returns a pointer to the C write_reg callback function"},
			{NULL, NULL, 0, NULL}
		};

		static struct PyModuleDef qemuapi = {
			PyModuleDef_HEAD_INIT, "qemuapi", NULL, -1, EmbMethods
		};

		PyMODINIT_FUNC PyInit_emb(void) {
			return PyModule_Create(&qemuapi);
		}

#endif