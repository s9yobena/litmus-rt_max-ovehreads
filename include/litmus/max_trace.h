#ifndef _SYS_MAX_TRACE_H_
#define	_SYS_MAX_TRACE_H_

#include <linux/types.h>
#include <linux/spinlock.h>

#ifdef CONFIG_MAX_SCHED_OVERHEAD_TRACE

struct max_overheads_t {
	uint64_t cxs;
	uint64_t sched;
	uint64_t sched2;
	uint64_t release;
	uint64_t send_resched;
  	uint64_t release_latency;
	uint64_t tick;
};

extern struct max_overheads_t max_overheads;
extern spinlock_t max_overheads_spinlock;

struct timestamp;

inline void init_max_sched_overhead_trace(void);
inline int mt_check(struct timestamp* ts, struct timestamp *_start_ts, struct timestamp *_end_ts );
inline int  mt_latency_check(struct timestamp *mt_ts);

#else /* !CONFIG_MAX_SCHED_OVERHEAD_TRACE */

#endif

#endif /* !_SYS_MAX_TRACE_H_ */
