/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version                        *
 *                                                                         *
 ***************************************************************************/

#include <icd_module_api.h>
#include <icd_common.h>
#ifdef __APPLE__
#include <openpbx/dlfcn-compat.h>
#else
#include <dlfcn.h>
#endif
#include <icd_globals.h>
#include <icd_caller.h>
#include <dirent.h>

static void_hash_table *loaded_modules;

AST_MUTEX_DEFINE_STATIC(modlock);

static int icd_module_load_from_file(char *filename, icd_config_registry * registry)
{
    icd_loadable_object *module;
    int errcnt = 0;
    int res = 0;

    assert(filename != NULL);

    ast_mutex_lock(&modlock);

    if (!loaded_modules)
        loaded_modules = vh_init("LOADED_MODULES");
    module = vh_read(loaded_modules, filename);
    if (module) {
        opbx_log(LOG_WARNING, "Already Loaded\n");
        ast_mutex_unlock(&modlock);
        return -1;
    } else
        module = NULL;

    ICD_MALLOC(module, sizeof(icd_loadable_object));
    strncpy(module->filename, filename, sizeof(module->filename));
    module->lib = dlopen(filename, RTLD_GLOBAL | RTLD_LAZY);
    if (!module->lib) {
        opbx_log(LOG_WARNING, "Error loading module %s, aborted %s\n", filename, dlerror());
        ICD_FREE(module);
        ast_mutex_unlock(&modlock);
        return -1;
    }

    module->load_fn = dlsym(module->lib, "icd_module_load");
    if (module->load_fn == NULL) {
        errcnt++;
        opbx_log(LOG_WARNING, "No 'icd_module_load' function found in module [%s]\n", filename);
    }

    module->unload_fn = dlsym(module->lib, "icd_module_unload");
    if (module->unload_fn == NULL) {
        errcnt++;
        opbx_log(LOG_WARNING, "No 'icd_module_unload' function found in module [%s]\n", filename);
    }

    if (errcnt) {
        dlclose(module->lib);
        ICD_FREE(module);
        ast_mutex_unlock(&modlock);
        return -1;
    }

    vh_write(loaded_modules, filename, module);
    ast_mutex_unlock(&modlock);

    if ((res = module->load_fn(registry))) {
        opbx_log(LOG_WARNING, "Error loading module %s\n", filename);
        ast_mutex_lock(&modlock);
        vh_delete(loaded_modules, filename);
        dlclose(module->lib);
        ICD_FREE(module);
        ast_mutex_unlock(&modlock);
        return -1;
    }

    return 0;

}

//int icd_module_dynamic_load(icd_config_registry *registry) {
/* this is called from app_icd.c->load_module                */
icd_status icd_module_load_dynamic_module(icd_config_registry * registry)
{
    static char *mydir = "/usr/lib/icd";
    char file[512];
    DIR *dir;
    struct dirent *de;
    char *ptr;
    int pos;

    dir = opendir(mydir);
    if (!dir) {
        opbx_log(LOG_WARNING, "Can't open directory: %s\n", mydir);
        return -1;
    }
    while ((de = readdir(dir))) {
        ptr = de->d_name;
        pos = strlen(ptr) - 2;
        ptr += pos;
        if (!strncasecmp(ptr, "so", 2)) {
            snprintf(file, 512, "%s/%s", mydir, de->d_name);
            //if (strcasecmp(file, "custom") == 0) {
            icd_module_load_from_file(file, registry);

        }
    }
    closedir(dir);

    return ICD_SUCCESS;
}

icd_status icd_module_unload_dynamic_modules()
{
    icd_loadable_object *module;
    vh_keylist *keys = vh_keys(loaded_modules), *key;
    icd_status result;

    ast_mutex_lock(&modlock);

    for (key = keys; key; key = key->next) {
        module = vh_read(loaded_modules, key->name);
        if (module) {
            /*opbx_log(LOG_NOTICE,"Module[%s] File[%s] UnLoaded\n",key->name,module->filename); */
            if (module->unload_fn != NULL) {
                module->unload_fn();
            } else {
                /*opbx_log(LOG_WARNING, "No 'icd_module_unload' function found in Module:[%s] File:[%s]\n",
                   key->name,module->filename);
                 */
                result = ICD_SUCCESS;
            }

            vh_delete(loaded_modules, module->filename);
            dlclose(module->lib);
            ICD_FREE(module);
        } else {
            /*opbx_log(LOG_WARNING,"wack vh_read from loadable_module hash and no module object found ... \n");
             */
            module = NULL;
        }
    }

    vh_destroy(&loaded_modules);
    ast_mutex_unlock(&modlock);

    return ICD_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * indent-tabs-mode:nil
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */

