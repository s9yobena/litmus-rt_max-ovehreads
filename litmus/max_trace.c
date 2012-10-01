#include <litmus/max_trace.h> 
#include <litmus/trace.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <litmus/debug_trace.h>

#define MAX_ENTRIES 10

enum overhead_state {
	WAIT_FOR_START,
	WAIT_FOR_MATCH
};

struct max_overhead {
	enum overhead_state state;
  	uint8_t start_id;
	uint8_t end_id;
	uint8_t cpu_id;
	struct timestamp start_ts;
	struct timestamp end_ts;
	struct timestamp curr_ts;
	uint64_t value;       
	spinlock_t spinlock;
};

static DEFINE_PER_CPU(struct max_overhead[MAX_ENTRIES], _max_overhead_table);
#define max_overhead_table(idx) (__get_cpu_var(_max_overhead_table[(idx)]))
#define max_overhead_table_for(idx,cpu_id) (per_cpu(_max_overhead_table[(idx)], cpu_id))

static DEFINE_PER_CPU(unsigned, _curr_size);
#define curr_size (__get_cpu_var(_curr_size))
#define curr_size_for(cpu_id) (per_cpu(_curr_size, cpu_id))

inline void init_max_sched_overhead_trace(void) {
	int cpu;
	int i;
	static int start_ids[] = {100, 102, 104, 106, 110, 190};
	for_each_online_cpu(cpu) {

		curr_size_for(cpu) = 0;
		for (i = 0; i < sizeof(start_ids) / sizeof(start_ids[0]); i++) {

			max_overhead_table_for(curr_size_for(cpu), cpu).state = WAIT_FOR_START;
			max_overhead_table_for(curr_size_for(cpu), cpu).start_id = start_ids[i];
			max_overhead_table_for(curr_size_for(cpu), cpu).end_id = start_ids[i]+1;
			max_overhead_table_for(curr_size_for(cpu), cpu).cpu_id = cpu;
			max_overhead_table_for(curr_size_for(cpu), cpu).value = 0;
			spin_lock_init(&max_overhead_table_for(curr_size_for(cpu), cpu).spinlock);
			
			curr_size_for(cpu) += 1;

			TRACE(KERN_INFO "add_entry with start_id %d, end_id %d, cpu_id %d, executing on cpu %d \n",
			       start_ids[i],
			       start_ids[i]+1,
			       cpu,
			       smp_processor_id());

		}
	}
}

static inline void print_all_entries(void) {

	int cpu;
	int i;
	for_each_online_cpu(cpu) {
		
		TRACE(KERN_INFO"Printing all table entries for cpu %d.\n",
		       cpu);
		for (i=0; i<curr_size_for(cpu); i++) {
			TRACE(KERN_INFO"cpu_id %d; start_id %d; end_id %d; value %lu;\n",
			       max_overhead_table_for(i,cpu).cpu_id,
			       max_overhead_table_for(i,cpu).start_id,
			       max_overhead_table_for(i,cpu).end_id,
			       max_overhead_table_for(i,cpu).value);
		}
	}

}

static inline int get_ts_idx(struct timestamp *ts, int cpu_id)
{
	int i;

	for (i = 0; i < curr_size_for(cpu_id); i++) {

		if (((ts->event == max_overhead_table_for(i, cpu_id).start_id)
		     ||(ts->event == max_overhead_table_for(i, cpu_id).end_id))
		    &&(ts->cpu == max_overhead_table_for(i, cpu_id).cpu_id)) {

			return i;
		} 
	}


	/* ts must be registered before calling get_ts_idx() */
	TRACE(KERN_ERR"Timestamp %d is not registered \n",ts->event);
	BUG();
	return -1;
}

static inline int update_max_overhead_table(struct timestamp *ts, int cpu_id)
{
	int ts_idx;	
	unsigned long curr_max;
	unsigned long lock_flags;

	
	ts_idx = get_ts_idx(ts, cpu_id);

	spin_lock_irqsave(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags);

	/* prevent re-ordering of ts_idx = ... */
	barrier();
	
	// Check if we are in WAIT_FOR_START_EVENT state; store the timestamp ts
	if ( (max_overhead_table_for(ts_idx, cpu_id).state == WAIT_FOR_START)
	     &&(ts->event == max_overhead_table_for(ts_idx, cpu_id).start_id)
	     &&(ts->cpu == max_overhead_table_for(ts_idx, cpu_id).cpu_id)) {

		max_overhead_table_for(ts_idx, cpu_id).state = WAIT_FOR_MATCH;
		max_overhead_table_for(ts_idx, cpu_id).start_ts = *ts;
		max_overhead_table_for(ts_idx, cpu_id).curr_ts = *ts;
		
		spin_unlock_irqrestore(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags);
		return -1;
	}
        // Here currentTimestamp contains a start event and ts 
	// is begin event; Make sure ts was generated 
	// after currentTimestamp and set currentTimestamp to ts
	else if ((max_overhead_table_for(ts_idx, cpu_id).curr_ts.event == max_overhead_table_for(ts_idx, cpu_id).start_id)
		 &&( ts->event == max_overhead_table_for(ts_idx, cpu_id).start_id)
		 &&(ts->cpu == max_overhead_table_for(ts_idx, cpu_id).cpu_id)
		 &&(max_overhead_table_for(ts_idx, cpu_id).curr_ts.seq_no < ts->seq_no)) {

		max_overhead_table_for(ts_idx, cpu_id).state = WAIT_FOR_MATCH;
		max_overhead_table_for(ts_idx, cpu_id).start_ts = *ts;
		max_overhead_table_for(ts_idx, cpu_id).curr_ts = *ts;

		spin_unlock_irqrestore(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags);
		return -1;
	}

	// Here currentTimestamp contains a start event and 
	// ts is an end event. Make sure ts was generated afer 
	// currentTimestamp and its task_type ==TSK_RT. Then 
	// generate a new overhead value
	else if (((max_overhead_table_for(ts_idx, cpu_id).curr_ts.event == max_overhead_table_for(ts_idx, cpu_id).start_id)
		  &&(ts->event == max_overhead_table_for(ts_idx, cpu_id).end_id)
		  &&(ts->cpu == max_overhead_table_for(ts_idx, cpu_id).cpu_id)
		  &&(ts->task_type == TSK_RT)
		  &&(max_overhead_table_for(ts_idx, cpu_id).curr_ts.seq_no < ts->seq_no))
		 ||
		 ((max_overhead_table_for(ts_idx, cpu_id).curr_ts.event == max_overhead_table_for(ts_idx, cpu_id).start_id)
		  &&(ts->event == max_overhead_table_for(ts_idx, cpu_id).end_id)
		  &&(ts->cpu == max_overhead_table_for(ts_idx, cpu_id).cpu_id)
		  &&(max_overhead_table_for(ts_idx, cpu_id).curr_ts.seq_no < ts->seq_no)
		  &&(ts->event == TS_SEND_RESCHED_START_EVENT
		     || ts->event == TS_SEND_RESCHED_END_EVENT
		     || ts->event == TS_TICK_START_EVENT
		     || ts->event == TS_TICK_END_EVENT)
		  )) {
			
		max_overhead_table_for(ts_idx, cpu_id).end_ts = *ts;
		max_overhead_table_for(ts_idx, cpu_id).state = WAIT_FOR_START;
		barrier();
		/* prevent re-ordering of curr_max = ... */
		curr_max = max_overhead_table_for(ts_idx, cpu_id).end_ts.timestamp 
			- max_overhead_table_for(ts_idx, cpu_id).start_ts.timestamp;
		barrier();
		/* prevent re-ordering of if( curr_max > ... */
		if ( curr_max > max_overhead_table_for(ts_idx, cpu_id).value) {

			max_overhead_table_for(ts_idx, cpu_id).value = curr_max;
			spin_unlock_irqrestore(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags);
			return ts_idx;
		}
		spin_unlock_irqrestore(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags);
		return -1;
	}
	spin_unlock_irqrestore(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags);
	return -1;
}

/* 
 * Check if ts yields a maximum overhead
 */
inline int mt_check(struct timestamp* ts, struct timestamp *_start_ts, struct timestamp *_end_ts )
{

	int update_r;

	update_r = update_max_overhead_table(ts, ts->cpu);

	if (update_r > -1) {
		*_start_ts = max_overhead_table_for(update_r, ts->cpu).start_ts;
		*_end_ts = max_overhead_table_for(update_r, ts->cpu).end_ts;
	}

	return update_r;
}

