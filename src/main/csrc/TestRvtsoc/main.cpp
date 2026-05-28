#include <laserpants/dotenv/dotenv.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <verilated.h>
#include <verilated_fst_c.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include "VTestRvtsoc.h"
#include "cliUtils.h"
#include "configuration.h"
#include "devices/DPIAXI4.h"
#include "devices/axi4master.h"
#include "devices/ddr4.h"
#include "spdlog/spdlog.h"
#include "utils/bar.h"
#include "utils/config.h"
#include "utils/fuse_driver.h"
#include "utils/logger.h"

const size_t reset_cycles = 5;
const std::filesystem::path log_path = "logs";
const std::filesystem::path fst_log_file = log_path / "TestRvtsoc.fst";

const std::string default_ddr_config =
    "../deps/RVTNG/csrc/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini";
const std::string default_mount_point = "/tmp/virtual_npu";

std::shared_ptr<spdlog::logger> logger = nullptr;
std::unique_ptr<VerilatedContext> contextp = nullptr;
std::unique_ptr<VTestRvtsoc> top = nullptr;
#if VM_TRACE_FST
std::shared_ptr<VerilatedFstC> m_trace = nullptr;
#endif
std::unique_ptr<std::thread> fuse_thread;
static uint64_t dump_offset;
bool is_shutting_down = false;
const size_t max_cycle = 100'000;

void shutdown() {
  logger->info("Try to Shutting down simulation...");
  is_shutting_down = true;
#if VM_TRACE
  if (m_trace) {
    logger->info("Export Trace...");
    m_trace->dump(contextp->time() - dump_offset);
    m_trace->close();
    m_trace = nullptr;
  }
#endif
  if (!FuseDriver::session_exited()) {
    logger->info("Try to stop fuse thread...");
    FuseDriver::session_stop();
  }
  // exit(EXIT_SUCCESS);
}

std::atomic<uint32_t> stopFlag = 0;
// ctrl-C handler
void sigint_handler(int signum) {
  stopFlag += 2;
  if (signum == SIGINT && stopFlag <= 4) {
    logger->info("Caught interrupt signal {} (SIGINT), stopping simulation...",
                 signum);
    return;
  }

  logger->error("Interrupted with signal {} ({})", signum, strsignal(signum));
  shutdown();
  std::cerr << strsignal(signum) << "!" << std::endl;
  exit(signum);
}

void run_next_cycle(uint64_t n, bool print_progress) {
  static double npu_time = 0;
  static double dram_time = 0;
  unsigned long realN = n;
  for (unsigned long i = 0; i < realN;) {
    top->clock = 1;
    DDR4Mod.tick();
    npu_time += npu_cycle;
    while (npu_time > dram_time) {
      DDR4Mod.tick();
      dram_time += dram_cycle;
    }
    top->eval();
#if VM_TRACE
    if (enable_waveform)
      m_trace->dump(contextp->time() - dump_offset);
#endif
    contextp->timeInc(1);
    top->clock = 0;
    top->eval();

#if VM_TRACE
    if (enable_waveform)
      m_trace->dump(contextp->time() - dump_offset);
#endif
    contextp->timeInc(1);

    i++;
    if (print_progress) {
      print_progress_with_speed_multithread(i, realN, 0);

      if (stopFlag != 0) {
        if ((--stopFlag) == 1)
          break;
      }
    }
  }

  if (print_progress) {
    print_progress_with_speed_multithread(realN, realN, 0);
    wait_for_render_completion();
    if (!stopFlag) {
      printf(
          "\033[42;32mVerilator: Simulator Run Next Cycle Success!\033[0m\n");
    } else {
      printf("\033[41;32mVerilator: Simulator Run Next Cycle FAILED!\033[0m\n");
    }
    stopFlag = 0;
  }
}

void run_until_success() {
  while ((!top->io_success) && (contextp->time() < max_cycle) && (!stopFlag)) {
    run_next_cycle(1, false);
  }
  stopFlag = 0;
}

void vl_fatal(const char *filename, int line, const char *func,
              const char *msg) {
  std::cerr << "Fatal error occurred at " << filename << ":" << line
            << " (function: " << func << ") - " << msg << std::endl;
  stopFlag = 2;
}

void vl_stop(const char *filename, int line, const char *func) {
  std::cerr << "stop occurred at " << filename << ":" << line
            << " (function: " << func << ")" << std::endl;
  stopFlag = 2;
}

void dump_waveform(void) {
#if VM_TRACE
  if (contextp->time() != dump_offset) {
    m_trace->close();
    start_surfer_process((char *)fst_log_file.c_str());
    m_trace = std::make_unique<VerilatedFstC>();
    top->trace(m_trace.get(), 99);
    sleep(2);
    dump_offset = contextp->time();
    m_trace->open(fst_log_file.c_str());
  }
#else
  logger->warn("Waveform dump is disabled. Rebuild with VM_TRACE to enable.");
#endif
}

void test_thread_entry() {
  const uint64_t bootrom_base = 0x10000;
  const size_t bootrom_size = 0xc0;

  const uint64_t boot_addr_base = 0x1000;

  const uint64_t clint_base = 0x20000;
  const size_t n_cores = 1;

  // test Bootrom
  logger->info(
      "Read Bootrom: {}",
      spdlog::to_hex(*AXI4Master.read(bootrom_base, bootrom_size), 16));

  // load hello_multicore.bin into DRAM at 0x80000000
  {
    const std::filesystem::path bin_path =
        "src/software/helloworld/hello.bin";
    std::ifstream bin_file(bin_path, std::ios::binary | std::ios::ate);
    if (!bin_file) {
      logger->error("Failed to open binary: {}", bin_path.string());
      return;
    }
    auto bin_size = bin_file.tellg();
    bin_file.seekg(0);
    std::vector<uint8_t> bin_data(bin_size);
    bin_file.read(reinterpret_cast<char *>(bin_data.data()), bin_size);
    logger->info("Loading {} ({} bytes) to 0x80000000", bin_path.string(),
                 bin_data.size());
    AXI4Master.write(0x80000000, bin_data);
  }

  // test boot addr reg
  // logger->info("Read boot addr reg: {:#x}",
  //              AXI4Master.read64(boot_addr_base));
  AXI4Master.write32(boot_addr_base, 0x80000000);

  // test clint mtime
  // logger->info("Read clint msip mtime: {:#x}",
  //              AXI4Master.read64(client_base + 0xbff8));

  // test clint msip
  // for (size_t i = 0; i < n_cores; i++) {
  //   logger->debug("test clint msip hart {}: write msip", i);
  //   AXI4Master.write32(client_base + i * 4, 1);
  // }
  AXI4Master.write32(clint_base, 1);
}

int main(int argc, char **argv) {
  dotenv::init();

  const auto ddr_config_file =
      getEnvOrDefault("SIM_DRAM_CONFIG", default_ddr_config);
  const auto mount_point = std::filesystem::path(
      getEnvOrDefault("SIM_MOUNT_POINT", default_mount_point));

  logger = spdlog::stderr_color_mt("main");
  logger->set_formatter(std::move(make_sim_formatter()));
  logger->set_level(spdlog::level::info);

  std::filesystem::create_directory(log_path);
  // fuse filesystem
  __attribute__((unused)) int ret =
      system((char *)("umount " + mount_point.string()).c_str());
  // mkdir
  std::filesystem::create_directory(mount_point);

  const char *fuse_argv[] = {
      "npu_fs",
      "-f", // foreground
      "-s", // single-threaded
      // "-d",        // enable debug output
      mount_point.c_str() // mount point
  };
  auto fuse_thread = std::make_unique<std::thread>([&]() {
    FuseDriver::main(sizeof(fuse_argv) / sizeof(fuse_argv[0]),
                     (char **)fuse_argv);
  });

  signal(SIGINT, sigint_handler);
  signal(SIGABRT, sigint_handler);
  signal(SIGSEGV, sigint_handler);

  // axi4 master dpi
  AXI4Master.init();
  // dramsim3
  logger->info("DRAMSim3 Config File: {}", ddr_config_file);
  auto ddr_config = dramsim3::Config(ddr_config_file, log_path);
  // DPIAXI4InitMem(ddr_config);
  DDR4Mod.init(ddr_config);

  // init_ram("/dev/zero", DEFAULT_EMU_RAM_SIZE);

  // verilator
  // Verilated::mkdir(log_path.c_str());
  contextp = std::unique_ptr<VerilatedContext>(new VerilatedContext);

  contextp->debug(0);
  contextp->randReset(2);
  contextp->traceEverOn(true);
  contextp->commandArgs(argc, argv);

  top = std::unique_ptr<VTestRvtsoc>(new VTestRvtsoc{contextp.get(), "TOP"});

#if VM_TRACE_FST
  m_trace = std::make_unique<VerilatedFstC>();
  top->trace(m_trace.get(), 0);
  m_trace->open(fst_log_file.c_str());
#endif

  top->reset = 1;
  for (size_t i = 0; i < reset_cycles && !contextp->gotFinish(); i++) {
    top->clock = 1;
    top->eval();
#if VM_TRACE
    if (enable_waveform)
      m_trace->dump(contextp->time() - dump_offset);
#endif
    contextp->timeInc(1);
    top->clock = 0;
    top->eval();
#if VM_TRACE
    if (enable_waveform)
      m_trace->dump(contextp->time() - dump_offset);
#endif
    contextp->timeInc(1);
  }
  top->reset = 0;
  run_next_cycle(1, false);

  logger->info("Starting main CLI loop...");
  std::thread test_thread(test_thread_entry);
  test_thread.detach();
  cli_main();
  // run_until_success();
  for (int tryI = 0; true; tryI++) {
    shutdown();
    if (FuseDriver::session_exited()) {
      fuse_thread->detach();
      break;
    }
    if (tryI >= 100) {
      logger->error("FUSE did not exit after 10s, forcing shutdown...");
      break;
    }
    if (tryI % 10 == 0)
      logger->warn("Waiting for FUSE to exit... ({}s)", tryI / 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return EXIT_SUCCESS;
}
