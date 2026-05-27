#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/prctl.h>

#include "cliUtils.h"

// 新增头文件
#include <termios.h>
#include <errno.h>

// 供 DRAMsim3 使用，动态 CPU 频率调整
double npu_cycle = 1000.f / 250.f, dram_cycle = 1000.f / 1200.f, npu_freq = 250.f, dram_freq = 1200.f;

// 历史记录相关定义
#define MAX_HISTORY_SIZE 100
#define MAX_LINE_LENGTH 256

// 历史记录结构
typedef struct {
    char commands[MAX_HISTORY_SIZE][MAX_LINE_LENGTH];
    int count;
    int current;
} History;

// 全局历史记录变量
static History history = { .count = 0, .current = 0 };

// 全局波形记录状态
#ifdef VM_TRACE
int enable_waveform = 1;
#else
int enable_waveform = 0;
#endif

// 函数声明
static inline void process_command(const char *command);
static inline int parse_number(const char *str, int *result);

// 新增函数声明
void init_terminal(void);
void restore_terminal(void);
int read_line_with_history(char *buffer, int size);
void add_to_history(const char *command);
void show_previous_history(char *buffer);
void show_next_history(char *buffer);
void clear_line(int length);
void print_prompt(void);

/**
 * 启动子进程
 * 如果之前有存活的子进程则先杀死
 * @return 成功返回子进程PID，失败返回-1
 */
pid_t start_surfer_process(char* filename) {
    static pid_t child_pid = 0;

    // 检查并杀死已存在的子进程
    if (child_pid > 0) {
        // 检查进程是否存活
        if (kill(child_pid, 0) == 0) {
            // 先尝试优雅终止
            if (kill(child_pid, SIGTERM) == 0) {
                // 等待进程终止
                for (int i = 0; i < 50; ++i) {
                    if (kill(child_pid, 0) == -1) {
                        break; // 进程已终止
                    }
                    usleep(100000);
                }

                // 优雅终止失败，强制终止
                if (kill(child_pid, 0) == 0) {
                    kill(child_pid, SIGKILL);
                }
            }
        }
    }

    // 创建新的子进程
    pid_t pid = fork();

    if (pid == -1) {
        return -1;
    }
    else if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull < 0) {
            perror("open /dev/null");
            exit(1);
        }
        dup2(devnull, STDIN_FILENO);
        close(devnull);
        // 子进程随父进程退出而结束
        prctl(PR_SET_PDEATHSIG, SIGHUP);
        // 子进程执行命令
        execlp("surver", "surver", filename,
               "--port", "4000", "--token", "00000000", NULL);
        exit(EXIT_FAILURE);
    }
    else {
        // 父进程记录信息
        child_pid = pid;

        // 设置信号处理避免僵尸进程
        signal(SIGCHLD, SIG_IGN);

        return pid;
    }
}

// 模拟的函数实现
static inline void run_next_cycle_abstract(uint64_t n) {
    if(n < 2) {
        n = 2;
    }
    printf("执行 run_next_cycle(%ld)\n", n);
    run_next_cycle(n);
}

// static inline void run_until_empty_abstract(void) {
//     printf("执行 run_until_empty()\n");
//     run_until_empty();
// }

static inline void dump_waveform_abstract(void) {
    printf("执行 dump_waveform()\n");
    dump_waveform();
}

static inline int parse_number(const char *str, uint64_t *result) {
    char *endptr;
    uint64_t num = strtol(str, &endptr, 10);

    if (endptr == str) {
        return 0;
    }

    if(endptr[0] == 'k' || endptr[0] == 'K') {
        num *= 1000;
    }

    *result = num;
    return 1;
}

static inline void process_command(const char *command) {
    while (isspace((unsigned char)*command)) {
        command++;
    }

    if (*command == '\0' || *command == '#') {
        return;
    }

    switch (command[0]) {
        case 'n': {
            const char *arg;
            // n 命令：n [十进制数字]
            if(command[1] == 'd') arg = command + 2;
            else arg = command + 1;

            // 跳过空格
            while (isspace((unsigned char)*arg)) {
                arg++;
            }

            if (*arg == '\0') {
                printf("错误：n 命令需要参数\n");
                return;
            }

            uint64_t n;
            if (parse_number(arg, &n)) {
                run_next_cycle_abstract(n);
            } else {
                printf("错误：无效的数字参数 '%s'\n", arg);
            }
            if(command[1] == 'd') {
                dump_waveform_abstract();
            }
            break;
        }

        case 'f': {
            const char *arg;
            // f 命令：fn [十进制数字] or fd
            arg = command + 2;

            // 跳过空格
            while (isspace((unsigned char)*arg)) {
                arg++;
            }

            if (*arg == '\0') {
                printf("错误：f 命令需要参数\n");
                return;
            }

            uint64_t n;
            if (parse_number(arg, &n)) {
                if(command[1] == 'n') {
                    npu_freq = (double) n;
                    npu_cycle = 1000.f / npu_freq;
                    printf("调整 NPU 频率为 %lfMHz (周期长 %0.4lf ns)\n", npu_freq, npu_cycle);
                } else if(command[1] == 'd') {
                    dram_freq = (double) n;
                    dram_cycle = 1000.f / dram_freq;
                    printf("调整 DRAM 频率为 %lfMHz (周期长 %0.4lf ns)\n", dram_freq, dram_cycle);
                } else {
                    printf("错误：f[d/n] 命令需要制定目标 [d/n]\n");
                }
            } else {
                printf("错误：无效的数字参数 '%s'\n", arg);
            }
            break;
        }

        case 'c':
            // run_until_empty();
            extern void run_until_success();
            run_until_success();
            break;

        case 'd':
            dump_waveform_abstract();
            break;

        case 't':
#if VM_TRACE
            enable_waveform = !enable_waveform;
            printf("目前波形记录状态：%s\n", enable_waveform ? "记录" : "不记录");
#else
            printf("波形记录功能未启用（请重新编译以支持波形记录）\n");
#endif
            break;

        default:
            printf("错误：未知命令 '%c'\n", command[0]);
            printf("可用命令：\n");
            printf("  n [数字] - 执行指定的周期数\n");
            // printf("  c        - 运行直到仿真终止标志指令\n");
            printf("  d        - 导出波形数据，并启用 surfer 查看\n");
            printf("  f [数字] - 指定 NPU 或 DRAM 的频率\n");
            printf("  t        - 切换波形记录状态\n");
            printf("  q        - 退出\n");
            break;
    }
}

// 新增函数实现

// 初始化终端设置（原始模式）
void init_terminal(void) {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);

    // 保存原始设置以便恢复
    static struct termios original_term;
    original_term = term;

    // 设置为非规范模式，禁用回显
    term.c_lflag &= ~(ICANON | ECHO);
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    // 注册退出时恢复终端的函数
    atexit(restore_terminal);
}

// 恢复终端设置
void restore_terminal(void) {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

// 添加命令到历史记录
void add_to_history(const char *command) {
    if (command[0] == '\0' || command[0] == '\n') {
        return; // 忽略空命令
    }

    // 如果与上一条命令相同，则不重复添加
    if (history.count > 0 && strcmp(history.commands[history.count - 1], command) == 0) {
        history.current = history.count;
        return;
    }

    // 如果历史记录已满，移除最旧的一条
    if (history.count >= MAX_HISTORY_SIZE) {
        for (int i = 1; i < MAX_HISTORY_SIZE; i++) {
            strcpy(history.commands[i - 1], history.commands[i]);
        }
        history.count--;
    }

    // 添加新命令
    strncpy(history.commands[history.count], command, MAX_LINE_LENGTH - 1);
    history.commands[history.count][MAX_LINE_LENGTH - 1] = '\0';
    history.count++;
    history.current = history.count;
}

// 显示上一条历史记录
void show_previous_history(char *buffer) {
    if (history.count == 0) {
        return;
    }

    if (history.current > 0) {
        history.current--;
    }

    clear_line(strlen(buffer));
    strcpy(buffer, history.commands[history.current]);
    printf("%s", buffer);
    fflush(stdout);
}

// 显示下一条历史记录
void show_next_history(char *buffer) {
    if (history.count == 0) {
        return;
    }

    if (history.current < history.count - 1) {
        history.current++;
        clear_line(strlen(buffer));
        strcpy(buffer, history.commands[history.current]);
        printf("%s", buffer);
    } else {
        history.current = history.count;
        clear_line(strlen(buffer));
        buffer[0] = '\0';
    }
    fflush(stdout);
}

// 清除当前行
void clear_line(int length) {
    printf("\r\033[K"); // 回车 + 清除从光标到行尾
    print_prompt();
}

// 打印提示符
void print_prompt(void) {
    printf("\033[32m➜\033[0m ");
    fflush(stdout);
}

// 带历史记录功能的读取行函数
int read_line_with_history(char *buffer, int size) {
    int pos = 0;
    buffer[0] = '\0';

    while (1) {
        char c;
        int n = read(STDIN_FILENO, &c, 1);

        if (n <= 0) {
            return -1;
        }

        // 处理特殊按键
        if (c == '\x1b') { // ESC 序列
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': // 上箭头
                        show_previous_history(buffer);
                        pos = strlen(buffer);
                        break;
                    case 'B': // 下箭头
                        show_next_history(buffer);
                        pos = strlen(buffer);
                        break;
                    case 'C': // 右箭头
                        // 可以在这里实现光标右移
                        break;
                    case 'D': // 左箭头
                        // 可以在这里实现光标左移
                        break;
                }
            }
        }
        // 处理退格键
        else if (c == 127 || c == '\b') { // 退格键
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                printf("\b \b"); // 退格、空格、再退格
                fflush(stdout);
            }
        }
        // 处理回车键
        else if (c == '\n' || c == '\r' || c == ';') {
            printf("\n");
            buffer[pos] = '\0';

            // 添加到历史记录（非空命令）
            if (pos > 0) {
                add_to_history(buffer);
            }

            return pos;
        }
        // 处理普通字符
        else if (c >= 32 && c <= 126 && pos < size - 1) {
            buffer[pos++] = c;
            buffer[pos] = '\0';
            printf("%c", c);
            fflush(stdout);
        }
    }
}

void debug_stdin_status() {
    printf("stdin status:\n");
    printf("  feof: %d\n", feof(stdin));
    printf("  ferror: %d\n", ferror(stdin));
    printf("  errno: %d\n", errno);
    fflush(stdout);
}

void reset_stdin() {
    clearerr(stdin);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags & O_NONBLOCK) {
        fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    }
}

// RVT CLI
int cli_main() {
    char input[MAX_LINE_LENGTH];

    printf("=================== RVTNG CLI Shell ===================\n");
    printf("输入 'quit' 或 'exit' 退出 Shell\n");
    printf("输入 'help' 显示帮助信息\n");
    printf("使用 ↑/↓ 键浏览历史命令\n\n");

    // 初始化终端设置
    init_terminal();

    while (1) {
        print_prompt();

        // 使用新的读取行函数（带历史记录）
        int result = read_line_with_history(input, sizeof(input));

        if (result < 0) {
            printf("\nRVT CLI退出\n");
            break;
        }

        // 检查退出命令
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0 || strcmp(input, "q") == 0) {
            printf("RVT CLI退出\n");
            break;
        }

        // 检查帮助命令
        if (strcmp(input, "help") == 0) {
            printf("可用命令：\n");
            printf("  n [十进制数字] - 调用函数 run_next_cycle(n)\n");
            printf("  nd [十进制数字] - 调用函数 run_next_cycle(n);dump_waveform();\n");
            printf("  fd [数字]      - 指定 DRAM 的频率\n");
            printf("  fn [数字]      - 指定 NPU 的频率\n");
            printf("  d              - 调用函数 dump_waveform()\n");
            printf("  t              - 切换波形记录状态\n");
            printf("  help           - 显示此帮助信息\n");
            printf("  q, quit, exit  - 退出RVT CLI\n");
            printf("\n使用 ↑/↓ 键浏览历史命令\n");
            continue;
        }

        // 处理命令
        if (input[0] != '\0') {
            process_command(input);
        }
    }

    return 0;
}