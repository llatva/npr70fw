/**
  ******************************************************************************
  * @file    syscalls.c
  * @brief   System calls for newlib (printf retargeting to UART2)
  ******************************************************************************
  */

#include "main.h"
#include <errno.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/unistd.h>

/* External UART handle from main.c */
extern UART_HandleTypeDef huart2;

/**
 * @brief Redirect printf to UART2
 */
int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO) {
        HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
        return len;
    }
    errno = EBADF;
    return -1;
}

/**
 * @brief Dummy implementations for newlib
 */
int _close(int file)
{
    return -1;
}

int _fstat(int file, struct stat *st)
{
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    return 0;
}

int _read(int file, char *ptr, int len)
{
    return 0;
}
