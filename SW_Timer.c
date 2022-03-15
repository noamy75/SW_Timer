/*
SW Timer implementation.
The implementataion allows scheduling up to 10 simultaneous SW timer instances, based on a single HW timer module.
The HW timer is connected to the CPU data bus, and its registers are mapped to the addresses defined below.
The HW timer is implemented by a free running 32-bit counter block, counting up at frequency of 1MHz.
*/

typedef unsigned int uint32;

typedef struct {
	uint32 wait_us; // The constant time interval of the timer
	uint32 remain; // The remaining time until next interrupt
} timer_data_t;

#define TMR_VAL_REG 0x10001000 // Read-Only register - current uint32 timer value
#define TMR_CMP_REG 0x10001004 // Write-Only register - uint32 timer interrupt compare value
#define TMR_INT_CLR_REG 0x10001008 // Write-Only uint32 register - write any value to clear interrupt
#define TMR_NUM 10

volatile uint32* tmr_val_reg = TMR_VAL_REG;
volatile uint32* tmr_cmp_reg = TMR_CMP_REG;
volatile uint32* tmr_int_clr_reg = TMR_INT_CLR_REG;

// For inactive timer entries: wait_us==0, remain==0
timer_data_t timer_data[TMR_NUM] = { 0 };

// The timer value of the last time the timer_data array was updated
// Initialized with 0, the real value will be set in the first timer_set call
uint32 last_update_timer_value = 0;

// A function that finds minimal remain
uint32 find_minimal_remain() {
	uint32 minimal_rem = 0xffffffff; // just an initial minimal_rem to begin with
	for (int i = 0; i < TMR_NUM; i++) {
		// Skip inactive timer entries
		if (timer_data[i].wait_us == 0)
			continue;

		// Update minimal if necessary
		uint32 rem_i = timer_data[i].remain;
		if (rem_i < minimal_rem)
			minimal_rem = rem_i;
	}
	return minimal_rem;
}

// A function that sets a new timer
void timer_set(int timer_id, uint32 wait_us) {
	ASSERT(timer_id < TMR_NUM);

	// Assign values of the new timer
	timer_data[timer_id].wait_us = wait_us;
	timer_data[timer_id].remain = wait_us;

	// Read current timer value so all updates are relative to this time
	uint32 current_timer_value = *tmr_val_reg;

	// Update all the timers' remain values with respect to current time
	// In the first call, last_update_timer_value is 0, but all entries are skipped so we don't
	// actually use it before we have a real value
	uint32 time_diff = current_timer_value - last_update_timer_value;
	for (int i = 0; i < TMR_NUM; i++) {
		// No need to update the new timer
		if (timer_id == i)
			continue;

		// Skip inactive timer entries
		if (timer_data[i].wait_us == 0)
			continue;

		timer_data[i].remain -= time_diff;
	}

	// Array was updated - save timer value
	last_update_timer_value = current_timer_value;

	uint32 min_remain = find_minimal_remain(); // The minimal remain after setting the new timer
	*tmr_cmp_reg = current_timer_value + min_remain; // Next interrupt is min_remain from now
}

// Timer interrupt callback function. The interrupt is configured as a Level in the CPU.
__interrupt void timer_interrupt(void) {

	// Find the minimal remain as this is the current interrupt that's firing
	uint32 min_remain = find_minimal_remain();

	for (int i = 0; i < TMR_NUM; i++) {
		// Skip inactive timer entries
		if (timer_data[i].wait_us == 0)
			continue;

		// Update remain of all timers
		timer_data[i].remain -= min_remain;

		// The firing timers now have remain==0 so update them and print
		if (timer_data[i].remain == 0) {
			timer_data[i].remain = timer_data[i].wait_us;
			printf("Firing timer id = %d\n", i);
		}
	}

	// Array was updated - save timer value
	last_update_timer_value = *tmr_val_reg;

	// Set the next interrupt
	min_remain = find_minimal_remain();
	*tmr_cmp_reg = last_update_timer_value + min_remain;

	// End of interrupt - clear
	*tmr_int_clr_reg = 1;
}