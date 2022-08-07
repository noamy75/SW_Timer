/*
SW Timer implementation.
The implementataion allows scheduling up to 10 simultaneous SW timer instances, based on a single HW timer module.
The HW timer is connected to the CPU data bus, and its registers are mapped to the addresses defined below.
The HW timer is implemented by a free running 32-bit counter block, counting up at frequency of 1MHz.
*/

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#define TMR_NUM 10
#define MAX_INPUT_LENGTH 256
#define STRINGS_ARE_EQUAL( Str1, Str2 ) ( strcmp( (Str1), (Str2) ) == 0 )

typedef unsigned int uint32;
typedef unsigned char uint8;

typedef struct {
	uint32 wait_us; // The constant time interval of the timer
	uint32 remain; // The remaining time until next interrupt
	uint32 times_fired; // The number of times the timer fired
} timer_data_t;

volatile uint32 tmr_val_reg = 0; // Read-Only register - current uint32 timer value
volatile uint32 tmr_cmp_reg = 0; // Write-Only register - uint32 timer interrupt compare value
volatile uint32 tmr_clr_reg = 0; // Write-Only uint32 register - write any value to clear interrupt
HANDLE h_hw_timer = NULL; // hw timer thread handle
HANDLE h_isr = NULL; // isr thread handle
DWORD hw_timer_tid; // hw timer thread ID (tid)
BOOL g_no_errors = TRUE; // Indicates an error that leads to finishing the program
timer_data_t timer_data[TMR_NUM] = { 0 }; // For inactive timer entries: wait_us==0, remain==0
uint32 last_update_timer_value = 0; /*The timer value of the last time the timer_data array was updated. 
									Initialized with 0, the real value will be set in the first set_timer call*/


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
void set_timer(uint8 timer_id, uint32 wait_us) {

	// input Timer ID exceeds limit
	if (timer_id >= TMR_NUM) {
		printf("ERROR: Timer ID exceeds limit, maximal is: %d\n", TMR_NUM-1);
		return NULL;
	}

	// Assign values of the new timer
	timer_data[timer_id].wait_us = wait_us;
	timer_data[timer_id].remain = wait_us;
	timer_data[timer_id].times_fired = 0;

	// Read current timer value so all updates are relative to this time
	uint32 current_timer_value = tmr_val_reg;

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
	tmr_cmp_reg = current_timer_value + min_remain; // Next interrupt is min_remain from now
}

// Timer interrupt callback function. The interrupt is configured as a Level in the CPU.
void timer_interrupt(void) {

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
			timer_data[i].times_fired++;
			//printf("Firing timer id = %d\n", i);
		}
	}

	// Array was updated - save timer value
	last_update_timer_value = tmr_val_reg;

	// Set the next interrupt
	min_remain = find_minimal_remain();
	tmr_cmp_reg = last_update_timer_value + min_remain;

	// End of interrupt - clear
	tmr_clr_reg = 1;

	return 0;
}

/* The function creates a thread
* Parameters: thread's start routine, thread id and the thread argument
* Returns a thread handle
*/
HANDLE create_thread_simple(LPTHREAD_START_ROUTINE p_start_routine, LPDWORD p_thread_id)
{
	if (NULL == p_start_routine)
	{ // If thread has no start routine
		printf("ERROR: Error when creating a thread\n");
		printf("Received null pointer\n");
		return NULL;
	}

	return CreateThread(
		NULL,            /*  default security attributes */
		0,               /*  use default stack size */
		p_start_routine, /*  thread function */
		NULL,/*  argument to thread function */
		0,               /*  use default creation flags */
		p_thread_id);    /*  returns the thread identifier */
}

// This function closes one handle
// Parameter: pointer to the handle
void close_one_handle(HANDLE* handle, BOOL* no_errors)
{
	if (*handle != NULL)
	{
		if (FALSE == CloseHandle(*handle))
		{ // send thread closing failed
			printf("ERROR: CloseHandle failed\n");
			*no_errors = FALSE;
			return;
		}
		*handle = NULL;
	}
}

/*This function closes thread handles
Parameters: thread handles array*/
void close_handles(HANDLE* thread_handle)
{
	// close hw timer thread handle
	close_one_handle(thread_handle, &g_no_errors);
}

/* This function handling the finish program routine - closing handles
* Parameter: thread handles array
*/
void finish_program_routine(HANDLE* thread_handle)
{
	close_handles(thread_handle); // close all open handles
	if (g_no_errors == FALSE)
	{ // if an error occurred during the program - exit with 1
		exit(1);
	}
}

// This function deactivates a timer
void remove_timer(uint8 timer_id)
{
	// input Timer ID exceeds limit
	if (timer_id >= TMR_NUM) {
		printf("ERROR: Timer ID exceeds limit, maximal is: %d\n", TMR_NUM - 1);
		return NULL;
	}
	
	// Timer is already inactive
	if (timer_data[timer_id].wait_us == 0)
		printf("Timer is already inactive\n");
	
	// Deactivate timer
	else {
		timer_data[timer_id].wait_us = 0;
		timer_data[timer_id].remain = 0;
		timer_data[timer_id].times_fired = 0;
	}
}

// This function displays active timers
void display_timers()
{
	BOOL all_timers_inactive = TRUE;
	for (int i = 0; i < TMR_NUM; i++) {
		if (timer_data[i].wait_us != 0) {
			printf("Timer %u - Interval: %u us, Remain: %u us, Times fired: %u\n", i, timer_data[i].wait_us, timer_data[i].remain, timer_data[i].times_fired);
			all_timers_inactive = FALSE;
		}
	}

	if (all_timers_inactive)
		printf("All timers are inactive\n");
}

// This function prints the main menu to the user
void show_main_menu()
{
	BOOL user_quits = FALSE;
	while (!user_quits && g_no_errors) {
		int decision = 0;
		char decision_str[MAX_INPUT_LENGTH] = { 0 };
		do { //print the main menu
			printf("Choose what to do:\n"
				"1. Display timers\n"
				"2. Set a new timer\n"
				"3. Remove a timer\n"
				"4. Quit\n");
			// get input from user
			gets_s(decision_str, MAX_INPUT_LENGTH);
			if (STRINGS_ARE_EQUAL(decision_str, "1")) {
				// user chose to display timers
				display_timers();
				decision = atoi(decision_str); // convert the string to integer
			}
			else if (STRINGS_ARE_EQUAL(decision_str, "2")) {
				// client chose to set a new timer
				printf("Insert timer ID and desired interval (ex: 1, 5):\n");
				uint8 timer_id;
				uint32 wait_us;
				scanf_s("%" SCNu8 ", %u", &timer_id, &wait_us); // wait_us can be given negative - it's not a bug, it's a feature!
				getc(stdin);
				set_timer(timer_id, wait_us);
				decision = atoi(decision_str); // convert the string to integer
			}
			else if (STRINGS_ARE_EQUAL(decision_str, "3")) {
				// client chose to remove a timer
				printf("Insert timer ID to remove:\n");
				uint8 timer_id;
				scanf_s("%" SCNu8, &timer_id);
				getc(stdin);
				remove_timer(timer_id);
				decision = atoi(decision_str); // convert the string to integer
			}
			else if (STRINGS_ARE_EQUAL(decision_str, "4")) {
				// client chose to quit
				user_quits = TRUE;
				decision = atoi(decision_str); // convert the string to integer
			}
			else {
				// user inserted invalid input
				printf("Error: Illegal command\n");
			}
		} while (!decision && !user_quits && g_no_errors); // user will be able to choose again until he inserts valid input
	}

	// error occured, probably in hw timer thread
	if(!g_no_errors)
		finish_program_routine(h_hw_timer); // finish program routine
}

// Entry point of the HW timer simulating thread
void hw_timer_thread()
{
	DWORD isr_tid; // isr thread ID
	while (TRUE) {
		tmr_val_reg++;
		//printf("hw timer increased by 1, tmr_val_reg = %d\n", tmr_val_reg);
		if (tmr_val_reg == tmr_cmp_reg)
		{
			h_isr = create_thread_simple((LPTHREAD_START_ROUTINE)timer_interrupt, &isr_tid);
			if (h_isr == NULL)
			{ // ISR thread creation failed
				printf("ERROR: create_thread_simple - hw timer thread\n");
				g_no_errors = FALSE;
			}
		}
		Sleep(0.001); // delay of 0.001ms=1us -> freq=1MHz
	}
}

int main() {

	h_hw_timer = create_thread_simple((LPTHREAD_START_ROUTINE)hw_timer_thread, &hw_timer_tid);
	if (h_hw_timer == NULL)
	{ // hw timer thread creation failed
		printf("ERROR: create_thread_simple - hw timer thread\n");
		g_no_errors = FALSE;
		finish_program_routine(h_hw_timer); // finish program routine
	}

	show_main_menu();

	return 0;

}
