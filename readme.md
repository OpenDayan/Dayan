# Rvtsoc

## update dependencies

```bash
make deps
```

## build

### verilog for simulation

```bash
make
```

### verilog for VLSI

```bash
make verilog-vlsi
```
## sim

```bash
make sim-config
make sim -j
```

## develop

### add extra file and include path for simulation

```scala
import rvtsoc.utils.ExtraFiles
ExtraFiles.add("test.sv") // test.sv
ExtraFiles.add("test.c") // test.c
ExtraFiles.addResource("/rvtsoc/test.c") // src/main/resources/rvtsoc/test.c
ExtraFiles.addIncludePath("include") // include
ExtraFiles.addResourceIncludePath("/rvtsoc") // src/main/resources/rvtsoc
```
