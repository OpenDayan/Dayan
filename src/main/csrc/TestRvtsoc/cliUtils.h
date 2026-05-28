#ifndef CLI_UTILS_H
#define CLI_UTILS_H

#include <sys/types.h>

#include <cstdint>

extern int enable_waveform;
extern double npu_cycle, dram_cycle, npu_freq, dram_freq;
pid_t start_surfer_process(char *filename);
int cli_main();

// 需要实现的函数声明
void run_next_cycle(uint64_t n, bool print_progress);
// void run_until_empty(void);
void dump_waveform(void);

#endif