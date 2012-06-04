#ifndef _SYS_MAX_TRACE_H_
#define	_SYS_MAX_TRACE_H_

#include <linux/types.h>

#ifdef CONFIG_MAX_SCHED_OVERHEAD_TRACE

struct timestamp;

inline int mt_check(struct timestamp*);
inline struct timestamp* mt_get_start_ts (int);
inline struct timestamp* mt_get_end_ts (int idx);
inline uint64_t mt_get_overhead(int);

#else /* !CONFIG_MAX_SCHED_OVERHEAD_TRACE */

#endif

#endif /* !_SYS_MAX_TRACE_H_ */
