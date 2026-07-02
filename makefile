base_dir=$(abspath ./)

MILL ?= ./mill
CMAKE ?= cmake

# Default configuration, defined in src/main/scala/rvtsoc/config/
CONFIG ?= RvtsocConfig

VERILOG_TOP = TestRvtsoc
VERILOG_FPGA_TOP = RvtsocWrapper
VERILOG_VLSI_TOP ?= Rvtsoc
VERILOG_VLSI_DIR = $(base_dir)/generated/npu-vlsi
VERILOG_VLSI_MEM_CONF = npu.mem.conf
VERILOG_VLSI_MEM_GEN_CONF = .npu.mem.vlsi_mem_gen.conf
VERILOG_VLSI_MEM_SV = npu.mem.sv

SIM_C_COMPILER ?= clang
SIM_CXX_COMPILER ?= clang++
SIM_THREADS ?= 16
SIM_TRACE_FST ?= ON
SIM_TOP_NAME ?= $(VERILOG_TOP)

SIM_DIR = $(base_dir)/sim
SIM_BUILD_DIR = $(SIM_DIR)/build
SIM_EXE = $(SIM_BUILD_DIR)/$(VERILOG_TOP)

.PHONY: verilog verilog-* sim sim-* clean deps test

# first target is the default
default: verilog

deps:
	scripts/update-submodules.py

test:
	$(MILL) $(JAVA_OPTS) __.test

# verilog targets

verilog:
	@cd $(base_dir) && $(MILL) $(JAVA_OPTS) -D top=$(VERILOG_TOP) -D config=$(CONFIG) gen

verilog-fpga:
	@cd $(base_dir) && $(MILL) $(JAVA_OPTS) -D top=$(VERILOG_FPGA_TOP) -D config=$(CONFIG) gen

verilog-vlsi:
	@cd $(base_dir) && $(MILL) -D dir=$(VERILOG_VLSI_DIR) -D config=npu.config.$(CONFIG) npu.genVlsi
	@cd $(base_dir) && ./deps/rocket-chip/scripts/vlsi_mem_gen $(VERILOG_VLSI_DIR)/npu.mem.conf -o $(VERILOG_VLSI_DIR)/npu.mem.sv

verilog-clean:
	rm -rf out/ generated/

# simulation targets

sim-config: verilog
	@$(MAKE) -s sim-clean
	@echo -e "\033[34mSIM_C_COMPILER = $(SIM_C_COMPILER)\033[0m"
	@echo -e "\033[34mSIM_CXX_COMPILER = $(SIM_CXX_COMPILER)\033[0m"
	@echo -e "\033[34mSIM_THREADS = $(SIM_THREADS)\033[0m"
	@echo -e "\033[34mSIM_TRACE_FST = $(SIM_TRACE_FST)\033[0m"
	@echo -e "\033[34mSIM_TOP_NAME = $(SIM_TOP_NAME)\033[0m"
	@$(CMAKE) -S $(SIM_DIR) -B $(SIM_BUILD_DIR) -G "Unix Makefiles" \
		-DCMAKE_C_COMPILER=$(SIM_C_COMPILER) \
		-DCMAKE_CXX_COMPILER=$(SIM_CXX_COMPILER) \
		-DTHREADS=$(SIM_THREADS) \
		-DTRACE_FST=$(SIM_TRACE_FST) \
		-DTOP_NAME=$(SIM_TOP_NAME)
	@ln -f -s $(SIM_BUILD_DIR)/compile_commands.json $(base_dir)/compile_commands.json

sim-build: verilog
	@if [ ! -d $(SIM_BUILD_DIR) ]; then \
	  echo -e "\033[31mSimulation build directory not found.\033[0m\nPlease run \033[1m'make sim-config [options]'\033[0m first."; \
	  exit 1; \
	fi
	@$(MAKE) -s -C $(SIM_BUILD_DIR)

sim: sim-build
	@cd $(SIM_DIR) && $(SIM_EXE)

sim-clean:
	rm -rf $(SIM_BUILD_DIR)/

# clean all generated files
clean: verilog-clean sim-clean
