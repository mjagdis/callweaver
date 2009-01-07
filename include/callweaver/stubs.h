/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - Navynet SRL
 *
 * Massimo Cetra <devel@navynet.it>
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

// This is not meant to work. It's a request for comments.

enum cw_stubfunction_return {
    CW_RETURN_SUCCESS,
    CW_RETURN_FAILURE,
    CW_RETURN_NOT_SUPPORTED,
};

typedef enum cw_stubfunction_return cw_stubfunction_return_t;



enum cw_stubfunction_types {
    CW_STUNFUNCTION_PRIVATE,            // It's code only. 
    CW_STUNFUNCTION_PUBLIC,             // It will be exported to the external world 
                                        // (xmlrpc, telnet, whatever you like)
};

typedef enum cw_stubfunction_types cw_stubfunction_types_t;



struct cw_stubfunction_type_s {
    char                        *function_name;
    char                        *function_api;          // A string like "s/NAME i/timeout d/WHENTOHANGUP" 
                                                        // where s/ stands for string, i/for integer, d/ for date
                                                        //
    char                        *description;

    char			*ret_type;		// What type of return to expect from stub
    cw_stubfunction_types_t     type;
    cw_stubfunction_return_t    (*implementation)( int argc, char **argv, char *output );
}

typedef struct cw_stubfunction_type_s cw_stubfunction_type_t;

/* ***************************************************************** */

//
// The internal list of registered stub functions is stored in an hashtable
//

extern CW_API_PUBLIC int cw_stub_function_register( cw_stubfunction_type_t *function );
extern CW_API_PUBLIC int cw_stub_function_unregister( cw_stubfunction_type_t *function );

extern CW_API_PUBLIC cw_stubfunction_type_t *cw_stub_lookup( char *name );

extern CW_API_PUBLIC cw_stubfunction_type_t *cw_stub_walk_first( Something );
extern CW_API_PUBLIC cw_stubfunction_type_t *cw_stub_walk_next ( Something );







#if 0  

/*  ***************************************************************** 
                             QUICK EXAMPLE
    ***************************************************************** */

// We write our function
static cw_stubfunction_return_t  channel_kill( int argc, char **argv, char **output ) {
    // Here goes the code
    return CW_RETURN_SUCCESS;
}

// We define our struct
cw_stubfunction_type_t stub_test {
    /* name */          "channel_kill",
    /* api */           "s/CHAN_NAME",
    /* desc */          "This method will kill the channel identified by CHAN_NAME, if exists.",
    /* type*/           CW_STUNFUNCTION_PUBLIC,
    /* function */      channel_kill
};


void register_me(void) {
    cw_stub_function_register( &stub_test );
}

/*
A module to interface to the outside world will accept calls for this method.
It will parse the "function_api" string to give the user the proper API on how to call it 
(so if it is SOAP it will generate a WSDL, if it is telnet it will eventually reformat 
it so that it can be readable)

When the external interface will get it's data, it will be responsible of parsing those 
values, convert them to a suitable format and call the function itself.

The function will return an enum cw_stubfunction_return AND, if necessary, a char * output 
containing the details of what has been done.

*/


#endif

/*  ***************************************************************** 
                          ALTERNATE APPROACH
    ***************************************************************** */

extern CW_API_PUBLIC void *cw_stubfunction_getarg(cw_stubfunction_arg_t args, cw_stubfunction_argtype_t ARG_TYPE, ARG_INDEX)
extern CW_API_PUBLIC boolean cw_stubfunction_testarg(cw_stubfunction_arg_t args, cw_stubfunction_argtype_t ARG_TYPE, ARG_INDEX)

enum cw_stubfunction_argtype {
    TYPE_NULL,	// Used if optional arg and no value supplied
    TYPE_INT,
    TYPE_STRING,
    TYPE_BOOL
};
typedef enum cw_stubfunction_argtype cw_stubfunction_argtype_t;

struct cw_stubfunction_arg_s {
  cw_stubfunction_argtype_t arg_t;    // Type of arg
  void *arg_v;  // Actual arg
};
typedef struct cw_stubfunction_arg_s cw_stubfunction_arg_t;
typedef cw_stubfunction_return_t cw_stubfunction_arg_t;

struct cw_stubfunction_args_s {
  int argc;			// number of args
  cw_stubfunction_arg_t *args;	// args
};
typedef struct cw_stubfunction_args_s cw_stubfunction_args_t;



#if 0

/*  ***************************************************************** 
                     QUICK EXAMPLE - ALTERNATE
    ***************************************************************** */

static cw_stubfunction_return_t *channel_kill(cw_stubfunction_args_t *args) {
  char *chan;
  cw_stubfunction_return_t *retval = malloc(sizeof(cw_stubfunction_return_t));
  retval->return_t = TYPE_BOOL;
  retval->return_v = false;

  /* Optional internal testing of an argument - we should be able to trust upstream API
  if (!cw_stubfunction_testarg(args, TYPE_STRING, 0))
    return retval; // set false already
  */
  chan = cw_stubfunction_getarg(args, TYPE_STRING, 0);

  if (cw_stubfunction_testarg(args, TYPE_INT, 1));
    // do stuff with timeout variable

  // Code here
  retval->return_v = true;
  return retval;
}

// We define our struct
cw_stubfunction_type_t stub_test {
    /* name */          "channel_kill",
    /* api */           "s/CHAN_NAME;oi/timeout", // CHAN_NAME = 0, timeout = 1
    /* desc */          "This method will kill the channel identified by CHAN_NAME, if exists.",
    /* ret_type */      "b/RESULT",
    /* type*/           CW_STUNFUNCTION_PUBLIC,
    /* function */      channel_kill
};


void register_me(void) {
    cw_stub_function_register( &stub_test );
}
#endif
