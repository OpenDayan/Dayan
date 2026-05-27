#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>

// 颜色定义
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[0;33m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_CYAN    "\033[0;36m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_MAGENTA "\033[0;35m"
#define COLOR_BOLD_WHITE "\033[1;37m"

// 渲染状态结构体
typedef struct {
    long long start_time_ms;
    long long last_time_ms;
    uint64_t last_count;
    double smoothed_speed;
    int update_count;
    int first_call;
} render_state_t;

// 共享数据结构
typedef struct {
    uint64_t current;
    uint64_t total;
    int style;
    int active;
    int completed;
    int completion_printed;
    render_state_t render_state;  // 将渲染状态移到共享结构中
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} progress_data_t;

// 全局共享变量
static progress_data_t shared_progress = {0};
static pthread_t render_thread = 0;
static int thread_created = 0;

/**
 * 获取终端宽度
 */
static inline int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80; // 默认宽度
}

/**
 * 获取当前时间的毫秒表示
 */
static inline long long get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/**
 * 格式化速度显示，自动选择单位
 */
static inline void format_speed(double speed, char *buffer, int buffer_size) {
    if (speed >= 1000000) {
        snprintf(buffer, buffer_size, "%.1fMHz", speed / 1000000.0);
    } else if (speed >= 10000) {
        snprintf(buffer, buffer_size, "%.1fkHz", speed / 1000.0);
    } else if (speed >= 1000) {
        snprintf(buffer, buffer_size, "%.2fkHz", speed / 1000.0);
    } else if (speed >= 100) {
        snprintf(buffer, buffer_size, "%.1fHz", speed);
    } else if (speed >= 10) {
        snprintf(buffer, buffer_size, "%.2fHz", speed);
    } else if (speed >= 1) {
        snprintf(buffer, buffer_size, "%.3fHz", speed);
    } else if (speed > 0.001) {
        snprintf(buffer, buffer_size, "%.4fHz", speed);
    } else {
        snprintf(buffer, buffer_size, "0Hz");
    }
}

/**
 * 计算自适应进度条宽度
 */
static inline int calculate_adaptive_width(int min_width, int max_width, int info_length) {
    int terminal_width = get_terminal_width();
    int available_width = terminal_width - info_length - 3; // 减去边框、边距等
    
    if (available_width < min_width) {
        return min_width;
    } else if (available_width > max_width) {
        return max_width;
    } else {
        return available_width;
    }
}

/**
 * 渲染进度条（在子线程中调用）
 */
static inline void render_progress_bar() {
    // 锁定互斥锁以安全读取共享数据
    pthread_mutex_lock(&shared_progress.mutex);
    
    uint64_t current = shared_progress.current;
    uint64_t total = shared_progress.total;
    int style = shared_progress.style;
    int completed = shared_progress.completed;
    int completion_printed = shared_progress.completion_printed;
    render_state_t *state = &shared_progress.render_state;
    
    pthread_mutex_unlock(&shared_progress.mutex);
    
    if (total == 0) return;
    
    if (state->first_call) {
        state->start_time_ms = get_time_ms();
        state->last_time_ms = state->start_time_ms;
        state->last_count = current;
        state->smoothed_speed = 0.0;
        state->update_count = 0;
        state->first_call = 0;
    }
    
    // 计算速度
    long long current_time_ms = get_time_ms();
    long long time_diff_ms = current_time_ms - state->last_time_ms;
    uint64_t count_diff = current - state->last_count;
    
    double current_speed = 0.0;
    
    if (time_diff_ms > 10 && count_diff >= 0) {  // 添加 count_diff >= 0 检查
        current_speed = (double)count_diff / (time_diff_ms / 1000.0);
        
        if (state->smoothed_speed == 0.0) {
            state->smoothed_speed = current_speed;
        } else {
            double alpha = (state->update_count < 5) ? 0.8 : 0.5;
            state->smoothed_speed = alpha * current_speed + (1 - alpha) * state->smoothed_speed;
        }
        
        state->update_count++;
        state->last_time_ms = current_time_ms;
        state->last_count = current;
    }
    
    // 计算自适应进度条宽度
    int info_length = 50;
    int progress_width = calculate_adaptive_width(20, 80, info_length);
    
    double percentage = (double)current / total;
    int progress_pos = (int)(percentage * progress_width);
    if (progress_pos > progress_width) progress_pos = progress_width;
    if (progress_pos < 0) progress_pos = 0;
    
    // 选择样式
    const char *filled, *empty;
    switch (style) {
        case 0: filled = "█"; empty = "░"; break;
        case 1: filled = "●"; empty = "○"; break;
        case 2: filled = "~"; empty = " "; break;
        default: filled = "█"; empty = "░";
    }
    
    // 选择颜色
    const char *color;
    if (percentage < 0.3) color = COLOR_RED;
    else if (percentage < 0.7) color = COLOR_YELLOW;
    else color = COLOR_GREEN;
    
    // 打印进度条
    printf("\r%s[%s", COLOR_BOLD_WHITE, color);
    for (int i = 0; i < progress_pos; i++) printf("%s", filled);
    for (int i = progress_pos; i < progress_width; i++) printf("%s", empty);
    
    // 打印信息
    printf("%s] %s%.1f%% (%lu/%lu) ", 
           COLOR_BOLD_WHITE, COLOR_CYAN, percentage * 100, current, total);
    
    // 打印速度信息
    char speed_str[32];
    format_speed(state->smoothed_speed, speed_str, sizeof(speed_str));
    
    const char *speed_color;
    if (state->smoothed_speed < 10) speed_color = COLOR_RED;
    else if (state->smoothed_speed < 100) speed_color = COLOR_YELLOW;
    else if (state->smoothed_speed < 1000) speed_color = COLOR_GREEN;
    else if (state->smoothed_speed < 10000) speed_color = COLOR_BLUE;
    else speed_color = COLOR_MAGENTA;
    
    printf("%s[%s]%s ", speed_color, speed_str, COLOR_RESET);
    
    // 打印时间信息
    long long elapsed_ms = current_time_ms - state->start_time_ms;
    int elapsed_sec = elapsed_ms / 1000;
    int remaining_sec = (percentage > 0.01) ? 
        ((elapsed_ms / percentage - elapsed_ms) / 1000) : 0;
        
    printf("%s[%02d:%02d<%02d:%02d]%s", 
           COLOR_BLUE, 
           elapsed_sec / 60, elapsed_sec % 60,
           remaining_sec / 60, remaining_sec % 60, 
           COLOR_RESET);
    
    // 清除行尾
    int current_length = progress_width + 80;
    int terminal_width = get_terminal_width();
    for (int i = current_length; i < terminal_width; i++) printf(" ");
    
    fflush(stdout);
    
    // 完成时打印统计信息（确保只打印一次）
    if (completed && !completion_printed) {
        // 标记完成信息已打印
        pthread_mutex_lock(&shared_progress.mutex);
        shared_progress.completion_printed = 1;
        pthread_mutex_unlock(&shared_progress.mutex);
        
        long long total_time_ms = current_time_ms - state->start_time_ms;
        double avg_speed = (total_time_ms > 0) ? 
            (double)total / (total_time_ms / 1000.0) : 0.0;
        
        char avg_speed_str[32];
        format_speed(avg_speed, avg_speed_str, sizeof(avg_speed_str));
        
        printf("\n%s✓ 完成！总时间: %.1f秒, 平均速度: %s%s%s\n", 
               COLOR_GREEN, total_time_ms / 1000.0, COLOR_CYAN, avg_speed_str, COLOR_RESET);
    }
}

/**
 * 渲染线程函数
 */
static inline void* render_thread_func(void* arg) {
    int last_completed = 0;
    
    while (1) {
        // 锁定检查状态
        pthread_mutex_lock(&shared_progress.mutex);
        int active = shared_progress.active;
        int completed = shared_progress.completed;
        pthread_mutex_unlock(&shared_progress.mutex);
        
        if (!active) {
            break; // 线程退出条件
        }
        
        // 执行渲染
        render_progress_bar();
        
        // 检查是否完成，如果刚刚完成，则退出循环
        pthread_mutex_lock(&shared_progress.mutex);
        completed = shared_progress.completed;
        int completion_printed = shared_progress.completion_printed;
        pthread_mutex_unlock(&shared_progress.mutex);
        
        // 如果任务完成且完成信息已打印，则退出线程
        if (completed && completion_printed) {
            break;
        }
        
        // 100ms 刷新间隔
        usleep(100000);
    }
    
    // 清理线程状态
    pthread_mutex_lock(&shared_progress.mutex);
    shared_progress.active = 0;
    thread_created = 0;
    pthread_mutex_unlock(&shared_progress.mutex);
    
    return NULL;
}

/**
 * 启动渲染线程
 */
static inline void start_render_thread() {
    if (pthread_create(&render_thread, NULL, render_thread_func, NULL) != 0) {
        fprintf(stderr, "错误: 无法创建渲染线程\n");
        return;
    }
    thread_created = 1;
}

/**
 * 重置进度条状态（用于开始新的进度条）
 */
static inline void reset_progress_state() {
    pthread_mutex_lock(&shared_progress.mutex);
    
    // 重置所有状态，包括渲染状态
    shared_progress.current = 0;
    shared_progress.total = 0;
    shared_progress.completed = 0;
    shared_progress.completion_printed = 0;
    shared_progress.active = 0;
    
    // 重置渲染状态
    shared_progress.render_state.start_time_ms = 0;
    shared_progress.render_state.last_time_ms = 0;
    shared_progress.render_state.last_count = 0;
    shared_progress.render_state.smoothed_speed = 0.0;
    shared_progress.render_state.update_count = 0;
    shared_progress.render_state.first_call = 1;
    
    pthread_mutex_unlock(&shared_progress.mutex);
    
    // 等待现有线程结束（如果有）
    if (thread_created && render_thread) {
        pthread_join(render_thread, NULL);
        render_thread = 0;
        thread_created = 0;
    }
}

/**
 * 多线程版本的进度条函数
 */
void print_progress_with_speed_multithread(uint64_t current, uint64_t total, int style) {
    static int first_call = 1;
    
    // 第一次调用时初始化共享数据
    if (first_call) {
        memset(&shared_progress, 0, sizeof(shared_progress));
        pthread_mutex_init(&shared_progress.mutex, NULL);
        pthread_cond_init(&shared_progress.cond, NULL);
        
        // 初始化渲染状态
        shared_progress.render_state.first_call = 1;
        
        first_call = 0;
    }
    
    // 锁定并更新共享数据
    pthread_mutex_lock(&shared_progress.mutex);
    
    shared_progress.current = current;
    shared_progress.total = total;
    shared_progress.style = style;
    
    // 检查是否完成
    if (current >= total && total > 0) {
        shared_progress.completed = 1;
    }
    
    // 如果是第一次调用或者线程不存在，启动渲染线程
    if (!thread_created) {
        shared_progress.active = 1;
        shared_progress.completed = 0;
        shared_progress.completion_printed = 0;
        
        // 重置渲染状态
        shared_progress.render_state.first_call = 1;
        
        pthread_mutex_unlock(&shared_progress.mutex);
        start_render_thread();
    } else {
        pthread_mutex_unlock(&shared_progress.mutex);
    }
}

/**
 * 等待渲染线程结束（用于主线程在任务完成后等待渲染完成）
 */
void wait_for_render_completion(void) {
    if (thread_created && render_thread) {
        pthread_join(render_thread, NULL);
        render_thread = 0;
        thread_created = 0;
    }
    reset_progress_state();
}
