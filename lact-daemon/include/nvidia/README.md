These headers are taken from https://github.com/NVIDIA/open-gpu-kernel-modules.

## GPU metrics sample

`gpu-metrics.c` is a standalone C example that queries GPU core voltage, hotspot
temperature, and memory temperature via the NVIDIA NvAPI shared library on
Linux.

Build and run:

```sh
cc -O2 -Wall -Wextra -ldl -I. -o gpu-metrics gpu-metrics.c
./gpu-metrics
```
