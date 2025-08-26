#ifndef PYTHON_H
#define PYTHON_H
#include <common.h>
#include <config.h>
#include <utils.h>
#include <Python.h>

int python_vm_setup(void);
void initialize_halucinator_python_vm(void);

extern uint8_t py_init;
extern uint8_t peripheral_server_init;
extern PyObject *fastdyn_interceptor;
extern PyObject *halucinator_initialize;
extern PyObject *dev_notify_irq_callback;
extern PyObject *dev_irqret_hook_callback;
extern PyObject *device_model_callback;


#endif // PYTHON_H
