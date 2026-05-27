#ifndef _BAR_H_
#define _BAR_H_

/**
 * 多线程版本的进度条函数
 */
void print_progress_with_speed_multithread(uint64_t current, uint64_t total, int style);
/**
 * 等待渲染线程结束（用于主线程在任务完成后等待渲染完成）
 */
void wait_for_render_completion(void);

#endif
