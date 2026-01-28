# NVIDIA GPU Monitor

A simple C utility to display NVIDIA GPU metrics using NVAPI.

## Features

This tool displays the following GPU information:
- **Core Voltage** (in Volts and microvolts)
- **Hotspot Temperature** (in Celsius)
- **Memory Temperature** (in Celsius)

## Implementation Details

The tool uses undocumented NVAPI calls to access GPU metrics:
- `QUERY_NVAPI_THERMALS` (0x65fe3aad) - Thermal sensor data
- `QUERY_NVAPI_VOLTAGE` (0x465f9bcf) - Voltage information

These calls are accessed through the `libnvidia-api.so.1` library using the `nvapi_QueryInterface` function.

## Requirements

- NVIDIA GPU with proprietary drivers installed
- `libnvidia-api.so.1` library (included with NVIDIA drivers)
- GCC compiler
- Linux operating system

## Building

To build the utility:

```bash
cd nvidia
make
```

This will create the `gpu_monitor` executable.

## Usage

Run the compiled executable:

```bash
./gpu_monitor
```

Example output:
```
NVIDIA GPU Monitor
==================

Found 1 NVIDIA GPU(s)

GPU 0:
------
  Core Voltage: 0.850 V (850000 µV)
  Hotspot Temperature: 45 °C
  Memory Temperature: 42 °C
```

## Installation

To install system-wide:

```bash
sudo make install
```

To uninstall:

```bash
sudo make uninstall
```

## Notes

- The tool requires NVIDIA proprietary drivers to be installed
- Some metrics may not be available on all GPU models
- The thermal sensor indices (9 for hotspot, 15 for memory) are based on the LACT daemon implementation
- This is a simple demonstration tool based on the existing Rust implementation in the LACT project

## Based On

This C implementation is based on the Rust NVAPI implementation found in:
`lact-daemon/src/server/gpu_controller/nvidia/nvapi.rs`
