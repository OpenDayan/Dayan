# Dayan

Dayan（大衍）is an open-source NPU (Neural Processing Unit). This repository is the SoC that integrates Dayan: a RISC-V scalar core and a Dayan tensor core (matrix/vector/DMA), connected through an AXI-based on-chip interconnect.

中文说明[在此](readme.zh-cn.md)。

Copyright (c) 2026 Institute of Computing Technology, Chinese Academy of Sciences.

Dayan is licensed under the [Mulan Permissive Software License, Version 2](http://license.coscl.org.cn/MulanPSL2) (Mulan PSL v2). See [LICENSE](LICENSE) for the full license text.

## Architecture

Dayan is a heterogeneous NPU. Each Dayan NPU core pairs a scalar core (for control flow and instruction issue) with a tensor core (for matrix, vector and DMA computation). The SoC exposes a front (slave) AXI port for the host to push work and a master AXI port to reach external memory, while an internal system crossbar routes traffic between the cores, the control block and the outside world.

The micro-architecture overview of the Dayan SoC is shown below.

![dayan-arch](images/dayan-arch.png)

## Sub-directories Overview

Some of the key directories are shown below.

```
.
├── src
│   ├── main/scala         # design files
│   │   └── rvtsoc
│   │       ├── blackbox   # empty stubs used when a submodule is absent
│   │       ├── config     # CDE configurations
│   │       ├── devices    # AXI DPI and auxiliary devices for simulation
│   │       ├── subsystem  # NPU core, control block and system crossbar
│   │       ├── top        # top module (Rvtsoc) and simulation wrapper
│   │       └── utils      # shared utilities
│   ├── main/csrc          # C/C++ sources for the simulation harness
│   └── main/resources     # boot ROM and other resources
├── sim                    # Verilator-based co-simulation environment
│   ├── src                # simulator host code, elf loader, DPI devices
│   └── third_party        # spdlog, dotenv-cpp
├── scripts                # scripts for agile development
├── deps                   # git submodules (rocket-chip, AxiInfra, etc.)
└── build.mill             # mill build definition
```

## Update Dependencies

Dayan relies on several git submodules (e.g. `rocket-chip`, `AxiInfra`, `WiredRV`, the tensor core `rvt`, and `difftest`). After cloning, initialize and update them recursively:

```bash
make deps
```

`scripts/update-submodules.py` also recurses into the nested submodules of each dependency.

## Generate Verilog

* Run `make` (the default target is `verilog`) to generate SystemVerilog for simulation. Output is written to `generated/TestRvtsoc/`.
* Run `make verilog-fpga` to generate the FPGA wrapper `RvtsocWrapper` instead. Output is written to `generated/RvtsocWrapper/`.
* Refer to `makefile` and `build.mill` for the available `CONFIG` / top-module options.

## Run Programs by Simulation

### Prepare environment

* Install `mill`. Refer to [the mill installation guide](https://mill-build.org/).
* Clone this project and run `make deps` to initialize the submodules.
* Install [Verilator](https://verilator.org/guide/latest/), the open-source Verilog simulator. If it is not on the default path, set the `VERILATOR_ROOT` environment variable.
* The simulator also depends on `cmake`, `clang`/`clang++`, and the system libraries `fuse3`, `libzstd` and `zlib`.

### Run with the simulator

* `make sim-config` configures the simulation build with CMake. Common overrides (defined in `makefile`):

  | Variable          | Default        | Description                                        |
  | ------------------ | -------------- | -------------------------------------------------- |
  | `CONFIG`           | `VerifRvtsocConfig` | CDE configuration used for RTL generation       |
  | `SIM_C_COMPILER`   | `clang`        | C compiler for the simulation harness              |
  | `SIM_CXX_COMPILER` | `clang++`      | C++ compiler for the simulation harness            |
  | `SIM_THREADS`      | `16`           | Number of threads passed to Verilator              |
  | `SIM_TRACE_FST`    | `ON`           | Toggle FST waveform tracing                        |
  | `SIM_TOP_NAME`     | `TestRvtsoc`   | Top-level module to simulate                       |

* `make sim -j` builds the simulator and runs it.
* The compiled executable is `sim/build/TestRvtsoc`; waveform files are written to `sim/logs/*.fst`.

Example:

```bash
make sim-config
make sim -j
```

## Develop

### Add extra files and include paths for simulation

Extra C/C++ sources and include paths can be attached to the generated simulation from Scala via `ExtraFiles`:

```scala
import rvtsoc.utils.ExtraFiles
ExtraFiles.add("test.sv") // test.sv
ExtraFiles.add("test.c") // test.c
ExtraFiles.addResource("/rvtsoc/test.c") // src/main/resources/rvtsoc/test.c
ExtraFiles.addIncludePath("include") // include
ExtraFiles.addResourceIncludePath("/rvtsoc") // src/main/resources/rvtsoc
```

## License

This repository is licensed under Mulan PSL v2 (see [LICENSE](LICENSE)). The standalone `license-header.txt` is only the header snippet prepended to each Scala source file; it is not the license itself. To verify or apply the header:

```bash
./mill __.license.check    # verify all Scala files carry the header
./mill __.license.create   # add the header where it is missing
```
