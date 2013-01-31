/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

//include for python extension module: must be first
#include <Python.h>
#include <vampyhost.h>
#include <pyRealTime.h>

//includes for vamp host
#include "vamp-hostsdk/Plugin.h"
#include "vamp-hostsdk/PluginHostAdapter.h"
#include "vamp-hostsdk/PluginChannelAdapter.h"
#include "vamp-hostsdk/PluginInputDomainAdapter.h"
#include "vamp-hostsdk/PluginLoader.h"
//#include "vamp/vamp.h"

#include <iostream>
#include <fstream>
#include <set>
#include <sndfile.h>

#include <cstring>
#include <cstdlib>
#include <string>

#include "system.h"

#include <cmath>


using namespace std;
using namespace Vamp;

using Vamp::Plugin;
using Vamp::PluginHostAdapter;
using Vamp::RealTime;
using Vamp::HostExt::PluginLoader;

#define HOST_VERSION "1.1"


/* MODULE HELPER FUNCTIONS */
PyDoc_STRVAR(xx_foo_doc, "Some description"); //!!!

/*obtain C plugin handle and key from pyCobject */
bool getPluginHandle 
(PyObject *pyPluginHandle, Plugin **plugin, string **pKey=NULL) {

    //char errormsg[]="Wrong input argument: Plugin Handle required.";

    *plugin = NULL;
    if (!PyCObject_Check(pyPluginHandle)) return false;

    //try to convert to Plugin pointer
    Plugin *p = (Plugin*) PyCObject_AsVoidPtr(pyPluginHandle);
    if (!p) return false;	

    string pId;

    if (pKey) {
	*pKey = (string*) PyCObject_GetDesc(pyPluginHandle);
	if (!*pKey) return false;
	pId = *(string*) *pKey;

    } else {

	void *pKey = PyCObject_GetDesc(pyPluginHandle);
	if (!pKey) return false;
	pId = *(string*) pKey;
    }
	
    string::size_type pos = pId.find(':');
    if (pos == string::npos) return false;

    pId = pId.substr(pId.rfind(':')+1);
    string identifier = p->getIdentifier();	
			
    if (pId.compare(identifier)) return false;

    *plugin = p;
    return true;
}

/*
  ----------------------------------------------------------------
*/



/*
  VAMPYHOST MAIN 
  --------------------------------------------------------------------- 
*/

/* ENUMERATE PLUGINS*/

static PyObject *
vampyhost_enumeratePlugins(PyObject *self, PyObject *args)
{
    string retType;
	
    if (!PyArg_ParseTuple(args, "|s:enumeratePlugins", &retType))
	return NULL;

    //list available plugins
    PluginLoader *loader = PluginLoader::getInstance();
    vector<PluginLoader::PluginKey> plugins = loader->listPlugins();

    //library Map
    typedef multimap<string, PluginLoader::PluginKey> LibraryMap;
    LibraryMap libraryMap;

    //New list object
    PyObject *pyList = PyList_New(plugins.size());

    for (size_t i = 0; i < plugins.size(); ++i) {
        string path = loader->getLibraryPathForPlugin(plugins[i]);
        libraryMap.insert(LibraryMap::value_type(path, plugins[i]));

	PyObject *pyPluginKey = PyString_FromString(plugins[i].c_str());
	PyList_SET_ITEM(pyList,i,pyPluginKey);
		
    }

    PyList_Sort(pyList);
    return pyList;
}


/* GET PLUGIN LIBRARY PATH*/

static PyObject *
vampyhost_getLibraryPath(PyObject *self, PyObject *args)
{
    PyObject *pyPluginKey;

    if (!PyArg_ParseTuple(args, "S", &pyPluginKey)) {
	PyErr_SetString(PyExc_TypeError,
			"String input argument required: pluginKey");
	return NULL; }

    //convert to stl string
    string pluginKey(PyString_AS_STRING(pyPluginKey));

    //check pluginKey Validity
    string::size_type ki = pluginKey.find(':');
    if (ki == string::npos) {
	PyErr_SetString(PyExc_TypeError,
			"String input argument required: pluginLibrary:Identifier"); 
       	return NULL;
    }

    PluginLoader *loader = PluginLoader::getInstance();
    string path = loader->getLibraryPathForPlugin(pluginKey);
    PyObject *pyPath = PyString_FromString(path.c_str());
    return pyPath;
}


/* GET PLUGIN CATEGORY*/

static PyObject *
vampyhost_getPluginCategory(PyObject *self, PyObject *args)
{
    PyObject *pyPluginKey;

    if (!PyArg_ParseTuple(args, "S", &pyPluginKey)) {
	PyErr_SetString(PyExc_TypeError,
			"String input argument required: pluginKey");
	return NULL; }

    //convert to stl string
    string pluginKey(PyString_AS_STRING(pyPluginKey));

    //check pluginKey Validity
    string::size_type ki = pluginKey.find(':');
    if (ki == string::npos) {
	PyErr_SetString(PyExc_TypeError,
			"String input argument required: pluginLibrary:Identifier"); 
       	return NULL;
    }

    PluginLoader *loader = PluginLoader::getInstance();
    PluginLoader::PluginCategoryHierarchy 
	category = loader->getPluginCategory(pluginKey);
    string catstring;

    if (!category.empty()) {
        catstring = "";
        for (size_t ci = 0; ci < category.size(); ++ci) {
	    catstring.append(category[ci]);
            catstring.append(" ");
        }
	PyObject *pyCat = PyString_FromString(catstring.c_str());
	return pyCat;
    }
    PyObject *pyCat = PyString_FromString("");
    return pyCat;
}



/* GET PLUGIN OUTPUT LIST*/

static PyObject *
vampyhost_getOutputList(PyObject *self, PyObject *args)
{
    PyObject *pyPluginHandle;
    string pluginKey;

    if (!PyArg_ParseTuple(args, "O", &pyPluginHandle)) {
	PyErr_SetString(PyExc_TypeError,
			"Invalid argument: plugin handle or plugin key required.");
	return NULL; 
    }

    //check if we have a plugin key string or a handle object
    if (PyString_Check(pyPluginHandle) ) {

	pluginKey.assign(PyString_AS_STRING(pyPluginHandle));
	//check pluginKey Validity
    	string::size_type ki = pluginKey.find(':');
    	if (ki == string::npos) {
	    PyErr_SetString(PyExc_TypeError,
			    "String input argument required: pluginLibrary:Identifier"); 
	    return NULL;
    	}

    } else {
		
	string *key;	
	Plugin *plugin; 

	if ( !getPluginHandle(pyPluginHandle, &plugin, &key) ) {
	    PyErr_SetString(PyExc_TypeError,
			    "Invalid or deleted plugin handle.");
	    return NULL; }
	pluginKey.assign(*key);
    } 
	
    //This code creates new instance of the plugin anyway
    PluginLoader *loader = PluginLoader::getInstance();
	
    //load plugin
    Plugin *plugin = loader->loadPlugin (pluginKey, 48000);
    if (!plugin) { 		
	string pyerr("Failed to load plugin: "); pyerr += pluginKey;
	PyErr_SetString(PyExc_TypeError,pyerr.c_str()); 
	return NULL;
    }		

    Plugin::OutputList outputs = plugin->getOutputDescriptors();
    //Plugin::OutputDescriptor od;

    if (outputs.size()<1) {
	string pyerr("Plugin has no output: "); pyerr += pluginKey;
	PyErr_SetString(PyExc_TypeError,pyerr.c_str()); 
	return NULL;
    }

    //New list object
    PyObject *pyList = PyList_New(outputs.size());

    for (size_t i = 0; i < outputs.size(); ++i) {
	PyObject *pyOutputId = 
	    PyString_FromString(outputs[i].identifier.c_str());
	PyList_SET_ITEM(pyList,i,pyOutputId);		
    }

    delete plugin;
    return pyList;
}



/* LOAD PLUGIN */

static PyObject *
vampyhost_loadPlugin(PyObject *self, PyObject *args)
{
    PyObject *pyPluginKey;
    float inputSampleRate;

    if (!PyArg_ParseTuple(args, "Sf", 
			  &pyPluginKey,
			  &inputSampleRate)) {
	PyErr_SetString(PyExc_TypeError,
			"String input argument required: pluginKey");
	return NULL; }

    //convert to stl string
    string pluginKey(PyString_AS_STRING(pyPluginKey));

    //check pluginKey Validity
    string::size_type ki = pluginKey.find(':');
    if (ki == string::npos) {
	PyErr_SetString(PyExc_TypeError,
			"String input argument required: pluginLibrary:Identifier"); 
       	return NULL;
    }

    PluginLoader *loader = PluginLoader::getInstance();
	
    //load plugin
    Plugin *plugin = loader->loadPlugin (pluginKey, inputSampleRate);
    if (!plugin) { 		
	string pyerr("Failed to load plugin: "); pyerr += pluginKey;
	PyErr_SetString(PyExc_TypeError,pyerr.c_str()); 
	return NULL;
    }		
    //void *identifier = (void*) new string(pluginKey);
    PyPluginDescriptor *pd = new PyPluginDescriptor;
	
    pd->key = pluginKey;
    pd->isInitialised = false;
    pd->inputSampleRate = inputSampleRate;
	
    //New PyCObject
    //PyObject *pyPluginHandle = PyCObject_FromVoidPtrAndDesc( 
    //(void*) plugin, identifier, NULL);	

    PyObject *pyPluginHandle = PyCObject_FromVoidPtrAndDesc( 
	(void*) plugin, (void*) pd, NULL);	
		
    return pyPluginHandle;
}



/* UNLOAD PLUGIN */

static PyObject *
vampyhost_unloadPlugin(PyObject *self, PyObject *args)
{
    PyObject *pyPluginHandle;

    if (!PyArg_ParseTuple(args, "O", &pyPluginHandle)) {
	PyErr_SetString(PyExc_TypeError,
			"Wrong input argument: Plugin Handle required.");
	return NULL; }

    string *key;	
    Plugin *plugin; 

    if ( !getPluginHandle(pyPluginHandle, &plugin, &key) ) {
	PyErr_SetString(PyExc_TypeError,
			"Invalid or already deleted plugin handle.");
	return NULL; }

/*	Prevent repeated calls from causing segfault
	sice it will fail type checking the 2nd time:						*/
    PyCObject_SetVoidPtr(pyPluginHandle,NULL);
	
    PyPluginDescriptor *pd = (PyPluginDescriptor*) key;
	
    delete plugin;
    delete pd;
    return pyPluginHandle;

}


/* INITIALISE PLUGIN */

static PyObject *
vampyhost_initialise(PyObject *self, PyObject *args)
{
    PyObject *pyPluginHandle;
    size_t channels,blockSize,stepSize;
    //PyObject pyInputSampleType;
    bool mixChannels = false;

    if (!PyArg_ParseTuple (args, "Oiii",  &pyPluginHandle, 
			   (size_t) &channels, 
			   (size_t) &stepSize, 
			   (size_t) &blockSize)) 
    {
	PyErr_SetString(PyExc_TypeError,
			"Wrong input arguments: requires a valid plugin handle,channels,stepSize,blockSize.");
	return NULL; 
    }

    Plugin *plugin; 
    string *key; 

    if ( !getPluginHandle(pyPluginHandle, &plugin, &key) ) {
	PyErr_SetString(PyExc_TypeError,
			"Invalid plugin handle.");
	return NULL; }

    // here we cast the void pointer as PyPluginDescriptor instead of string
    PyPluginDescriptor *plugDesc = (PyPluginDescriptor*) key;	

    plugDesc->channels = channels;
    plugDesc->stepSize = stepSize;
    plugDesc->blockSize = blockSize;
    plugDesc->inputSampleType = PyPluginDescriptor::int16;
    plugDesc->sampleSize = 2;
    plugDesc->mixChannels = mixChannels; 
	
    size_t minch = plugin->getMinChannelCount();
    size_t maxch = plugin->getMaxChannelCount();
    if (mixChannels) channels = 1;

    /* TODO: DO WE WANT TO MIX IT DOWN? */
    if (maxch < channels || channels < minch) {
	PyErr_SetString(PyExc_TypeError,
			"Invalid number of channels.");
	return NULL; }

    if (!plugin->initialise(channels, stepSize, blockSize)) {
	PyErr_SetString(PyExc_TypeError,
			"Plugin initialization failed.");
	return NULL; }

    plugDesc->identifier = 
	plugDesc->key.substr(plugDesc->key.rfind(':')+1);
    plugDesc->isInitialised = true;
	
    return Py_True;
}


/* RUN PROCESS */

static PyObject *
vampyhost_process(PyObject *self, PyObject *args)
{
    PyObject *pyPluginHandle;
    PyObject *pyBuffer;
    PyObject *pyRealTime;

    if (!PyArg_ParseTuple(args, "OOO", 
			  &pyPluginHandle,	// C object holding a pointer to a plugin and its descriptor
			  &pyBuffer,			// Audio data
			  &pyRealTime)) {		// TimeStamp
	PyErr_SetString(PyExc_TypeError,
			"Required: plugin handle, buffer, timestmap.");
	return NULL; }

    if (!PyRealTime_Check(pyRealTime)) {
	PyErr_SetString(PyExc_TypeError,"Valid timestamp required.");
	return NULL; }

    // RealTime *rt = PyRealTime_AsPointer(pyRealTime);
    // if (!rt) return NULL;
    // cerr << ">>>sec: " << rt->sec << " nsec: " << rt->nsec << endl;
    // 
    // PyObject *rrt = PyRealTime_FromRealTime (rt);

    string *key;	
    Plugin *plugin; 

    if ( !getPluginHandle(pyPluginHandle, &plugin, &key) ) {
	PyErr_SetString(PyExc_AttributeError,
			"Invalid or already deleted plugin handle.");
	return NULL; }

    PyPluginDescriptor *pd = (PyPluginDescriptor*) key;

    if (!pd->isInitialised) {
	PyErr_SetString(PyExc_StandardError,
			"Plugin has not been initialised.");
	return NULL; }

    size_t channels =  pd->channels;	
    size_t blockSize = pd->blockSize;

/*
  Handle the case when we get the data as a character buffer
  Handle SampleFormats: int16, float32	

*/		

    if (PyString_Check(pyBuffer)) {
	cerr << ">>> String obj passed in." << endl;
    }

    size_t sample_size = sizeof(short);

    long buflen = (long) PyString_GET_SIZE(pyBuffer);

    size_t input_length = 
	static_cast <size_t> (buflen/channels/sample_size);

    if (input_length == pd->blockSize) {
	cerr << ">>> A full block has been passed in." << endl; }
    short *input = 
	reinterpret_cast <short*> (PyString_AS_STRING(pyBuffer));

    //convert int16 PCM data to 32-bit floats
    float **plugbuf = new float*[channels];
    float normfact = 1.0f / static_cast <float> (SHRT_MAX);
		
    for (size_t c = 0; c < channels; ++c) {

	plugbuf[c] = new float[blockSize+2];
  
      	size_t j = 0;
        while (j < input_length) {
	    plugbuf[c][j] = normfact *
		static_cast <float> (input[j * channels + c]);
	    ++j; 
        }
        while (j < blockSize) {
            plugbuf[c][j] = 0.0f;
            ++j;
        }
    }	

    const char *output = reinterpret_cast <const char*> (plugbuf[0]);
    Py_ssize_t len = (Py_ssize_t) channels*blockSize*4;
	
    PyObject* pyReturnBuffer = 
	PyString_FromStringAndSize(output,len);

    // long frame = 1;
    // unsigned int samplerate = (unsigned int) pd->inputSampleRate;

    RealTime timeStamp = *PyRealTime_AsPointer(pyRealTime);

    //Call process and store the output
    pd->output = plugin->process(
	plugbuf, 
	timeStamp);

    /* TODO:  DO SOMETHONG WITH THE FEATURE SET HERE */
/// convert to appropriate python objects, reuse types and conversion utilities from Vampy ...


    //We can safely delete here
    for(size_t k=0; k<channels; k++){
	delete[] plugbuf[k];
    }
    delete[] plugbuf;

    return pyReturnBuffer;

}

/* GET / SET OUTPUT */

//getOutput(plugin,outputNo)
static PyObject *
vampyhost_getOutput(PyObject *self, PyObject *args) {

    PyObject *pyPluginHandle;
//	PyObject *pyBuffer;
//	PyObject *pyRealTime;
    PyObject *pyOutput;

    if (!PyArg_ParseTuple(args, "OO", 
			  &pyPluginHandle,	// C object holding a pointer to a plugin and its descriptor
			  &pyOutput)) {		// Output reference
	PyErr_SetString(PyExc_TypeError,
			"Required: plugin handle, buffer, timestmap.");
	return NULL; }

    string *key;	
    Plugin *plugin; 

    if ( !getPluginHandle(pyPluginHandle, &plugin, &key) ) {
	PyErr_SetString(PyExc_AttributeError,
			"Invalid or already deleted plugin handle.");
	return NULL; }

    PyPluginDescriptor *pd = (PyPluginDescriptor*) key;

    unsigned int outputNo = (unsigned int) PyInt_AS_LONG(pyOutput);

    //Get output list: but we don't need it
    //Plugin::FeatureList features = pd->output[outputNo];

    size_t outLength = pd->output[outputNo].size();

    //New PyList for the featurelist
    PyObject *pyFeatureList = PyList_New(outLength);

    for (size_t i = 0; i < outLength; ++i) {
	// Test:
	/*
	  XxoObject *pyFeature = PyObject_New(XxoObject, &Xxo_Type); 
	  if (pyFeature == NULL) break; //return NULL;

	  pyFeature->x_attr = NULL;
	  pyFeature->feature = &pd->output[outputNo][i];

	  PyList_SET_ITEM(pyFeatureList,i,(PyObject*)pyFeature);		
	*/
    }

    Py_INCREF(pyFeatureList);
    return pyFeatureList;

// EXPLAIN WHAT WE NEED TO DO HERE:
// We have the block output in pd->output
// FeatureSet[output] -> [Feature[x]] -> Feature.hasTimestamp = v	
// Vamp::Plugin::FeatureSet output; = pd->output	
// typedef std::vector<Feature> FeatureList;
// typedef std::map<int, FeatureList> FeatureSet; // key is output no

    // 	THIS IS FOR OUTPUT id LOOKUP LATER 
    //     Plugin::OutputList outputs = plugin->getOutputDescriptors();
    // 
    // if (outputs.size()<1) {
    // 	string pyerr("Plugin has no output: "); pyerr += pluginKey;
    // 	PyErr_SetString(PyExc_TypeError,pyerr.c_str()); 
    // 	return NULL;
    // }
    // 
    // //New list object
    // PyObject *pyList = PyList_New(outputs.size());
    // 
    //     for (size_t i = 0; i < outputs.size(); ++i) {
    // 	PyObject *pyOutputId = 
    // 	PyString_FromString(outputs[i].identifier.c_str());
    // 	PyList_SET_ITEM(pyList,i,pyOutputId);		
    //     }
	
}




/* List of functions defined in this module */
//module methods table
static PyMethodDef vampyhost_methods[] = {

    {"enumeratePlugins",	vampyhost_enumeratePlugins,	METH_VARARGS,
     xx_foo_doc},

    {"getLibraryPath",	vampyhost_getLibraryPath, METH_VARARGS,
     xx_foo_doc},

    {"getPluginCategory",	vampyhost_getPluginCategory, METH_VARARGS,
     xx_foo_doc},

    {"getOutputList",	vampyhost_getOutputList, METH_VARARGS,
     xx_foo_doc},

    {"loadPlugin",	vampyhost_loadPlugin, METH_VARARGS,
     xx_foo_doc},

    {"process",	vampyhost_process, METH_VARARGS,
     xx_foo_doc},

    {"unloadPlugin",	vampyhost_unloadPlugin, METH_VARARGS,
     xx_foo_doc},

    {"initialise",	vampyhost_initialise, METH_VARARGS,
     xx_foo_doc},

    {"getOutput",	vampyhost_getOutput, METH_VARARGS,
     xx_foo_doc},

    /* Add RealTime Module Methods */

    {"frame2RealTime",	(PyCFunction)RealTime_frame2RealTime,	METH_VARARGS,
     PyDoc_STR("frame2RealTime((int64)frame, (uint32)sampleRate ) -> returns new RealTime object from frame.")},

    {"realtime",	(PyCFunction)RealTime_new,		METH_VARARGS,
     PyDoc_STR("realtime() -> returns new RealTime object")},
			
    {NULL,		NULL}		/* sentinel */
};

//Documentation for our new module
PyDoc_STRVAR(module_doc, "This is a template module just for instruction.");

/* Initialization function for the module (*must* be called initxx) */

//module initialization (includes extern C {...} as necessary)
PyMODINIT_FUNC
initvampyhost(void)
{
    PyObject *m;

    /* Finalize the type object including setting type of the new type
     * object; doing it here is required for portability to Windows 
     * without requiring C++. */

    if (PyType_Ready(&RealTime_Type) < 0)
	return;
//	PyModule_AddObject(m, "Real_Time", (PyObject *)&RealTime_Type);

    /* Create the module and add the functions */
    m = Py_InitModule3("vampyhost", vampyhost_methods, module_doc);
    if (m == NULL) return;

    // PyModule_AddObject(m, "realtime", (PyObject *)&RealTime_Type);

}
