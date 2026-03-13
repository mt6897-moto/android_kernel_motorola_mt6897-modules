#ifndef __TN_COMMON_H__
#define __TN_COMMON_H__
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>


typedef int (*tn_keyboard_uart_dma_call_back_t)(char *buf, int len);
typedef int (*tn_keyboard_uart_dma_write_call_back_t)(int enable);
typedef int (*tn_keyboard_uart_dma_set_call_back_t)(void *uart_8250_port, int type);
typedef void (*tn_keyboard_pad_hall_status_call_back_t)(int status);
extern tn_keyboard_uart_dma_call_back_t uart_dma_call_back;
extern tn_keyboard_uart_dma_set_call_back_t uart_dma_set_call_back;
extern tn_keyboard_uart_dma_write_call_back_t uart_dma_write_call_back;
extern tn_keyboard_pad_hall_status_call_back_t pad_hall_status_call_back;
extern ssize_t tn_tty_write(struct file *file, const char __user *buf,	size_t count, loff_t *ppos);





#endif

