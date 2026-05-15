/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * os_tr181 - Cross-platform TR-181 Parameter Access Library
 *
 * This library provides a unified API for accessing TR-181 parameters
 * across different platforms (prplOS/Ambiorix, RDK-B/CCSP).
 */

#ifndef OS_TR181_H
#define OS_TR181_H

#include "os_tr181_types.h"
#include "os_tr181_val.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default timeout for synchronous operations (milliseconds) */
#define OS_TR181_DEFAULT_TIMEOUT_MS 5000

/* Parameter value structure */
typedef struct
{
    char *name;
    char *value;
    os_tr181_param_type_t type;
} os_tr181_param_t;

/* Parameter info structure (for listing) */
typedef struct
{
    char *name;
    int writable;
    os_tr181_param_type_t type;
} os_tr181_param_info_t;

/* Opaque handle for library context */
typedef struct os_tr181_handle_s os_tr181_handle_t;

/* Opaque handle for subscription */
typedef struct os_tr181_subscription_s *os_tr181_sub_handle_t;

/*
 * Initialize TR-181 library
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_init(os_tr181_handle_t **handle);

/*
 * Close TR-181 library and free resources
 *
 * Parameters:
 *   handle - Library handle
 */
void os_tr181_close(os_tr181_handle_t *handle);

/*
 * Get a TR-181 parameter value (with type)
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path (e.g., "Device.DeviceInfo.ModelName")
 *   value - Pointer to os_tr181_val_t to receive the value
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Note: The value structure is populated with the parameter's value and type.
 *       For string types, memory is allocated and must be freed with os_val_free().
 */
os_tr181_error_t os_tr181_get_val(os_tr181_handle_t *handle, const char *param_name, os_tr181_val_t *value);

/*
 * Get a TR-181 integer parameter value
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path
 *   value - Pointer to receive integer value
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_get_int(os_tr181_handle_t *handle, const char *param_name, int *value);

/*
 * Get a TR-181 string parameter value
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path
 *   value - Pointer to receive string value (caller must free)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_get_str(os_tr181_handle_t *handle, const char *param_name, char **value);

/*
 * Get a TR-181 64-bit integer parameter value
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path
 *   value - Pointer to receive 64-bit integer value
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_get_int64(os_tr181_handle_t *handle, const char *param_name, int64_t *value);

/*
 * Set a TR-181 parameter value (with type)
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path
 *   value - Pointer to os_tr181_val_t containing value and type
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Note: The value structure should be initialized with the desired value and type.
 */
os_tr181_error_t os_tr181_set_val(os_tr181_handle_t *handle, const char *param_name, const os_tr181_val_t *value);

/*
 * Set a TR-181 integer parameter value
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path
 *   value - Integer value to set
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_set_int(os_tr181_handle_t *handle, const char *param_name, int value);

/*
 * Set a TR-181 string parameter value
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path
 *   value - String value to set
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_set_str(os_tr181_handle_t *handle, const char *param_name, const char *value);

/*
 * Set a TR-181 64-bit integer parameter value
 *
 * Parameters:
 *   handle - Library handle
 *   param_name - Full parameter path
 *   value - 64-bit integer value to set
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_set_int64(os_tr181_handle_t *handle, const char *param_name, int64_t value);

/*
 * List parameters under a TR-181 path
 *
 * Parameters:
 *   handle - Library handle
 *   path - Parameter path (e.g., "Device.WiFi.SSID.1.")
 *   recursive - true for recursive listing (all sub-levels), false for next level only
 *   params - Pointer to receive array of parameter info (caller must free with os_tr181_free_list)
 *   count - Pointer to receive number of parameters
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_list(
        os_tr181_handle_t *handle,
        const char *path,
        bool recursive,
        os_tr181_param_info_t **params,
        int *count);

/*
 * Free parameter list returned by os_tr181_list
 *
 * Parameters:
 *   params - Parameter info array
 *   count - Number of parameters
 */
void os_tr181_free_list(os_tr181_param_info_t *params, int count);

/*
 * Get error string for error code
 *
 * Parameters:
 *   error_code - Error code from library functions
 *
 * Returns: Human-readable error string
 */
const char *os_tr181_error_string(int error_code);

/*
 * Parse parameter type from string
 *
 * Parameters:
 *   type_str - Type string (e.g., "string", "int", "bool")
 *
 * Returns: Corresponding os_tr181_param_type_t value
 */
os_tr181_param_type_t os_tr181_parse_type(const char *type_str);

/*
 * Convert parameter type to string
 *
 * Parameters:
 *   type - Parameter type enumeration value
 *
 * Returns: String representation of the type
 */
const char *os_tr181_type_to_string(os_tr181_param_type_t type);

/*
 * Parse last instance number from TR-181 path
 *
 * Helper function to parse the last instance index from a path.
 * For example: "Device.WiFi.SSID.1.Name" returns 1
 *              "Device.IP.Interface.13.MaxMTUSize" returns 13
 *              "Device.DeviceInfo.ModelName" returns -1 (no instance)
 *
 * Parameters:
 *   path - Full TR-181 parameter path
 *
 * Returns: Instance number (>= 1) on success, -1 if no instance found
 */
os_tr181_error_t os_tr181_parse_instance(const char *path);

/*
 * Parse all instance numbers from TR-181 path
 *
 * Helper function to parse all instance indices from a path.
 * For example: "Device.WiFi.AccessPoint.1.AssociatedDevice.3.MACAddress"
 *              returns 2 with indices[0]=1, indices[1]=3
 *
 * Parameters:
 *   path - Full TR-181 parameter path
 *   indices - Output array to store instance numbers
 *   max_count - Maximum number of indices to parse (size of array)
 *
 * Returns: Number of instances found (>=0), or -1 on error
 */
os_tr181_error_t os_tr181_parse_instances(const char *path, int *indices, int max_count);

/*
 * Get callback function type for registered parameters
 *
 * Parameters:
 *   param_path - Full path of the parameter being read
 *   value - Output value (caller initializes, callback sets)
 *   user_data - User context pointer
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
typedef os_tr181_error_t (*os_tr181_get_cb_t)(const char *param_path, os_tr181_val_t *value, void *user_data);

/*
 * Set callback function type for registered parameters
 *
 * Parameters:
 *   param_path - Full path of the parameter being written
 *   value - New value to set (with type information)
 *   user_data - User context pointer
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
typedef os_tr181_error_t (*os_tr181_set_cb_t)(const char *param_path, const os_tr181_val_t *value, void *user_data);

/*
 * Add instance callback function type for table objects
 *
 * Parameters:
 *   object_path - Path of the table object
 *   instance_num - Output parameter for the new instance number
 *   user_data - User context pointer
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
typedef os_tr181_error_t (*os_tr181_add_cb_t)(const char *object_path, int *instance_num, void *user_data);

/*
 * Delete instance callback function type for table objects
 *
 * Parameters:
 *   object_path - Path of the instance to delete
 *   instance_num - Instance number to delete
 *   user_data - User context pointer
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
typedef os_tr181_error_t (*os_tr181_del_cb_t)(const char *object_path, int instance_num, void *user_data);

/*
 * Event callback function type
 *
 * Parameters:
 *   param_path - Full path of the parameter that changed
 *   new_value - New value of the parameter
 *   user_data - User context pointer passed to os_tr181_subscribe
 *
 * Note: The callback should not free the value; it's managed by the library.
 */
typedef void (*os_tr181_event_cb_t)(const char *param_path, const os_tr181_val_t *new_value, void *user_data);

/*
 * Subscribe to TR-181 parameter change events
 *
 * Supports both specific parameter and wildcard subscriptions:
 *   - Specific: "Device.WiFi.Radio.1.Enable"
 *   - Wildcard: "Device.WiFi.Radio.1." (all params under this path)
 *
 * Parameters:
 *   handle - Library handle
 *   path - Parameter path or path prefix for wildcard subscription
 *   callback - Callback function to invoke on parameter changes
 *   user_data - User context pointer passed to callback
 *   sub_handle - Output parameter for subscription handle
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Note: The subscription handle is used with os_tr181_unsubscribe() to
 *       remove this specific subscription.
 */
os_tr181_error_t os_tr181_subscribe(
        os_tr181_handle_t *handle,
        const char *path,
        os_tr181_event_cb_t callback,
        void *user_data,
        os_tr181_sub_handle_t *sub_handle);

/*
 * Unsubscribe from TR-181 parameter change events
 *
 * Parameters:
 *   handle - Library handle
 *   sub_handle - Subscription handle from os_tr181_subscribe()
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Note: All subscriptions are automatically cleaned up on os_tr181_close()
 */
os_tr181_error_t os_tr181_unsubscribe(os_tr181_handle_t *handle, os_tr181_sub_handle_t sub_handle);

/*
 * Wait for and process TR-181 parameter change events
 *
 * This is a blocking call that waits for events and dispatches them
 * to registered callbacks. Returns after timeout or error.
 *
 * Parameters:
 *   handle - Library handle
 *   timeout_ms - Timeout in milliseconds (-1 for infinite)
 *
 * Returns: OS_TR181_SUCCESS on success, OS_TR181_ERROR_TIMEOUT on timeout,
 *          other error code on failure
 */
os_tr181_error_t os_tr181_wait_event(os_tr181_handle_t *handle, int timeout_ms);

/*
 * Register a TR-181 object path as a data provider
 *
 * This registers the application as a provider for the specified object path.
 * After registration, the application must handle get/set operations via callbacks.
 *
 * Parameters:
 *   handle - Library handle
 *   object_path - Object path to register (e.g., "Device.MyApp.")
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_register_object(os_tr181_handle_t *handle, const char *object_path);

/*
 * Register a TR-181 parameter with callbacks
 *
 * Registers a parameter under a previously registered object path.
 * The get_callback is required, set_callback is optional (for read-only params).
 *
 * Parameters:
 *   handle - Library handle
 *   param_path - Full parameter path (e.g., "Device.MyApp.Parameter1")
 *   type - Parameter type
 *   writable - True if parameter is writable
 *   get_callback - Callback to get parameter value (required)
 *   set_callback - Callback to set parameter value (optional, NULL for read-only)
 *   user_data - User context pointer passed to callbacks
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_register_parameter(
        os_tr181_handle_t *handle,
        const char *param_path,
        os_tr181_param_type_t type,
        int writable,
        os_tr181_get_cb_t get_callback,
        os_tr181_set_cb_t set_callback,
        void *user_data);

/*
 * Register a TR-181 table object with add/delete callbacks
 *
 * Registers a multi-instance object (table) that supports adding and
 * deleting instances.
 *
 * Parameters:
 *   handle - Library handle
 *   table_path - Table object path (e.g., "Device.MyApp.Table.")
 *   add_callback - Callback to add new instance (optional)
 *   del_callback - Callback to delete instance (optional)
 *   user_data - User context pointer passed to callbacks
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_register_table(
        os_tr181_handle_t *handle,
        const char *table_path,
        os_tr181_add_cb_t add_callback,
        os_tr181_del_cb_t del_callback,
        void *user_data);

/*
 * Publish registered objects to the bus
 *
 * Makes all registered objects, parameters, and tables available on the bus.
 * Should be called after all registrations are complete.
 *
 * Parameters:
 *   handle - Library handle
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_publish_objects(os_tr181_handle_t *handle);

/*
 * Notify that a parameter value has changed
 *
 * This function notifies subscribers that a registered parameter's value
 * has changed. This is needed for read-only parameters that are updated
 * by the data provider (not through set operations).
 *
 * Parameters:
 *   handle - Library handle
 *   param_path - Full path of the parameter that changed (e.g., "Device.X_DEMO.Sample.count")
 *   old_value - Previous value of the parameter as a string (required)
 *   new_value - New value of the parameter as a string (required)
 *   type - Type of the parameter (OS_TR181_TYPE_STRING, OS_TR181_TYPE_INT, etc.)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Note: For writable parameters, change notifications are sent automatically
 * when the value is set. This function is mainly for read-only parameters
 * that change due to internal state updates or external events.
 */
os_tr181_error_t os_tr181_notify_changed(
        os_tr181_handle_t *handle,
        const char *param_path,
        const os_tr181_val_t *old_value,
        const os_tr181_val_t *new_value);

/*
 * Process TR-181 requests for registered objects
 *
 * This function processes incoming get/set/add/delete requests for
 * registered objects. Should be called periodically or in event loop.
 *
 * Parameters:
 *   handle - Library handle
 *   timeout_ms - Timeout in milliseconds (0 for non-blocking)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_process_requests(os_tr181_handle_t *handle, int timeout_ms);

/*
 * Add a new instance to a multi-instance object (table)
 *
 * Creates a new instance in a TR-181 table object and returns the
 * instance number assigned.
 *
 * Parameters:
 *   handle - Library handle
 *   object_path - Table object path (e.g., "Device.WiFi.SSID.")
 *   instance_number - Output parameter for the new instance number
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Example:
 *   int instance;
 *   os_tr181_add_instance(handle, "Device.WiFi.SSID.", &instance);
 *   // New instance created at Device.WiFi.SSID.{instance}.
 */
os_tr181_error_t os_tr181_add_instance(os_tr181_handle_t *handle, const char *object_path, int *instance_number);

/*
 * Add a new instance and wait for creation confirmation
 *
 * Creates a new instance in a TR-181 table object and waits until
 * the instance is confirmed to exist on the backend before returning.
 * Uses backend-specific synchronous methods if available, otherwise
 * falls back to event-based verification.
 *
 * Parameters:
 *   handle - Library handle
 *   object_path - Table object path (e.g., "Device.WiFi.SSID.")
 *   instance_number - Output parameter for the new instance number
 *   timeout_ms - Maximum time to wait for confirmation in milliseconds
 *                (use OS_TR181_DEFAULT_TIMEOUT_MS or 0 for default)
 *
 * Returns: OS_TR181_SUCCESS on success,
 *          OS_TR181_ERROR_TIMEOUT if instance not confirmed within timeout,
 *          other error codes on failure
 *
 * Example:
 *   int instance;
 *   int ret = os_tr181_add_instance_wait(handle, "Device.WiFi.SSID.",
 *                                         &instance, OS_TR181_DEFAULT_TIMEOUT_MS);
 *   if (ret == OS_TR181_SUCCESS) {
 *       // Instance is guaranteed to exist and be accessible
 *       char param[256];
 *       snprintf(param, sizeof(param), "Device.WiFi.SSID.%d.SSID", instance);
 *       os_tr181_set(handle, param, "MyNetwork", OS_TR181_TYPE_STRING);
 *   }
 */
os_tr181_error_t os_tr181_add_instance_wait(
        os_tr181_handle_t *handle,
        const char *object_path,
        int *instance_number,
        int timeout_ms);

/*
 * Delete an instance from a multi-instance object (table)
 *
 * Removes an existing instance from a TR-181 table object.
 *
 * Parameters:
 *   handle - Library handle
 *   instance_path - Full path to instance (e.g., "Device.WiFi.SSID.1.")
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Example:
 *   os_tr181_delete_instance(handle, "Device.WiFi.SSID.3.");
 */
os_tr181_error_t os_tr181_delete_instance(os_tr181_handle_t *handle, const char *instance_path);

/*
 * Get list of instance numbers for a multi-instance object
 *
 * Returns an array of instance numbers that exist under the specified
 * table object path.
 *
 * Parameters:
 *   handle - Library handle
 *   object_path - Table object path (e.g., "Device.WiFi.SSID.")
 *   instance_numbers - Output array of instance numbers (caller must free)
 *   count - Number of instances found
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Example:
 *   int *instances, count;
 *   os_tr181_get_instances(handle, "Device.WiFi.SSID.", &instances, &count);
 *   for (int i = 0; i < count; i++) {
 *       printf("Instance: %d\n", instances[i]);
 *   }
 *   free(instances);
 */
os_tr181_error_t os_tr181_get_instances(
        os_tr181_handle_t *handle,
        const char *object_path,
        int **instance_numbers,
        int *count);

/*
 * Sort instance numbers in ascending order
 *
 * Helper function to sort an array of instance numbers returned by
 * os_tr181_get_instances(). Sorts in-place.
 *
 * Parameters:
 *   instance_numbers - Array of instance numbers to sort
 *   count - Number of elements in the array
 */
void os_tr181_sort_instances(int *instance_numbers, int count);

/*
 * Get the backend name
 *
 * Returns the name of the TR-181 backend being used.
 *
 * Returns: Backend name string ("ambiorix", "ccsp", ...)
 */
const char *os_tr181_get_backend_name(void);

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_H */
