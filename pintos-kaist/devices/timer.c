#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);




static struct list sleep_list;



// Alarm-Clock 구현을 위해 추가한 함수.
void thread_sleep(int64_t ticks);

// 추가해야 할 함수.
// 1. thread 상태를 block으로 만들고, sleep queue 에 넣고 wait 하게 만드는 함수.
// 2. sleep queue에서 wake up 시킬 스레드를 찾고, wake up 시키는 함수.
// 3. 스레드가 가진 최소의 tick 값을 저장하는 함수.
// 4. 최소의 tick 값을 리턴하는 함수.


/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	// 추가한 부분
	list_init(&sleep_list);

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
	}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

// 추가 함수.
bool wakeup_less(const struct list_elem *a, const struct list_elem *b, void *aux) {
    struct thread *t1 = list_entry(a, struct thread, elem);
    struct thread *t2 = list_entry(b, struct thread, elem);
    return t1->wakeup_time < t2->wakeup_time;
}

/* Suspends execution for approximately TICKS timer ticks. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	// printf("-- start : %s, time : %d \n", thread_current()->name, start);

	ASSERT (intr_get_level () == INTR_ON);

	// busy-waiting
	// while (timer_elapsed (start) < ticks)
	// thread_yield ();


	// 다른 방법
	if (timer_elapsed (start) < ticks)
	{
		// thread_sleep함수도 만들어야 함.
		thread_sleep(start+ticks);
	}
	
}

void thread_sleep(int64_t ticks)
{
	struct thread *t;
	t = thread_current();

	// 스레드를 다룰땐 항상 interrupt 허용하면 안됨!!!! 원자성 보존
	enum intr_level old_level;

	// 현재 쓰레드가, idle 스레드가 아니면,
	// caller 스레드의 상태를 blocked로 마꾸고,
	// local tick을 저장한다.(local tick은 나중에 wake up 할 때 필요함)
	// 만약 필요하면 global tick도 업데이트
	// 그리고 schedule 호출.
	if (t == thread_get_idle())
	{
		printf("now idle");
		return;
	}
	old_level = intr_disable();

	t->wakeup_time = ticks;
	list_insert_ordered(&sleep_list, &t->elem, wakeup_less, NULL);  // 정렬 삽입

	
	// t->status = THREAD_BLOCKED;

	thread_block();
	// printf("%s blocked, time : %d\n", t->name, timer_ticks());

	intr_set_level(old_level);
	// 어쨋든 이 함수의 목적은 
	// 1. caller 스레드의 상태를 block으로 바꾸고,
	// 2. sleep queue 에 넣는다.


}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();

	enum intr_level old_level = intr_disable();

	// 추가할 부분 :
	// sleep_list 와 global tick 체크.
	// 아무 스레드나 깨우면 됨.
	// 그리고 필요하면 걔네 ready_list로 옮기기.
	// 그리고 global tick 업데이트 하기.

	struct list_elem *e = list_begin(&sleep_list);

	while (!list_empty(&sleep_list))
	{
		struct thread *t = list_entry(list_front(&sleep_list), struct thread, elem);

		// 이건 sleep_list에 순서가 정해져 있을 때,
		if (t->wakeup_time > ticks) break;

		list_pop_front(&sleep_list);
		thread_unblock(t);
		// printf("%s Unblocked, time : %d\n", t->name, timer_ticks());

		// 이건 sleep_list에 순서가 정해져 있지 않을 때,
		// if (t->wakeup_time <= ticks)
		// {
		// 	e = list_remove(e);
		// 	thread_unblock(t);
		// }
		// else
		// {
		// 	e = list_next(e);
		// }
	}
	
	intr_set_level(old_level);

}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}