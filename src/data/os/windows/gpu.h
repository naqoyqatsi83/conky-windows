#ifndef CONKY_GPU_WINDOWS_H
#define CONKY_GPU_WINDOWS_H

struct text_object;

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum GPUs we can track */
#define MAX_GPUS 8

/* GPU info structure populated from gpu.dat (written by lhm-temp.exe) */
struct gpu_info {
  int present;        /* 1 if this slot has valid data */
  int temp_celsius;
  int util_percent;
  unsigned long long mem_used;
  unsigned long long mem_total;
  int fan_rpm;
  char name[256];
};

/* Read GPU data from the shared file.
 * Returns number of GPUs found (0 on failure).
 * gpus array is indexed by GPU ID. */
int read_gpu_info(struct gpu_info *gpus, int max_gpus);

/* Text object callbacks for ${gputemp <N>}, ${gpuutil <N>}, etc.
 * obj->data.i stores the GPU index (default 0). */
void scan_gpu_arg(struct text_object *obj, const char *arg, void *free_at_crash,
                  const char *caller_name);
void print_gpu_temp(struct text_object *obj, char *p, unsigned int p_max_size);
void print_gpu_util(struct text_object *obj, char *p, unsigned int p_max_size);
void print_gpu_name(struct text_object *obj, char *p, unsigned int p_max_size);
void print_gpu_memused(struct text_object *obj, char *p, unsigned int p_max_size);
void print_gpu_memtotal(struct text_object *obj, char *p, unsigned int p_max_size);
void print_gpu_fan(struct text_object *obj, char *p, unsigned int p_max_size);
double gpu_graphval(struct text_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* CONKY_GPU_WINDOWS_H */
