/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

/*! \file
 * \brief Device state management
 */

#ifndef _CALLWEAVER_DEVICESTATE_H
#define _CALLWEAVER_DEVICESTATE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


typedef enum {
    /*! FAILURE WHEN RETURNING THIS ENUM */
    CW_DEVICE_FAILURE = -1,
    /*! Device is valid but channel didn't know state */
    CW_DEVICE_UNKNOWN = 0,
    /*! Device is not used */
    CW_DEVICE_NOT_INUSE = 1,
    /*! Device is in use */
    CW_DEVICE_INUSE = 2,
    /*! Device is busy */
    CW_DEVICE_BUSY = 3,
    /*! Device is invalid */
    CW_DEVICE_INVALID = 4,
    /*! Device is unavailable */
    CW_DEVICE_UNAVAILABLE = 5,
    /*! Device is ringing */
    CW_DEVICE_RINGING = 6
} cw_devicestate_t;

typedef int (*cw_devstate_cb_type)(const char *dev, int state, void *data);

/*! \brief Convert device state to text string for output 
 * \param devstate Current device state 
 */
extern CW_API_PUBLIC const char *devstate2str(cw_devicestate_t devstate);

/*! \brief Asks a channel for device state
 * \param device like a dialstring
 * Asks a channel for device state, data is  normaly a number from dialstring
 * used by the low level module
 * Trys the channel devicestate callback if not supported search in the
 * active channels list for the device.
 * Returns an CW_DEVICE_??? state -1 on failure
 */
extern CW_API_PUBLIC cw_devicestate_t cw_device_state(const char *device);

/*! \brief Tells CallWeaver that the State for a Device has changed
 * \param fmt devicename like a dialstring with format parameters
 * CallWeaver polls the new extensionstates and calls the registered
 * callbacks for the changed extensions
 * Returns 0 on success, -1 on failure
 */
extern CW_API_PUBLIC int cw_device_state_changed(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));


/*! \brief Tells CallWeaver the State for Device is changed 
 * \param device devicename like a dialstrin
 * CallWeaver polls the new extensionstates and calls the registered
 * callbacks for the changed extensions
 * Returns 0 on success, -1 on failure
 */
extern CW_API_PUBLIC int cw_device_state_changed_literal(const char *device);

/*! \brief Registers a device state change callback 
 * \param data to pass to callback
 * The callback is called if the state for extension is changed
 * Return -1 on failure, ID on success
 */ 
extern CW_API_PUBLIC int cw_devstate_add(cw_devstate_cb_type callback, void *data);
extern CW_API_PUBLIC void cw_devstate_del(cw_devstate_cb_type callback, void *data);

int cw_device_state_engine_init(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_DEVICESTATE_H */
