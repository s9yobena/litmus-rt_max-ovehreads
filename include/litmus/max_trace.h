#ifndef _SYS_MAX_TRACE_H_
#define	_SYS_MAX_TRACE_H_

#include <linux/types.h>

#ifdef CONFIG_MAX_SCHED_OVERHEAD_TRACE

struct timestamp;

inline void init_max_sched_overhead_trace(void);
inline int mt_check(struct timestamp* ts, struct timestamp *_start_ts, struct timestamp *_end_ts );

#else /* !CONFIG_MAX_SCHED_OVERHEAD_TRACE */

#endif

#endif /* !_SYS_MAX_TRACE_H_ */
