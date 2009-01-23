#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif


#if HAVE_OLD_SETAFFINITY
#  define HAVE_SETAFFINITY
#  define sched_getaffinity(pid, cpusetsize, mask) sched_getaffinity(pid, mask)
#  define sched_setaffinity(pid, cpusetsize, mask) sched_setaffinity(pid, mask)
#endif


#if defined _WIN32 || defined __CYGWIN__
#  define DLL_PUBLIC_EXPORT __declspec(dllexport)
#  define DLL_PUBLIC_IMPORT __declspec(dllimport)
#  define DLL_PRIVATE
#else
#  if HAVE_VISIBILITY
#    define DLL_PUBLIC_EXPORT __attribute__ ((visibility("default")))
#    define DLL_PUBLIC_IMPORT __attribute__ ((visibility("default")))
#    define DLL_PRIVATE       __attribute__ ((visibility("hidden")))
#  else
#    define DLL_PUBLIC_EXPORT
#    define DLL_PUBLIC_IMPORT
#    define DLL_PRIVATE
#  endif
#endif


#ifdef CW_API_IMPLEMENTATION
#  define CW_API_PUBLIC DLL_PUBLIC_EXPORT
#else
#  define CW_API_PUBLIC DLL_PUBLIC_IMPORT
#endif


#ifdef CW_RES_API_IMPLEMENTATION
#  define CW_RES_API_PUBLIC DLL_PUBLIC_EXPORT
#else
#  define CW_RES_API_PUBLIC DLL_PUBLIC_IMPORT
#endif
