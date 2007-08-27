
enum cw_stubfunction_return {
    CW_RETURN_SUCCESS,
    CW_RETURN_FAILURE,
    CW_RETURN_NOT_SUPPORTED,
};

typedef enum cw_stubfunction_return cw_stubfunction_return_t;

enum cw_stubfunction_types {
    CW_STUNFUNCTION_PRIVATE,            // It's code only. 
    CW_STUNFUNCTION_PUBLIC,             // It will be exported to the external world (xmlrpc, telnet, whatever you like)
};

typedef enum cw_stubfunction_types cw_stubfunction_types_t;

struct cw_stubfunction_type_s {
    char                        *function_name;
    char                        *function_api;          // A string like "s/NAME i/timeout d/WHENTOHANGUP" 
                                                        // where s/ stands for string, i/for integer, d/ for date
    char                        *description;

    cw_stubfunction_types_t     type;
    cw_stubfunction_return_t    (*implementation)( int argc, char **argv, char *output );

}

typedef struct cw_stubfunction_type_s cw_stubfunction_type_t;

/* ***************************************************************** */

//
// The internal list of registered stub functions is stored in an hashtable
//

int cw_stub_function_register( cw_stubfunction_type_t *function );
int cw_stub_function_unregister( cw_stubfunction_type_t *function );

cw_stubfunction_type_t *cw_stub_lookup( char *name );

cw_stubfunction_type_t *cw_stub_walk_first( Something );
cw_stubfunction_type_t *cw_stub_walk_next ( Something );







#if 0  

/*  ***************************************************************** 
                             QUICK EXAMPLE
    ***************************************************************** */

// We write our function
static cw_stubfunction_return_t  channel_kill( int argc, char **argv, char *output ) {
    // Here goes the code
    return CW_RETURN_SUCCESS;
}

// We define our struct
cw_stubfunction_type_t stub_test {
    "channel_kill",
    "s/CHAN_NAME",
    "This method will kill the channel identified by CHAN_NAME, if exists.",
    CW_STUNFUNCTION_PUBLIC,
    channel_kill
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
