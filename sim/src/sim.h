#ifndef SIM_H
#define SIM_H

#include <verilated.h>

#include <memory>

extern void system_tick();

extern std::unique_ptr<VerilatedContext> contextp;

extern bool is_shutting_down;

#endif  // SIM_H