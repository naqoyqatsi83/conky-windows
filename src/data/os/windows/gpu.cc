/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2005-2024 Brenden Matthews, Philip Kovacs, et. al.
 *  (see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* Read GPU sensor data from the LibreHardwareMonitor helper's shared file
 * (gpu.dat). This provides cross-vendor GPU stats (NVIDIA, AMD, Intel)
 * without requiring vendor-specific libraries. */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gpu.h"

/* Need full text_object definition for obj->data.i */
#include "../../../content/text_object.h"
/* For human_readable() */
#include "../../../conky.h"
/* For trigger_lhm_helper() */
#include "../windows.h"

/* Parse GPU index from text object argument.
 * Defaults to 0 if arg is NULL or empty. */
void scan_gpu_arg(struct text_object *obj, const char *arg, void *free_at_crash,
                  const char *caller_name) {
  (void)free_at_crash;
  (void)caller_name;
  if (arg != nullptr && arg[0] != '\0') {
    obj->data.i = atoi(arg);
  } else {
    obj->data.i = 0;
  }
}

/* Read the gpu.dat file written by lhm-temp.exe.
 * Format: id|temp_c|util_pct|mem_used_mib|mem_total_mib|fan_rpm|name
 * Values from lhm-temp are in MiB — multiply by 1048576 before human_readable().
 *
 * If the file is missing or stale (>15 s without update), triggers the
 * lhm-temp helper to restart it — GPU data depends on this process. */
int read_gpu_info(struct gpu_info *gpus, int max_gpus) {
  memset(gpus, 0, (size_t)max_gpus * sizeof(struct gpu_info));

  const char *env = getenv("ALLUSERSPROFILE");
  if (env == nullptr) return 0;

  std::string path = std::string(env) + "\\Conky\\gpu.dat";

  /* Check for staleness using file modification time */
  {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      trigger_lhm_helper();
      return 0;
    }
    FILETIME ftWrite = {}, ftNow = {};
    GetFileTime(h, nullptr, nullptr, &ftWrite);
    GetSystemTimeAsFileTime(&ftNow);
    CloseHandle(h);
    ULARGE_INTEGER uW = {{ftWrite.dwLowDateTime, ftWrite.dwHighDateTime}};
    ULARGE_INTEGER uN = {{ftNow.dwLowDateTime, ftNow.dwHighDateTime}};
    /* 15 seconds in FILETIME units (100-ns intervals) */
    if (uN.QuadPart - uW.QuadPart > 150000000ULL) {
      trigger_lhm_helper();
    }
  }

  FILE *f = fopen(path.c_str(), "r");
  if (f == nullptr) {
    trigger_lhm_helper();
    return 0;
  }

  int count = 0;
  char line[512];
  while (count < max_gpus && fgets(line, sizeof(line), f)) {
    int id;
    int temp = 0, util = 0, fan = 0;
    unsigned long long mem_used = 0, mem_total = 0;
    char name[256] = "";

    int parsed = sscanf(line, "%d|%d|%d|%llu|%llu|%d|%255[^\n]",
                        &id, &temp, &util, &mem_used, &mem_total, &fan, name);
    if (parsed >= 1 && id >= 0 && id < max_gpus) {
      gpus[id].present = 1;
      gpus[id].temp_celsius = temp;
      gpus[id].util_percent = util;
      gpus[id].mem_used = mem_used;
      gpus[id].mem_total = mem_total;
      gpus[id].fan_rpm = fan;
      size_t nlen = strlen(name);
      while (nlen > 0 && (name[nlen-1] == '\r' || name[nlen-1] == '\n')) {
        name[--nlen] = '\0';
      }
      strncpy(gpus[id].name, name, sizeof(gpus[id].name) - 1);
      gpus[id].name[sizeof(gpus[id].name) - 1] = '\0';
      if (id + 1 > count) count = id + 1;
    }
  }
  fclose(f);
  return count;
}

/* Helper: fill a gpu_info for the given object's GPU index.
 * Returns 1 if data was found, 0 otherwise. */
static int get_gpu_for_obj(struct text_object *obj, struct gpu_info *gpu) {
  struct gpu_info gpus[MAX_GPUS];
  int count = read_gpu_info(gpus, MAX_GPUS);
  int idx = obj->data.i;
  if (idx >= 0 && idx < count && gpus[idx].present) {
    *gpu = gpus[idx];
    return 1;
  }
  return 0;
}

void print_gpu_temp(struct text_object *obj, char *p, unsigned int p_max_size) {
  struct gpu_info gpu;
  if (get_gpu_for_obj(obj, &gpu)) {
    snprintf(p, p_max_size, "%d", gpu.temp_celsius);
  } else {
    snprintf(p, p_max_size, "N/A");
  }
}

void print_gpu_util(struct text_object *obj, char *p, unsigned int p_max_size) {
  struct gpu_info gpu;
  if (get_gpu_for_obj(obj, &gpu)) {
    snprintf(p, p_max_size, "%d", gpu.util_percent);
  } else {
    snprintf(p, p_max_size, "N/A");
  }
}

void print_gpu_name(struct text_object *obj, char *p, unsigned int p_max_size) {
  struct gpu_info gpu;
  if (get_gpu_for_obj(obj, &gpu)) {
    snprintf(p, p_max_size, "%s", gpu.name);
  } else {
    snprintf(p, p_max_size, "N/A");
  }
}

void print_gpu_memused(struct text_object *obj, char *p, unsigned int p_max_size) {
  struct gpu_info gpu;
  if (get_gpu_for_obj(obj, &gpu) && gpu.mem_used > 0) {
    // lhm-temp stores values in MiB; convert to bytes for human_readable
    human_readable((long long)gpu.mem_used * 1048576LL, p, (int)p_max_size);
  } else {
    snprintf(p, p_max_size, "N/A");
  }
}

void print_gpu_memtotal(struct text_object *obj, char *p, unsigned int p_max_size) {
  struct gpu_info gpu;
  if (get_gpu_for_obj(obj, &gpu) && gpu.mem_total > 0) {
    // lhm-temp stores values in MiB; convert to bytes for human_readable
    human_readable((long long)gpu.mem_total * 1048576LL, p, (int)p_max_size);
  } else {
    snprintf(p, p_max_size, "N/A");
  }
}

void print_gpu_fan(struct text_object *obj, char *p, unsigned int p_max_size) {
  struct gpu_info gpu;
  if (get_gpu_for_obj(obj, &gpu)) {
    snprintf(p, p_max_size, "%d", gpu.fan_rpm);
  } else {
    snprintf(p, p_max_size, "N/A");
  }
}

double gpu_graphval(struct text_object *obj) {
  struct gpu_info gpu;
  if (get_gpu_for_obj(obj, &gpu)) {
    return (double)gpu.util_percent;
  }
  return 0.0;
}
