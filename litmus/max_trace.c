#include <litmus/max_trace.h> 
#include <litmus/trace.h>
#include <linux/spinlock.h>

#define MAX_ENTRIES 1

enum overhead_state {
	WAIT_FOR_START,
	WAIT_FOR_MATCH
};

struct max_overhead {
	enum overhead_state state;
  	uint8_t start_id;
	uint8_t end_id;
	struct timestamp start_ts;
	struct timestamp end_ts;
	struct timestamp curr_ts;
	uint64_t value;       
};

static struct max_overhead max_overhead_table[MAX_ENTRIES];
DEFINE_SPINLOCK(mt_table_lock);

static unsigned cur_size = 0;


static inline void add_entry (unsigned start_id, unsigned end_id)
{
	max_overhead_table[cur_size].state = WAIT_FOR_START;
	max_overhead_table[cur_size].start_id = start_id;
	max_overhead_table[cur_size].end_id = end_id;
	max_overhead_table[cur_size].value = 0;
	cur_size += 1;
}

static inline int is_registered(struct timestamp* ts) 
{
	int i;

	for (i = 0; i < cur_size; i++) {

		if ((ts->event == max_overhead_table[i].start_id)
		    ||(ts->event == max_overhead_table[i].end_id)) {
			
			return 1;
		} 
	}	
	return 0;
}

static inline int is_start(struct timestamp *ts)
{
	/* By convention, the start event is always even */
	if (ts->event % 2 == 0)
		return 1;
	return 0;
}

static inline int register_ts(struct timestamp* ts) 
{	
	/* we only regiser start events */
	if (is_start(ts)) {
		add_entry(ts->event, ts->event+1);
		return 0;
	}
	return 1;
}

static inline int get_ts_idx(struct timestamp *ts)
{
	int i;
	
	for (i = 0; i < cur_size; i++) {

		if ((ts->event == max_overhead_table[i].start_id)
		    ||(ts->event == max_overhead_table[i].end_id)) {
			
			return i;
		} 
	}

	/* ts must be registered before calling get_ts_idx() */
	BUG();
	return -1;
}

static inline int update_max_overhead_table(struct timestamp *ts)
{
	int ts_idx;	
	unsigned long curr_max;

	ts_idx = get_ts_idx(ts);

	/* prevent re-ordering of ts_idx = ... */
	barrier();
	
	// Check if we are in WAIT_FOR_START_EVENT state; store the timestamp ts
	if ( (max_overhead_table[ts_idx].state == WAIT_FOR_START) && 
	     (ts->event == max_overhead_table[ts_idx].start_id)) {

		max_overhead_table[ts_idx].state = WAIT_FOR_MATCH;
		max_overhead_table[ts_idx].start_ts = *ts;
		max_overhead_table[ts_idx].curr_ts = *ts;
		return -1;
	}
        // Here currentTimestamp contains a start event and ts 
	// is begin event; Make sure ts was generated 
	// after currentTimestamp and set currentTimestamp to ts
	else if ((max_overhead_table[ts_idx].curr_ts.event == max_overhead_table[ts_idx].start_id)
		 &&( ts->event == max_overhead_table[ts_idx].start_id)
		 &&(max_overhead_table[ts_idx].curr_ts.seq_no < ts->seq_no)) {

		max_overhead_table[ts_idx].state = WAIT_FOR_MATCH;
		max_overhead_table[ts_idx].start_ts = *ts;
		max_overhead_table[ts_idx].curr_ts = *ts;
		return -1;
	}

	// Here currentTimestamp contains a start event and 
	// ts is an end event. Make sure ts was generated afer 
	// currentTimestamp and its task_type ==TSK_RT. Then 
	// generate a new overhead value
	else if ((max_overhead_table[ts_idx].curr_ts.event == max_overhead_table[ts_idx].start_id)
		 &&(ts->event == max_overhead_table[ts_idx].end_id)
		 &&(ts->task_type == TSK_RT)
		 &&(max_overhead_table[ts_idx].curr_ts.seq_no < ts->seq_no)) {
			
		max_overhead_table[ts_idx].end_ts = *ts;
		max_overhead_table[ts_idx].state = WAIT_FOR_START;
		barrier();
		/* prevent re-ordering of curr_max = ... */
		curr_max = max_overhead_table[ts_idx].end_ts.timestamp 
			- max_overhead_table[ts_idx].start_ts.timestamp;
		barrier();
		/* prevent re-ordering of if( curr_max > ... */
		if ( curr_max > max_overhead_table[ts_idx].value) {

			max_overhead_table[ts_idx].value = curr_max;
			return ts_idx;
		}
		return -1;
	}
	return -1;
}

/* 
 * Check if ts yields a maximum overhead
 */
inline int mt_check(struct timestamp* ts)
{
  	unsigned long lock_flags;
	spin_lock_irqsave(&mt_table_lock, lock_flags);
	if (is_registered(ts)) {
		spin_unlock_irqrestore(&mt_table_lock, lock_flags);
		return update_max_overhead_table(ts);
	} else {
		register_ts(ts);
		spin_unlock_irqrestore(&mt_table_lock, lock_flags);
		return update_max_overhead_table(ts);
	}	
}

/* 
 * Get start timestamp
 */
inline struct timestamp* mt_get_start_ts (int idx) {

	unsigned long lock_flags;
	
	static struct timestamp r_ts;

	spin_lock_irqsave(&mt_table_lock, lock_flags);
	r_ts = max_overhead_table[idx].start_ts;
	spin_unlock_irqrestore(&mt_table_lock, lock_flags);

	return &r_ts;
}

/* 
 * Get end timestamp
 */
inline struct timestamp* mt_get_end_ts (int idx) {

	unsigned long lock_flags;

	static struct timestamp r_ts;

	spin_lock_irqsave(&mt_table_lock, lock_flags);
	r_ts = max_overhead_table[idx].end_ts;
	spin_unlock_irqrestore(&mt_table_lock, lock_flags);

	return &r_ts;	
}


inline uint64_t mt_get_overhead(int i) {
	
	unsigned long lock_flags;
	
	uint64_t r_value;
	
	spin_lock_irqsave(&mt_table_lock, lock_flags);
	r_value = max_overhead_table[i].value;
	spin_unlock_irqrestore(&mt_table_lock, lock_flags);
	
	return  r_value;
}


