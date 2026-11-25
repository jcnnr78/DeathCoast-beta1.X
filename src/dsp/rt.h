/* The loader's worker thread, started from create_instance -- which MPC calls from its audio callback, so a
 * thread made the plain way would inherit the callback's real-time priority (POSIX PTHREAD_INHERIT_SCHED) and starve
 * the audio while it decodes a wavetable. The policy is set on the attributes instead: the worker is a normal
 * thread from birth, and named so a thread list tells it from the audio thread.
 */
#pragma once

#include <pthread.h>
#include <sched.h>
#include <cstring>

namespace nw {

/* Start a worker that is SCHED_OTHER from birth, off the SPI core, and
 * carries its own name. Returns 0 on success, like pthread_create.
 *
 * `name` is truncated to the 15 characters Linux allows a thread name. */
inline int rtStartWorker(pthread_t *tid, void *(*fn)(void *), void *arg,
                         const char *name)
{
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
        return -1;

    /* The whole point: do NOT inherit the audio thread's scheduling. */
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    struct sched_param sp = { };
    sp.sched_priority = 0;                 /* SCHED_OTHER takes only 0 */
    pthread_attr_setschedparam(&attr, &sp);

    /* Leave core 3 to the SPI callback. */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    pthread_attr_setaffinity_np(&attr, sizeof set, &set);

    int rc = pthread_create(tid, &attr, fn, arg);
    pthread_attr_destroy(&attr);

    if (rc == 0 && name && *name) {
        char nm[16];
        snprintf(nm, sizeof nm, "%s", name);
        pthread_setname_np(*tid, nm);      /* else it inherits "Audio Main" */
    }
    return rc;
}

} // namespace nw
