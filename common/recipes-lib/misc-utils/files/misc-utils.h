/*
 * Copyright 2018-present Facebook. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _OBMC_MISC_UTILS_H_
#define _OBMC_MISC_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
#endif

/* Retry till condition is true.
   Helper macro to consolidate retry logic. Usage example:
  if (retry_cond((rc = stuff(1, 4)) == 0, 4, 1000)) {
    printf("stuff failed with %d even after 4 attempts\n", rc);
  } else {
    printf("Stuff succeeded!\n");
  }
*/
#define retry_cond(oper, _num_retries, _msec) ({                          \
  int retries;                                                            \
  int num_retries = (int)(_num_retries);                                  \
  int msec = (int)(_msec);                                                \
  struct timespec ts;                                                     \
  ts.tv_sec = msec / 1000; ts.tv_nsec = (msec % 1000) * 1000000;          \
  for (retries = 0; retries < num_retries && !(oper); retries++) {        \
    int res;                                                              \
    do {                                                                  \
      res = nanosleep(&ts, &ts);                                          \
    } while (res && errno == EINTR);                                      \
  }                                                                       \
  retries == num_retries ? (!(oper) ? -1 : 0) : 0;                        \
})

typedef enum {
	CPU_MODEL_INVALID = -1,
	CPU_MODEL_ARM_V5,
	CPU_MODEL_ARM_V6,
	CPU_MODEL_ARM_V7,
	CPU_MODEL_ARM_V8,
	CPU_MODEL_MAX,
} cpu_model_t;

typedef enum {
	SOC_MODEL_INVALID = -1,
	SOC_MODEL_ASPEED_G4,
	SOC_MODEL_ASPEED_G5,
	SOC_MODEL_ASPEED_G6,
	SOC_MODEL_ASPEED_G7,
	SOC_MODEL_MAX,
} soc_model_t;

typedef uint32_t k_version_t;

/*
 * String utility functions.
 */
char* str_lstrip(char *input);
char* str_rstrip(char *input);
char* str_strip(char *input);
bool str_startswith(const char *str, const char *pattern);
bool str_endswith(const char *str, const char *pattern);

/*
 * Device IO utility functions.
 */
int device_read(const char *device, int *value);
int device_write_buff(const char *device, const char *value);

/*
 * File IO utility functions.
 */
ssize_t file_read_bytes(int fd, void *buf, size_t count);
ssize_t file_write_bytes(int fd, const void *buf, size_t count);

/*
 * pathname utility functions.
 */
int path_split(char *path, char **entries, int *size);
char* path_join(char *buf, size_t size, ...);
bool path_exists(const char *path);
bool path_isfile(const char *path);
bool path_isdir(const char *path);
bool path_islink(const char *path);

/*
 * Platform utility functions.
 */
cpu_model_t get_cpu_model(void);
soc_model_t get_soc_model(void);
k_version_t get_kernel_version(void);
int single_instance_lock(const char *prog_name);
int single_instance_lock_blocked(const char *prog_name);
int single_instance_unlock(int lock_fd);

/*
 * CRC utility functions.
 */
uint16_t crc16_ibm(const uint8_t *buf, size_t count);
uint8_t lrc8(const uint8_t *buf, size_t count);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* _OBMC_MISC_UTILS_H_ */
