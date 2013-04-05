#include <litmus/max_trace.h> 
#include <litmus/trace.h>

#include <linux/percpu.h>
#include <litmus/debug_trace.h>

#include <asm/atomic.h>

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
};

struct max_latency {
	uint64_t value;       
};

struct max_overheads_t max_overheads;
spinlock_t max_overheads_spinlock;

static DEFINE_PER_CPU(struct max_overhead[MAX_ENTRIES], _max_overhead_table);
#define max_overhead_table(idx) (__get_cpu_var(_max_overhead_table[(idx)]))
#define max_overhead_table_for(idx,cpu_id) (per_cpu(_max_overhead_table[(idx)], cpu_id))

static DEFINE_PER_CPU(struct max_latency, _max_latency);
#define max_latency_for(cpu_id) (per_cpu(_max_latency, cpu_id))

static DEFINE_PER_CPU(unsigned, _curr_size);
#define curr_size (__get_cpu_var(_curr_size))
#define curr_size_for(cpu_id) (per_cpu(_curr_size, cpu_id))

static unsigned int last_seqno = 0; /* Used to check for holes in event streams */

inline void init_max_sched_overhead_trace(void) {
	int cpu;
	int i;
	static int start_ids[] = {TS_SCHED_START_EVENT, 
				  TS_SCHED2_START_EVENT,
				  TS_CXS_START_EVENT,
				  TS_RELEASE_START_EVENT,
				  TS_TICK_START_EVENT,
				  TS_SEND_RESCHED_START_EVENT};
	
	for_each_online_cpu(cpu) {

		curr_size_for(cpu) = 0;
		for (i = 0; i < sizeof(start_ids) / sizeof(start_ids[0]); i++) {

			max_overhead_table_for(curr_size_for(cpu), cpu).state = WAIT_FOR_START;
			max_overhead_table_for(curr_size_for(cpu), cpu).start_id = start_ids[i];
			max_overhead_table_for(curr_size_for(cpu), cpu).end_id = start_ids[i]+1;
			max_overhead_table_for(curr_size_for(cpu), cpu).cpu_id = cpu;
			max_overhead_table_for(curr_size_for(cpu), cpu).value = 0;
						
			curr_size_for(cpu) += 1;

			TRACE(KERN_INFO "add_entry with start_id %d, end_id %d, cpu_id %d, executing on cpu %d \n",
			       start_ids[i],
			       start_ids[i]+1,
			       cpu,
			       smp_processor_id());

		}

		max_latency_for(cpu).value = 0;
		/* spin_lock_init(&max_latency_for(cpu).spinlock); */
	}

	max_overheads.cxs = 0;
	max_overheads.sched = 0;
	max_overheads.sched2 = 0;
	max_overheads.release = 0;
	max_overheads.send_resched = 0;
	max_overheads.release_latency = 0;
	max_overheads.tick = 0;

	spin_lock_init(&max_overheads_spinlock);
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
	unsigned long irq_flags;

	
	ts_idx = get_ts_idx(ts, cpu_id);

	/* spin_lock_irqsave(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags); */
	local_irq_save(irq_flags);

	/* prevent re-ordering of ts_idx = ... */
	barrier();
	
	// Check if we are in WAIT_FOR_START_EVENT state; store the timestamp ts
	if ( (max_overhead_table_for(ts_idx, cpu_id).state == WAIT_FOR_START)
	     &&(ts->event == max_overhead_table_for(ts_idx, cpu_id).start_id)
	     &&(ts->cpu == max_overhead_table_for(ts_idx, cpu_id).cpu_id)) {

		max_overhead_table_for(ts_idx, cpu_id).state = WAIT_FOR_MATCH;
		max_overhead_table_for(ts_idx, cpu_id).start_ts = *ts;
		max_overhead_table_for(ts_idx, cpu_id).curr_ts = *ts;
		
		/* spin_unlock_irqrestore(&max_overhead_table_for(ts_idx, cpu_id).spinlock, lock_flags); */
		local_irq_restore(irq_flags);
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

		local_irq_restore(irq_flags);
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
				local_irq_restore(irq_flags);
				return ts_idx;
			}
			local_irq_restore(irq_flags);
			return -1;

	}
	local_irq_restore(irq_flags);
	return -1;	
}

/* 
 * Check if ts yields a maximum overhead
 */
inline int mt_check(struct timestamp* ts, struct timestamp *_start_ts, struct timestamp *_end_ts )
{
	
	int update_r = -1;
	unsigned long irq_flags;
	unsigned long lock_flags; 
	int cpu;
	int i;


	/* First, check for holes in event stream */
	if ((atomic_read((atomic_t*)&last_seqno) + 1 == ts->seq_no)) {

		update_r = update_max_overhead_table(ts, ts->cpu);
		atomic_set((atomic_t*)&last_seqno, ts->seq_no);
	} else {
		/* here we strumbled across a hole, therefore, we reset 
		 * processing of to WAIT_FOR_START  */
		atomic_set((atomic_t*)&last_seqno, ts->seq_no);
		for_each_online_cpu(cpu) {
			for (i = 0; i < curr_size_for(cpu); i++) {
				local_irq_save(irq_flags);
				max_overhead_table_for(i, cpu).state = WAIT_FOR_START;
				local_irq_restore(irq_flags);
			}		
		}
	}




	if (update_r > -1) {
		*_start_ts = max_overhead_table_for(update_r, ts->cpu).start_ts;
		*_end_ts = max_overhead_table_for(update_r, ts->cpu).end_ts;
	

		switch (_start_ts->event) {
		
		case TS_SCHED_START_EVENT: 
			spin_lock_irqsave(&max_overheads_spinlock, lock_flags);
			/* Although we know _start_ts and _end_ts correspond to 
			 * a maximum value, we need to further add this check to make			
			 * the per-cpu max overhead value, which _start_ts and _end_ts
			 * correspond to, is still the global maximum overhead value
			 * (i.e., among all CPUs).		       
			 */
			if (max_overheads.sched < _end_ts->timestamp - _start_ts->timestamp) {
				max_overheads.sched = _end_ts->timestamp - _start_ts->timestamp;
			}
			spin_unlock_irqrestore(&max_overheads_spinlock, lock_flags);
			break;

		case TS_SCHED2_START_EVENT: 
			spin_lock_irqsave(&max_overheads_spinlock, lock_flags);
			if (max_overheads.sched2 < _end_ts->timestamp - _start_ts->timestamp) {
				max_overheads.sched2 = _end_ts->timestamp - _start_ts->timestamp;
			}
			spin_unlock_irqrestore(&max_overheads_spinlock, lock_flags);
			break;

		case TS_CXS_START_EVENT: 
			spin_lock_irqsave(&max_overheads_spinlock, lock_flags);
			if (max_overheads.cxs < _end_ts->timestamp - _start_ts->timestamp) {
				max_overheads.cxs = _end_ts->timestamp - _start_ts->timestamp;
			}
			spin_unlock_irqrestore(&max_overheads_spinlock, lock_flags);
			break;

		case TS_RELEASE_START_EVENT: 
			spin_lock_irqsave(&max_overheads_spinlock, lock_flags);
			if (max_overheads.release < _end_ts->timestamp - _start_ts->timestamp) {
				max_overheads.release = _end_ts->timestamp - _start_ts->timestamp;
			}
			spin_unlock_irqrestore(&max_overheads_spinlock, lock_flags);
			break;
			
		case TS_SEND_RESCHED_START_EVENT: 
			spin_lock_irqsave(&max_overheads_spinlock, lock_flags);
			if (max_overheads.send_resched < _end_ts->timestamp - _start_ts->timestamp) {
				max_overheads.send_resched = _end_ts->timestamp - _start_ts->timestamp;
			}
			spin_unlock_irqrestore(&max_overheads_spinlock, lock_flags);
			break;

		case TS_TICK_START_EVENT: 
			spin_lock_irqsave(&max_overheads_spinlock, lock_flags);
			if (max_overheads.tick < _end_ts->timestamp - _start_ts->timestamp) {
				max_overheads.tick = _end_ts->timestamp - _start_ts->timestamp;
			}
			spin_unlock_irqrestore(&max_overheads_spinlock, lock_flags);
			break;


		default:
			break;

		}

	}

	return update_r;
}

inline int  mt_latency_check(struct timestamp *mt_ts) {

	unsigned long lock_flags;
	unsigned long irq_flags; 

	local_irq_save(irq_flags);
	
	if (mt_ts->timestamp > max_latency_for(mt_ts->cpu).value) {
		
		spin_lock_irqsave(&max_overheads_spinlock, lock_flags);
		if (max_overheads.release_latency < mt_ts->timestamp) {
			max_overheads.release_latency = mt_ts->timestamp;
		}
		spin_unlock_irqrestore(&max_overheads_spinlock, lock_flags);
		local_irq_restore(irq_flags);
		return 1;
	} else {
		local_irq_restore(irq_flags);
		return -1;
	}
}

