#ifndef __UTILS_H
#define __UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Make sure these constants are defined. */
#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* Useful macros such as MIN and MAX. */
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* (Magic) Macro to test conditions in compile time.
 * Copied from the Linux kernel.
 */
#define BUILD_BUG_ON(condition) \
    ((void)sizeof(char[1 - 2*!!(condition)]))

/* Macros to help branch prediction. */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Other useful macros. */
#define __inline__ __inline__
#define __aligned__(x) __attribute__((aligned (x)))
#define __restrict__ __restrict__

/* Functions */

/* Reports an error to stderr.
 * This function follows the same parameter convetion as printf().
 */
void report_error(const char *fmt, ...)
    __attribute__((format (printf, 1, 2)));

#endif /* __UTILS_H */
