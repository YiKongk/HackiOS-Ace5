#ifndef REDWOOD_GPU_BACKEND_H
#define REDWOOD_GPU_BACKEND_H
/* Hardware operations under RedwoodGPU's existing client lock. Address
 * allocation and buffer ownership stay in the common service. */
struct redwood_gpu_backend {
    uint64_t va_start, va_size;
    bool (*parameter)(uint32_t, uint64_t *);
    bool (*map)(uint64_t, uint64_t, bool);
    void (*mapped)(void);
    void (*unmap)(uint64_t, uint64_t);
    uint32_t (*submit)(const uint64_t *, const uint32_t *, uint32_t);
    bool (*done)(uint32_t);
    uint32_t (*last)(void);
    void (*report)(const char *);
    void (*stop)(void);
    void (*quiesce)(void);
};
extern void redwood_gpu_use_backend(const struct redwood_gpu_backend *);
#endif
