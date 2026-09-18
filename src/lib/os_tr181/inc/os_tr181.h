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

/* Maximum length for TR-181 parameter paths (including null terminator) */
#define OS_TR181_PATH_MAX 1024

/* Maximum number of file descriptors for event loop integration */
#define OS_TR181_MAX_FDS 8

/* Flags for os_tr181_list() */
#define OS_TR181_LIST_RECURSIVE (1 << 0) /* List recursively (all sub-levels) */
#define OS_TR181_LIST_DETAILS   (1 << 1) /* Fetch detailed type info (slower) */

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
    uint32_t access_flags;
    os_tr181_param_type_t type;
} os_tr181_param_info_t;

/* Opaque handle for library context */
typedef struct os_tr181_handle_s os_tr181_handle_t;

/* Opaque handle for subscription */
typedef struct os_tr181_subscription_s *os_tr181_sub_handle_t;

/* Forward declaration for async method context (defined in Asynchronous Method Invocation API section) */
struct os_tr181_async_method_ctx;

/* Forward declaration for libev loop (defined in <ev.h>) */
struct ev_loop;

/*
 * Initialize TR-181 library
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_init(os_tr181_handle_t **handle);

/*
 * Initialize TR-181 library with explicit component name
 *
 * Parameters:
 *   handle - Pointer to receive library handle
 *   component_name - Component name to use, or NULL for auto-generation
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Note: If component_name is NULL, a unique name is auto-generated.
 *       Use this function if you need to control the component name.
 */
os_tr181_error_t os_tr181_init_ex(os_tr181_handle_t **handle, const char *component_name);

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
 *   flags - Bitfield of OS_TR181_LIST_* flags:
 *           OS_TR181_LIST_RECURSIVE - List recursively (all sub-levels), default is next level only
 *           OS_TR181_LIST_DETAILS - Fetch detailed type info (slower)
 *   params - Pointer to receive array of parameter info (caller must free with os_tr181_free_list)
 *   count - Pointer to receive number of parameters
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Note: Without OS_TR181_LIST_DETAILS flag, properties are reported as OS_TR181_TYPE_PROPERTY.
 *       With the flag, actual types (INT, STRING, BOOL, etc.) are fetched but is slower.
 */
os_tr181_error_t os_tr181_list(
        os_tr181_handle_t *handle,
        const char *path,
        uint32_t flags,
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
 * Invoke a TR-181 method (RPC call)
 *
 * Invokes a TR-181 method with optional input arguments and returns the result.
 * The method name can be specified with or without trailing parentheses "()",
 * e.g., both "Device.WiFi.Reset()" and "Device.WiFi.Reset" are accepted.
 *
 * Parameters:
 *   handle - Library handle
 *   path - Full method path (e.g., "Device.WiFi.Reset" or "Device.WiFi.Reset()")
 *   args - Input arguments as dictionary/object, or NULL for no args
 *   result - Output result value (caller must initialize and free with os_val_free), can be NULL if result not needed
 *   timeout_sec - Timeout in seconds (0 = use backend default)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Example:
 *   os_tr181_val_t args = OS_VAL_INIT();
 *   os_tr181_val_t result = OS_VAL_INIT();
 *
 *   // Simple invocation with no arguments
 *   ret = os_tr181_invoke(handle, "Device.X_DEMO.Sample.Reset()", NULL, &result, 0);
 *
 *   // Invocation with JSON arguments
 *   os_val_from_json_string(&args, "{\"type\":\"counter\"}");
 *   ret = os_tr181_invoke(handle, "Device.X_DEMO.Sample.Reset()", &args, &result, 30);
 *   os_val_free(&args);
 *
 *   // Process result
 *   char *json_str = NULL;
 *   os_val_to_json_string(&result, &json_str);
 *   printf("Result: %s\n", json_str);
 *   free(json_str);
 *   os_val_free(&result);
 */
os_tr181_error_t os_tr181_invoke(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        uint32_t timeout_sec);

/* ========================================================================
 * Asynchronous Method Invocation API
 * ======================================================================== */

/**
 * @brief Async request handle
 * Opaque handle for managing async method invocations
 */
typedef struct os_tr181_async_request os_tr181_async_request_t;

/**
 * @brief Async method completion callback
 *
 * Called when an async method invocation completes or times out.
 *
 * @param method_name   Name of the invoked method
 * @param error         Error code (OS_TR181_SUCCESS if successful)
 * @param result        Result value (NULL if error, caller must not free)
 * @param priv          User private data
 *
 * @note The result pointer is only valid during the callback.
 *       Copy data if needed after callback returns.
 */
typedef void (*os_tr181_async_cb_t)(
        const char *method_name,
        os_tr181_error_t error,
        const os_tr181_val_t *result,
        void *priv);

/**
 * @brief Invoke a method asynchronously (non-blocking)
 *
 * Invokes a TR-181 method and returns immediately. The callback will be
 * called when the method completes or times out. Callbacks are dispatched
 * when os_tr181_process_requests() is called.
 *
 * @param handle        TR-181 handle
 * @param method        Method name (e.g., "Device.IP.Diagnostics.IPPing()")
 * @param args          Method arguments (can be NULL), ownership retained by caller
 * @param callback      Completion callback
 * @param priv          User private data passed to callback
 * @param timeout_sec   Timeout in seconds (0 = platform default)
 * @param req           OUT: Request handle (can be NULL if not needed)
 *
 * @return OS_TR181_SUCCESS or error code
 *
 * @note Request handle can be used for cancellation via os_tr181_invoke_async_cancel().
 * @note Callback is called from event loop context (process_requests).
 * @note After cancellation, callback will not be called and priv data can be safely freed.
 */
os_tr181_error_t os_tr181_invoke_async(
        os_tr181_handle_t *handle,
        const char *method,
        const os_tr181_val_t *args,
        os_tr181_async_cb_t callback,
        void *priv,
        int timeout_sec,
        os_tr181_async_request_t **req);

/**
 * @brief Cancel an async method invocation
 *
 * Cancels a pending async method invocation. After this call returns:
 * - The callback will NOT be called
 * - The priv data can be safely freed by the caller
 * - The backend may still complete the method (platform-dependent)
 *
 * @param req Request handle from os_tr181_invoke_async()
 *
 * @return OS_TR181_SUCCESS or error code
 *
 * @note Request handle remains valid and must be freed separately if needed
 * @note Calling cancel on already-completed or cancelled request is safe (no-op)
 * @note Ambiorix: Backend request is cancelled. CCSP/RBUS: Only callback is detached.
 */
os_tr181_error_t os_tr181_invoke_async_cancel(os_tr181_async_request_t *req);

/**
 * @brief Async method context for provider-side async responses
 * Opaque handle for provider to complete method asynchronously
 */
typedef struct os_tr181_async_method_ctx os_tr181_async_method_ctx_t;

/**
 * @brief Send async method response (provider-side)
 *
 * Complete a deferred method invocation. Can be called from any thread/context.
 *
 * @param async_ctx     Async context from handler (stored when returned DEFERRED)
 * @param error         Result status (OS_TR181_SUCCESS if successful)
 * @param result        Result value (ownership transferred, can be NULL on error)
 *
 * @return OS_TR181_SUCCESS or error code
 *
 * @note async_ctx is freed after this call (do not use after)
 * @note result is freed by this function (caller must not free)
 */
os_tr181_error_t os_tr181_method_respond(
        os_tr181_async_method_ctx_t *async_ctx,
        os_tr181_error_t error,
        os_tr181_val_t *result);

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
int os_tr181_parse_instance(const char *path);

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
int os_tr181_parse_instances(const char *path, int *indices, int max_count);

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
 * The library pre-assigns the instance number before calling this callback.
 *
 * Parameters:
 *   object_path - Path of the table object
 *   instance_num - Instance number for the new instance
 *   initial_values - Optional dict of initial parameter values (can be NULL).
 *                    The data behind this pointer is valid only during the
 *                    callback. Should not be freed by the callback and should
 *                    not be referenced by the callee after the callback
 *                    returns.
 *   user_data - User context pointer
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Notes:
 *   - Instance number is pre-assigned by the library (last_index + 1)
 *   - Callback should allocate resources and initialize instance data
 *   - Return error to reject instance creation (library handles rollback)
 */
typedef os_tr181_error_t (*os_tr181_add_cb_t)(
        const char *object_path,
        int instance_num,
        const os_tr181_val_t *initial_values,
        void *user_data);

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
 * Method callback function type for registered methods
 *
 * Callback invoked when a client calls a registered method.
 * Supports both synchronous and asynchronous responses.
 *
 * Parameters:
 *   handle - TR-181 library handle
 *   path - Full method path (e.g., "Device.MyService.DoSomething")
 *   args - Input arguments as dictionary/object (NULL if no arguments)
 *   result - Output result structure (populate for sync response only)
 *   async_ctx - Async context handle (for deferred responses, always provided)
 *   user_data - User context pointer (from os_tr181_register_method)
 *
 * Returns:
 *   OS_TR181_SUCCESS - Synchronous success, result in 'result' param
 *   OS_TR181_ERROR_DEFERRED - Asynchronous response (call os_tr181_method_respond later)
 *   Other error codes - Synchronous failure
 *
 * Notes:
 * - For sync responses: Fill 'result', return SUCCESS/ERROR, ignore async_ctx
 * - For async responses: Store async_ctx, return DEFERRED, later call os_tr181_method_respond
 * - The async_ctx is always provided - return code determines sync vs async behavior
 * - The callback should validate input arguments (type/values)
 * - The library handles memory management for result
 * - The callback must be thread-safe if library is used from multiple threads
 *
 * See os_tr181_async.h for detailed async usage examples.
 */
typedef os_tr181_error_t (*os_tr181_method_cb_t)(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        struct os_tr181_async_method_ctx *async_ctx,
        void *user_data);

/*
 * Event callback function type
 *
 * Used for both parameter value-change and custom event subscriptions:
 *
 * For parameter value-change subscriptions:
 *   path  - Full parameter path that changed
 *           e.g. "Device.WiFi.Radio.1.Enable"
 *   value - New scalar value of the parameter
 *
 * For custom event subscriptions (USP "!" events or platform named events):
 *   path  - Full event path that fired
 *           e.g. "Device.ManagementServer.SendInformMessage!"
 *           e.g. "Device.X_PRPLWARE-COM_Buttons.Button.1.Event.1.ButtonEvent"
 *   value - Event arguments. Typically a dict (OS_TR181_TYPE_DICT) with named
 *           arguments; may be a scalar if the provider emits a single unnamed
 *           value. Empty dict if the event carries no data.
 *
 * Note: The callback should not free the value; it's managed by the library.
 */
typedef void (*os_tr181_event_cb_t)(const char *path, const os_tr181_val_t *value, void *user_data);

/*
 * Subscribe to TR-181 parameter value-change events or custom events
 *
 * Accepts three forms of path:
 *
 *   Object paths (wildcard value-change subscription):
 *     - "Device.WiFi.Radio.1."  (all params under object)
 *
 *   Parameter / named-event paths (specific subscription):
 *     - Parameter:    "Device.WiFi.Radio.1.Enable"
 *     - USP event:    "Device.ManagementServer.SendInformMessage!"
 *     - Platform event: "Device.X_PRPLWARE-COM_Buttons.Button.1.Event.1.ButtonEvent"
 *
 *   Wildcard event paths are also supported:
 *     - "Device.X_PRPLWARE-COM_Buttons.Button.*.ButtonEvent"
 *
 * The callback receives:
 *   - Value-change: path = parameter path, value = new scalar value
 *   - Custom event: path = full event path, value = event args as dict
 *
 * Parameters:
 *   handle     - Library handle
 *   path       - Parameter path, object path, or event path
 *   callback   - Callback function invoked on change or event
 *   user_data  - User context pointer passed to callback
 *   sub_handle - Output handle used with os_tr181_unsubscribe()
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
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
 *   access_flags - Access control flags (OS_TR181_ACCESS_READONLY, READWRITE, or WRITEONCE)
 *   get_callback - Callback to get parameter value (required)
 *   set_callback - Callback to set parameter value (optional, NULL for read-only)
 *   user_data - User context pointer passed to callbacks
 *
 * Access Flags:
 *   OS_TR181_ACCESS_READONLY  - Read-only parameter (set_callback can be NULL)
 *   OS_TR181_ACCESS_READWRITE - Read-write parameter (normal behavior)
 *   OS_TR181_ACCESS_WRITEONCE - Write-once parameter (first write succeeds, subsequent writes fail)
 *
 * Example:
 *   os_tr181_register_parameter(handle, "Device.DeviceInfo.ModelName",
 *       OS_TR181_TYPE_STRING, OS_TR181_ACCESS_READONLY, get_model_cb, NULL, data);
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_register_parameter(
        os_tr181_handle_t *handle,
        const char *param_path,
        os_tr181_param_type_t type,
        uint32_t access_flags,
        os_tr181_get_cb_t get_callback,
        os_tr181_set_cb_t set_callback,
        void *user_data);

/* Method parameter flags for os_tr181_param_schema_t. */
#define OS_TR181_PARAM_IN        (1u << 0) /* input parameter */
#define OS_TR181_PARAM_OUT       (1u << 1) /* output parameter */
#define OS_TR181_PARAM_MANDATORY (1u << 2) /* caller must supply this arg */
#define OS_TR181_PARAM_STRICT    (1u << 3) /* strict type checking */

/* Note: without STRICT/MANDATORY the declared parameters are informational -
 * no checking is enforced. MANDATORY rejects the call if the arg is absent.
 * STRICT rejects the call if the arg type doesn't match. Extra args not in
 * the schema are always allowed. Nested parameters are not supported; use a
 * DICT arg as a catch-all and validate the structure in the handler. */

/*
 * Method parameter descriptor for os_tr181_register_method().
 *
 * Defines the name, type and direction of a single method argument.
 * Arrays of these are NULL-terminated (last entry has name == NULL).
 */
typedef struct
{
    const char *name;   /* parameter name */
    int type;           /* OS_TR181_TYPE_* */
    unsigned int flags; /* OS_TR181_PARAM_* */
} os_tr181_param_schema_t;

/*
 * Method flags for os_tr181_register_method().
 *
 * On Ambiorix backends these map to amxd_fattr_* attributes and are
 * visible in ubus-cli dumps and obuspa datamodel output.
 * On CCSP/rbus backends all flags are ignored.
 */
#define OS_TR181_METHOD_ASYNC     (1u << 0) /* method responds asynchronously */
#define OS_TR181_METHOD_PRIVATE   (1u << 1) /* method only callable internally */
#define OS_TR181_METHOD_PROTECTED (1u << 2) /* method hidden from external clients */

/*
 * Register a TR-181 method with callback
 *
 * Registers a callable method under a previously registered object path.
 * The method callback will be invoked when clients call the method.
 *
 * params optionally registers the input/output argument schema (Ambiorix
 * only; rbus has no equivalent). Pass NULL if no schema is needed.
 *
 * flags controls method attributes (OS_TR181_METHOD_*). Pass 0 for defaults.
 * OS_TR181_METHOD_ASYNC must match the async="true/false" attribute from the
 * TR-181 standard for the command being implemented — it determines the USP
 * Operate response format (inline args vs. LocalAgent.Request path).
 * On rbus flags are ignored.
 *
 * Parameters:
 *   handle    - Library handle
 *   path      - Full method path (e.g., "Device.MyService.Reset" or
 *               "Device.MyService.Reset()") — "()" suffix is optional
 *   method_cb - Callback function to handle method invocation (required)
 *   user_data - User context pointer passed to callback
 *   params    - NULL-terminated array of parameter descriptors, or NULL
 *   flags     - OS_TR181_METHOD_* flags, or 0
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Notes:
 * - Method must be registered on an object that is already registered
 * - Method path must end with method name (after last '.')
 * - Method will be visible in list output
 * - params array must be NULL-terminated (last entry: { NULL })
 *
 * Example:
 *   const os_tr181_param_schema_t params[] = {
 *       { "Type",   OS_TR181_TYPE_STRING, OS_TR181_PARAM_IN },
 *       { "Status", OS_TR181_TYPE_STRING, OS_TR181_PARAM_OUT },
 *       { NULL, 0, 0 }
 *   };
 *   os_tr181_register_method(handle, "Device.MyService.Reset()",
 *                            my_reset_handler, NULL, params, 0);
 */
os_tr181_error_t os_tr181_register_method(
        os_tr181_handle_t *handle,
        const char *path,
        os_tr181_method_cb_t method_cb,
        void *user_data,
        const os_tr181_param_schema_t *params,
        unsigned int flags);

/*
 * Register a TR-181 event
 *
 * Declares a named event under a previously registered object so it is
 * visible in datamodel introspection (ubus-cli dump, obuspa dump, rbuscli
 * getnames). Must be called before os_tr181_publish_objects().
 *
 * params optionally registers the event argument schema — the fields that
 * appear in the event data dict when the event is emitted. The schema is
 * used for introspection only; the array is consumed immediately and no
 * reference is kept. Pass NULL for events that carry no arguments.
 * On CCSP/rbus backends params are accepted but silently ignored (rbus has
 * no event argument schema concept).
 *
 * Parameters:
 *   handle - Library handle
 *   path   - Full event path, e.g. "Device.X_DEMO.Sample.StateChange!"
 *   params - NULL-terminated array of argument descriptors, or NULL
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_register_event(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_param_schema_t *params);

/*
 * Emit a TR-181 event
 *
 * Fires a named event previously registered with os_tr181_register_event().
 * All active subscribers are notified. May be called at any time after
 * os_tr181_publish_objects() has been called.
 *
 * data should be a dict value (OS_TR181_TYPE_DICT) containing the event
 * argument values, or NULL for events that carry no arguments.
 *
 * Note: The CCSP/rbus backend enforces dict-or-NULL and returns
 * OS_TR181_ERROR_INVALID for other types (rbus events are key-value objects
 * by design). The Ambiorix backend accepts scalars too (the platform wraps
 * them under a "data" key), using a dict is recommended for portability.
 *
 * Parameters:
 *   handle - Library handle
 *   path   - Full event path, e.g. "Device.X_DEMO.Sample.StateChange!"
 *   data   - Event argument values as a dict, or NULL
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_emit_event(os_tr181_handle_t *handle, const char *path, const os_tr181_val_t *data);

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
 * IMPORTANT: This function can only be called ONCE per handle.
 * Subsequent calls will return OS_TR181_ERROR with no effect.
 *
 * Parameters:
 *   handle - Library handle
 *
 * Returns:
 *   OS_TR181_SUCCESS on success
 *   OS_TR181_ERROR if already published or on failure
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
 * Process TR-181 bus events and requests
 *
 * This function processes events and requests from the underlying data model bus(es).
 * For providers: handles incoming get/set/add/delete requests on registered objects.
 * For clients: delivers subscription notifications and async operation responses.
 * Also handles backend-specific maintenance (e.g., USP reconnection attempts).
 *
 * Parameters:
 *   handle - Library handle
 *   timeout_ms - Timeout in milliseconds (0 for non-blocking, -1 for infinite)
 *
 * Returns: OS_TR181_SUCCESS on success, OS_TR181_ERROR_TIMEOUT on timeout,
 *          other error code on failure
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
 * Add a new instance with initial values
 *
 * Creates a new instance in a TR-181 table object and optionally sets
 * initial parameter values including a custom Alias.
 *
 * Parameters:
 *   handle - Library handle
 *   object_path - Table object path (must end with '.', e.g., "Device.WiFi.SSID.")
 *   index - Requested instance index (0 = auto-assign next available)
 *   alias_value - Value for Alias parameter, or NULL for auto-generation
 *                 If NULL: generates "cpe-<ParentName>-<Index>"
 *                 If provided: sets as initial Alias value (write-once)
 *   values - Dict of initial parameter values, or NULL
 *            Key: parameter name (e.g., "SSID", "Enable")
 *            Value: parameter value (any type)
 *            Can include "Alias" (may override alias_value on some platforms)
 *   instance_number - Output parameter for the new instance number
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 *
 * Notes:
 *   - If index is 0, backend auto-assigns next available index (never reuses)
 *   - If index is specified, backend attempts to use that index (fails if exists)
 *   - Explicit index support varies by backend:
 *     * Ambiorix: Full support
 *     * CCSP/UBUS/NULL: Index parameter ignored, always auto-assigns
 *   - On Ambiorix: Maps directly to amxb_add() name and values parameters
 *   - On CCSP: Creates instance first, then sets values via os_tr181_set()
 *   - Write-once parameters (like Alias) can only be set at creation time
 *   - If values contains "Alias", platform determines which takes precedence
 *
 * Example:
 *   os_tr181_val_t values = OS_VAL_INIT();
 *   os_val_set_dict(&values);
 *   os_val_dict_set_string(&values, "SSID", "MyNetwork");
 *   os_val_dict_set_bool(&values, "Enable", true);
 *
 *   int instance;
 *   os_tr181_add_instance_ex(handle, "Device.WiFi.SSID.", 0,
 *                            "HomeNetwork", &values, &instance);
 *   os_val_free(&values);
 */
os_tr181_error_t os_tr181_add_instance_ex(
        os_tr181_handle_t *handle,
        const char *object_path,
        uint32_t index,
        const char *alias_value,
        const os_tr181_val_t *values,
        int *instance_number);

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

/* ========================================================================
 * Event Loop Integration API
 * ========================================================================
 *
 * These APIs allow integration with event loops (libev)
 *
 * Two integration approaches:
 *
 * 1. Manual Integration (works with any event loop):
 *    - os_tr181_get_fds() - Get file descriptors to monitor
 *    - os_tr181_process_events() - Process events when FDs are ready
 *    - os_tr181_get_poll_timeout() - Get timeout for timer-based events
 *    - os_tr181_register_fd_change_callback() - Get notified of FD changes
 *
 * 2. Automatic Integration (libev):
 *    - os_tr181_attach_loop() - Automatic setup with libev
 *    - os_tr181_detach_loop() - Cleanup libev integration
 *
 * Note: Ambiorix backend uses 2-3 FDs (ubus, signals, USP when connected).
 *       CCSP backend uses 1 FD (wakeup pipe for RBUS cross-thread signaling).
 */

/**
 * Callback invoked when the set of file descriptors change, or when a timed
 * wakeup is (no longer) needed.
 *
 * This happens when:
 * - USP connection is established (new FD added, timer stopped)
 * - USP connection is lost (FD removed, timer started)
 * - Backend reconnects with different FD
 *
 * The application should call os_tr181_get_fds() to get the updated list
 * and update its event loop watchers accordingly.
 *
 * The application should call os_tr181_get_poll_timeout() to adjust
 * timer-based wakeups if needed.
 *
 * @param handle     The TR181 handle
 * @param user_data  User data provided during registration
 *
 * @note Called from the same thread as os_tr181_process_events()
 * @note May be called multiple times (disconnect + reconnect)
 * @note Always safe to call os_tr181_get_fds() from this callback
 */
typedef void (*os_tr181_fd_change_callback_t)(os_tr181_handle_t *handle, void *user_data);

/**
 * Register callback for file descriptor changes.
 *
 * @param handle     The TR181 handle
 * @param callback   Function to call when FDs change (NULL to unregister)
 * @param user_data  User data passed to callback
 *
 * @return OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_register_fd_change_callback(
        os_tr181_handle_t *handle,
        os_tr181_fd_change_callback_t callback,
        void *user_data);

/**
 * Get file descriptors that need to be monitored for read events.
 *
 * @param handle    The TR181 handle
 * @param fds       Array to fill with file descriptors
 * @param max_fds   Size of the fds array (OS_TR181_MAX_FDS recommended)
 * @param num_fds   [out] Actual number of FDs returned (partial count on overflow)
 *
 * @return OS_TR181_SUCCESS on success,
 *         OS_TR181_ERROR_OVERFLOW if max_fds was too small (num_fds contains
 *         however many FDs did fit), error code otherwise
 *
 * @note FDs can change at runtime (e.g., USP reconnection). Call this
 *       after receiving an FD change callback, or periodically if not
 *       using callbacks.
 *
 * @note Ambiorix backend typically returns 2-3 FDs (ubus, usp, signals)
 * @note CCSP backend returns 1 FD (wakeup pipe for RBUS cross-thread signaling).
 */
os_tr181_error_t os_tr181_get_fds(os_tr181_handle_t *handle, int *fds, size_t max_fds, size_t *num_fds);

/**
 * Get recommended timeout for next poll/select operation.
 *
 * Returns the time (in milliseconds) until the next scheduled event
 * (e.g., USP reconnection retry, pending async operations).
 *
 * @param handle    The TR181 handle
 *
 * @return Timeout in milliseconds, or:
 *         - -1 if no timeout needed (can poll indefinitely)
 *         - 0 if should process immediately (events pending)
 *
 * @note This is used to determine when to wake up even if no FD activity.
 *       For example, USP reconnection needs to retry after a delay even
 *       if no data arrives on any FD.
 *
 * @note Call this before each poll/select to get current timeout.
 *       The value may change as timers fire or new operations start.
 */
int os_tr181_get_poll_timeout(os_tr181_handle_t *handle);

/* ========================================================================
 * libev integration API
 * ========================================================================
 *
 * Integration with libev event loop.
 * Requires libev library and <ev.h> header.
 */

/**
 * Attach TR181 handle to a libev event loop.
 *
 * This convenience function automatically:
 * - Sets up ev_io watchers for all file descriptors
 * - Sets up ev_timer watcher for timeout-based events
 * - Registers FD change callback to update watchers dynamically
 * - Handles FD changes (USP connect/disconnect) transparently
 *
 * @param handle    The TR181 handle
 * @param loop      The libev event loop to attach to
 *
 * @return OS_TR181_SUCCESS on success, error code otherwise
 *
 * @note Only one loop can be attached per handle.
 * @note The loop must remain valid until os_tr181_detach_loop() is called
 *      or the handle is destroyed.
 * @note All TR181 callbacks will be invoked in the libev loop's thread.
 * @note Automatically handles FDs that appear after attachment (e.g., USP).
 */
os_tr181_error_t os_tr181_attach_loop(os_tr181_handle_t *handle, struct ev_loop *loop);

/**
 * Detach TR181 handle from its libev event loop.
 *
 * Stops and frees all ev_io and ev_timer watchers created by
 * os_tr181_attach_loop(). Unregisters the FD change callback.
 *
 * @param handle    The TR181 handle
 *
 * @return OS_TR181_SUCCESS on success, error code otherwise
 *
 * @note Safe to call even if not attached (returns success).
 * @note After detaching, you can call os_tr181_process_requests()
 *      for manual polling, or attach to a different loop.
 */
os_tr181_error_t os_tr181_detach_loop(os_tr181_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_H */
