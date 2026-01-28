/*
 * NVIDIA GPU Monitor - Simple C utility to display GPU metrics
 * 
 * This utility uses NVAPI to display:
 * - Core voltage
 * - Hotspot temperature
 * - Memory temperature
 * 
 * Based on undocumented NVAPI calls:
 * - QUERY_NVAPI_THERMALS: 0x65fe3aad
 * - QUERY_NVAPI_VOLTAGE: 0x465f9bcf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

#define LIBRARY_NAME "libnvidia-api.so.1"
#define QUERY_INTERFACE_FN "nvapi_QueryInterface"

// NVAPI Query IDs
#define QUERY_NVAPI_INITIALIZE 0x0150e828
#define QUERY_NVAPI_UNLOAD 0xd22bdd7e
#define QUERY_NVAPI_ENUM_PHYSICAL_GPUS 0xe5ac921f
#define QUERY_NVAPI_THERMALS 0x65fe3aad  // Undocumented call
#define QUERY_NVAPI_VOLTAGE 0x465f9bcf   // Undocumented call

#define NVAPI_MAX_PHYSICAL_GPUS 64
#define NVAPI_THERMAL_VALUES 40

// Version constants for NVAPI structures
#define NVAPI_THERMALS_VERSION 2
#define NVAPI_VOLTAGE_VERSION 1

// Thermal sensor indices
#define THERMAL_INDEX_HOTSPOT 9
#define THERMAL_INDEX_MEMORY 15

// Type definitions based on NVAPI
typedef int NvAPI_Status;
typedef struct NvPhysicalGpuHandle__ { int unused; } *NvPhysicalGpuHandle;

// Thermal sensor data structure
typedef struct {
    uint32_t version;
    int32_t mask;
    int32_t values[NVAPI_THERMAL_VALUES];
} NvApiThermals;

// Voltage data structure
typedef struct {
    uint32_t version;
    uint32_t flags;
    uint32_t padding_1[8];
    uint32_t value_uv;
    uint32_t padding_2[8];
} NvApiVoltage;

// Function pointer types
typedef void* (*QueryInterfaceFn)(uint32_t id);
typedef NvAPI_Status (*NvAPI_InitializeFn)(void);
typedef NvAPI_Status (*NvAPI_UnloadFn)(void);
typedef NvAPI_Status (*NvAPI_EnumPhysicalGPUsFn)(NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS], uint32_t *count);
typedef NvAPI_Status (*NvAPI_GetThermalsFn)(NvPhysicalGpuHandle handle, NvApiThermals *sensors);
typedef NvAPI_Status (*NvAPI_GetVoltageFn)(NvPhysicalGpuHandle handle, NvApiVoltage *data);

// Global library handle
static void *lib_handle = NULL;
static QueryInterfaceFn query_interface = NULL;

// Load the NVAPI library
int load_nvapi_library(void) {
    lib_handle = dlopen(LIBRARY_NAME, RTLD_LAZY);
    if (!lib_handle) {
        fprintf(stderr, "Error: Could not load %s: %s\n", LIBRARY_NAME, dlerror());
        return -1;
    }

    query_interface = (QueryInterfaceFn)dlsym(lib_handle, QUERY_INTERFACE_FN);
    if (!query_interface) {
        fprintf(stderr, "Error: Could not find %s: %s\n", QUERY_INTERFACE_FN, dlerror());
        dlclose(lib_handle);
        lib_handle = NULL;
        return -1;
    }

    return 0;
}

// Get a function pointer from NVAPI using query interface
void* get_nvapi_function(uint32_t id) {
    if (!query_interface) {
        return NULL;
    }
    return query_interface(id);
}

// Initialize NVAPI
int initialize_nvapi(void) {
    NvAPI_InitializeFn initialize = (NvAPI_InitializeFn)get_nvapi_function(QUERY_NVAPI_INITIALIZE);
    if (!initialize) {
        fprintf(stderr, "Error: Could not get initialize function\n");
        return -1;
    }

    NvAPI_Status status = initialize();
    if (status != 0) {
        fprintf(stderr, "Error: NvAPI_Initialize failed with status: %d\n", status);
        return -1;
    }

    return 0;
}

// Enumerate physical GPUs
int enum_physical_gpus(NvPhysicalGpuHandle *handles, uint32_t *count) {
    NvAPI_EnumPhysicalGPUsFn enum_gpus = (NvAPI_EnumPhysicalGPUsFn)get_nvapi_function(QUERY_NVAPI_ENUM_PHYSICAL_GPUS);
    if (!enum_gpus) {
        fprintf(stderr, "Error: Could not get enum physical GPUs function\n");
        return -1;
    }

    NvPhysicalGpuHandle gpu_handles[NVAPI_MAX_PHYSICAL_GPUS];
    memset(gpu_handles, 0, sizeof(gpu_handles));
    
    NvAPI_Status status = enum_gpus(gpu_handles, count);
    if (status != 0) {
        fprintf(stderr, "Error: NvAPI_EnumPhysicalGPUs failed with status: %d\n", status);
        return -1;
    }

    memcpy(handles, gpu_handles, sizeof(NvPhysicalGpuHandle) * (*count));
    return 0;
}

// Get thermal sensors data
int get_thermals(NvPhysicalGpuHandle handle, NvApiThermals *sensors) {
    NvAPI_GetThermalsFn get_thermals_fn = (NvAPI_GetThermalsFn)get_nvapi_function(QUERY_NVAPI_THERMALS);
    if (!get_thermals_fn) {
        fprintf(stderr, "Error: Could not get thermals function\n");
        return -1;
    }

    memset(sensors, 0, sizeof(NvApiThermals));
    sensors->version = (sizeof(NvApiThermals) | (NVAPI_THERMALS_VERSION << 16));
    sensors->mask = 1;

    NvAPI_Status status = get_thermals_fn(handle, sensors);
    if (status != 0) {
        fprintf(stderr, "Error: NvAPI_GetThermals failed with status: %d\n", status);
        return -1;
    }

    return 0;
}

// Get voltage data
int get_voltage(NvPhysicalGpuHandle handle, uint32_t *voltage_uv) {
    NvAPI_GetVoltageFn get_voltage_fn = (NvAPI_GetVoltageFn)get_nvapi_function(QUERY_NVAPI_VOLTAGE);
    if (!get_voltage_fn) {
        fprintf(stderr, "Error: Could not get voltage function\n");
        return -1;
    }

    NvApiVoltage data;
    memset(&data, 0, sizeof(NvApiVoltage));
    data.version = (sizeof(NvApiVoltage) | (NVAPI_VOLTAGE_VERSION << 16));
    data.flags = 0;

    NvAPI_Status status = get_voltage_fn(handle, &data);
    if (status != 0) {
        fprintf(stderr, "Error: NvAPI_GetVoltage failed with status: %d\n", status);
        return -1;
    }

    *voltage_uv = data.value_uv;
    return 0;
}

// Extract temperature value from thermal sensor data
int get_temp_value(NvApiThermals *sensors, int index) {
    if (index < 0 || index >= NVAPI_THERMAL_VALUES) {
        return -1;
    }
    
    int value = sensors->values[index] / 256;
    
    // Filter invalid values
    if (value <= 0 || value > 255) {
        return -1;
    }
    
    return value;
}

// Cleanup and unload NVAPI
void cleanup_nvapi(void) {
    if (lib_handle) {
        NvAPI_UnloadFn unload = (NvAPI_UnloadFn)get_nvapi_function(QUERY_NVAPI_UNLOAD);
        if (unload) {
            unload();
        }
        dlclose(lib_handle);
        lib_handle = NULL;
    }
}

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    printf("NVIDIA GPU Monitor\n");
    printf("==================\n\n");

    // Load the NVAPI library
    if (load_nvapi_library() != 0) {
        return 1;
    }

    // Initialize NVAPI
    if (initialize_nvapi() != 0) {
        cleanup_nvapi();
        return 1;
    }

    // Enumerate GPUs
    NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS];
    uint32_t gpu_count = 0;
    
    if (enum_physical_gpus(handles, &gpu_count) != 0) {
        cleanup_nvapi();
        return 1;
    }

    printf("Found %u NVIDIA GPU(s)\n\n", gpu_count);

    // Process each GPU
    for (uint32_t i = 0; i < gpu_count; i++) {
        printf("GPU %u:\n", i);
        printf("------\n");

        // Get voltage
        uint32_t voltage_uv = 0;
        if (get_voltage(handles[i], &voltage_uv) == 0) {
            printf("  Core Voltage: %.3f V (%u µV)\n", voltage_uv / 1000000.0, voltage_uv);
        } else {
            printf("  Core Voltage: Not available\n");
        }

        // Get thermal sensors
        NvApiThermals sensors;
        if (get_thermals(handles[i], &sensors) == 0) {
            // Hotspot temperature
            int hotspot_temp = get_temp_value(&sensors, THERMAL_INDEX_HOTSPOT);
            if (hotspot_temp > 0) {
                printf("  Hotspot Temperature: %d °C\n", hotspot_temp);
            } else {
                printf("  Hotspot Temperature: Not available\n");
            }

            // Memory temperature
            int memory_temp = get_temp_value(&sensors, THERMAL_INDEX_MEMORY);
            if (memory_temp > 0) {
                printf("  Memory Temperature: %d °C\n", memory_temp);
            } else {
                printf("  Memory Temperature: Not available\n");
            }
        } else {
            printf("  Temperature data: Not available\n");
        }

        printf("\n");
    }

    // Cleanup
    cleanup_nvapi();
    
    return 0;
}
