#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#ifndef __cdecl
#define __cdecl
#endif
#endif

#include "nvapi/nvapi_lite_common.h"
#include "nvapi/nvapi_lite_salend.h"

#define NVAPI_QUERY_INTERFACE_FN "nvapi_QueryInterface"
#define QUERY_NVAPI_INITIALIZE 0x0150e828
#define QUERY_NVAPI_UNLOAD 0xd22bdd7e
#define QUERY_NVAPI_ENUM_PHYSICAL_GPUS 0xe5ac921f
#define QUERY_NVAPI_GET_ERROR_MESSAGE 0x6c2d048c
#define QUERY_NVAPI_THERMALS 0x65fe3aad
#define QUERY_NVAPI_VOLTAGE 0x465f9bcf
#define NVAPI_THERMALS_VALUE_COUNT 40
#define NVAPI_THERMALS_HOTSPOT_INDEX 9
#define NVAPI_THERMALS_VRAM_INDEX 15

typedef void *(*NvAPI_QueryInterfaceFn)(NvU32 id);
typedef NvAPI_Status (*NvAPI_InitializeFn)(void);
typedef NvAPI_Status (*NvAPI_UnloadFn)(void);
typedef NvAPI_Status (*NvAPI_EnumPhysicalGPUsFn)(
    NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS],
    NvU32 *count);
typedef NvAPI_Status (*NvAPI_GetErrorMessageFn)(NvAPI_Status status,
                                                char message[NVAPI_SHORT_STRING_MAX]);

typedef struct NvApiThermals {
    NvU32 version;
    NvS32 mask;
    /* NvAPI thermals values: hotspot index 9, VRAM index 15. */
    NvS32 values[NVAPI_THERMALS_VALUE_COUNT];
} NvApiThermals;

typedef NvAPI_Status (*NvAPI_GetThermalsFn)(NvPhysicalGpuHandle handle,
                                            NvApiThermals *thermals);

typedef struct NvApiVoltage {
    NvU32 version;
    NvU32 flags;
    NvU32 padding_1[8];
    NvU32 value_uv;
    NvU32 padding_2[8];
} NvApiVoltage;

typedef NvAPI_Status (*NvAPI_GetVoltageFn)(NvPhysicalGpuHandle handle,
                                           NvApiVoltage *voltage);

_Static_assert(sizeof(NvApiVoltage) == 76, "NvApiVoltage must match NvAPI layout");

static void *load_symbol(void *library, const char *symbol) {
    void *result = NULL;
    char *error = NULL;

    dlerror();
    result = dlsym(library, symbol);
    error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "Failed to load symbol %s: %s\n", symbol, error);
        return NULL;
    }

    return result;
}

static void *query_nvapi_function(NvAPI_QueryInterfaceFn query_interface, NvU32 id,
                                  const char *name) {
    void *fn = query_interface(id);

    if (fn == NULL) {
        fprintf(stderr, "nvapi_QueryInterface returned NULL for %s (0x%x)\n", name, id);
    }

    return fn;
}

static int check_nvapi_status(NvAPI_Status status, NvAPI_GetErrorMessageFn get_error_message,
                              const char *action) {
    char message[NVAPI_SHORT_STRING_MAX];

    if (status == NVAPI_OK) {
        return 1;
    }

    memset(message, 0, sizeof(message));
    if (get_error_message != NULL &&
        get_error_message(status, message) == NVAPI_OK && message[0] != '\0') {
        fprintf(stderr, "%s failed: %s (%d)\n", action, message, status);
    } else {
        fprintf(stderr, "%s failed with status %d\n", action, status);
    }

    return 0;
}

static int read_temp_value(const NvApiThermals *thermals, size_t index, int *out) {
    int raw_value = 0;

    if (index >= sizeof(thermals->values) / sizeof(thermals->values[0])) {
        return 0;
    }

    raw_value = thermals->values[index] / 256;
    if (raw_value <= 0 || raw_value >= 255) {
        return 0;
    }

    *out = raw_value;
    return 1;
}

static NvS32 calculate_thermals_mask(NvAPI_GetThermalsFn get_thermals,
                                     NvAPI_GetErrorMessageFn get_error_message,
                                     NvPhysicalGpuHandle handle) {
    NvApiThermals thermals = {0};
    NvAPI_Status status = NVAPI_OK;

    thermals.version = MAKE_NVAPI_VERSION(NvApiThermals, 2);
    thermals.mask = 1;

    /* Initial probe to ensure the call succeeds before iterating masks. */
    status = get_thermals(handle, &thermals);
    if (!check_nvapi_status(status, get_error_message, "NvAPI_GetThermals (mask probe)")) {
        return 0;
    }

    for (int bit = 0; bit < 32; ++bit) {
        NvU32 bit_mask = 1u << bit;

        thermals.mask = (NvS32)bit_mask;
        status = get_thermals(handle, &thermals);
        if (status != NVAPI_OK) {
            return (NvS32)(bit_mask - 1u);
        }
    }

    return (NvS32)~0u;
}

int main(void) {
    void *library = NULL;
    NvAPI_QueryInterfaceFn query_interface = NULL;
    NvAPI_InitializeFn initialize = NULL;
    NvAPI_UnloadFn unload = NULL;
    NvAPI_EnumPhysicalGPUsFn enum_gpus = NULL;
    NvAPI_GetErrorMessageFn get_error_message = NULL;
    NvAPI_GetThermalsFn get_thermals = NULL;
    NvAPI_GetVoltageFn get_voltage = NULL;
    NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS];
    NvU32 gpu_count = 0;
    NvAPI_Status status = NVAPI_OK;
    int hotspot_temp = 0;
    int memory_temp = 0;
    int has_hotspot = 0;
    int has_memory = 0;
    int has_voltage = 0;
    NvApiVoltage voltage = {0};
    NvS32 thermals_mask = 0;
    NvApiThermals thermals = {0};

    library = dlopen("libnvidia-api.so.1", RTLD_LAZY);
    if (library == NULL) {
        fprintf(stderr, "Failed to open libnvidia-api.so.1: %s\n", dlerror());
        return 1;
    }

    query_interface = (NvAPI_QueryInterfaceFn)load_symbol(library, NVAPI_QUERY_INTERFACE_FN);
    if (query_interface == NULL) {
        dlclose(library);
        return 1;
    }

    get_error_message = (NvAPI_GetErrorMessageFn)query_nvapi_function(
        query_interface, QUERY_NVAPI_GET_ERROR_MESSAGE, "NvAPI_GetErrorMessage");
    initialize = (NvAPI_InitializeFn)query_nvapi_function(
        query_interface, QUERY_NVAPI_INITIALIZE, "NvAPI_Initialize");
    unload = (NvAPI_UnloadFn)query_nvapi_function(query_interface, QUERY_NVAPI_UNLOAD,
                                                  "NvAPI_Unload");
    enum_gpus = (NvAPI_EnumPhysicalGPUsFn)query_nvapi_function(
        query_interface, QUERY_NVAPI_ENUM_PHYSICAL_GPUS, "NvAPI_EnumPhysicalGPUs");

    if (initialize == NULL || unload == NULL || enum_gpus == NULL) {
        dlclose(library);
        return 1;
    }

    status = initialize();
    if (!check_nvapi_status(status, get_error_message, "NvAPI_Initialize")) {
        unload();
        dlclose(library);
        return 1;
    }

    status = enum_gpus(handles, &gpu_count);
    if (!check_nvapi_status(status, get_error_message, "NvAPI_EnumPhysicalGPUs")) {
        unload();
        dlclose(library);
        return 1;
    }

    if (gpu_count == 0) {
        fprintf(stderr, "No NVIDIA GPUs found.\n");
        unload();
        dlclose(library);
        return 1;
    }

    get_thermals = (NvAPI_GetThermalsFn)query_nvapi_function(
        query_interface, QUERY_NVAPI_THERMALS, "NvAPI_GetThermals");
    get_voltage = (NvAPI_GetVoltageFn)query_nvapi_function(query_interface, QUERY_NVAPI_VOLTAGE,
                                                           "NvAPI_GetVoltage");

    if (get_thermals != NULL) {
        thermals_mask =
            calculate_thermals_mask(get_thermals, get_error_message, handles[0]);
        if (thermals_mask != 0) {
            thermals.version = MAKE_NVAPI_VERSION(NvApiThermals, 2);
            thermals.mask = thermals_mask;
            status = get_thermals(handles[0], &thermals);
            if (check_nvapi_status(status, get_error_message, "NvAPI_GetThermals")) {
                has_hotspot =
                    read_temp_value(&thermals, NVAPI_THERMALS_HOTSPOT_INDEX, &hotspot_temp);
                has_memory =
                    read_temp_value(&thermals, NVAPI_THERMALS_VRAM_INDEX, &memory_temp);
            }
        }
    }

    if (get_voltage != NULL) {
        voltage.version = MAKE_NVAPI_VERSION(NvApiVoltage, 1);
        status = get_voltage(handles[0], &voltage);
        if (check_nvapi_status(status, get_error_message, "NvAPI_GetVoltage")) {
            has_voltage = 1;
        }
    }

    printf("GPU core voltage: ");
    if (has_voltage) {
        printf("%.0f mV\n", voltage.value_uv / 1000.0);
    } else {
        printf("N/A\n");
    }

    printf("GPU hotspot temperature: ");
    if (has_hotspot) {
        printf("%d C\n", hotspot_temp);
    } else {
        printf("N/A\n");
    }

    printf("GPU memory temperature: ");
    if (has_memory) {
        printf("%d C\n", memory_temp);
    } else {
        printf("N/A\n");
    }

    unload();
    dlclose(library);
    return 0;
}
