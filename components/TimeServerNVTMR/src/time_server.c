/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(NICTA_GPL)
 */


#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdio.h>
#include <utils/math.h>
#include <utils/util.h>
#include <sel4/sel4.h>
#include <sel4/arch/constants.h>
#include <platsupport/timer.h>
#include <platsupport/plat/timer.h>

#include <camkes.h>

/* Prototype for this function is not generated by the camkes templates yet */
seL4_Word the_timer_get_sender_id();

/* Frequency of timer interrupts that we use for processing timeouts */
#define TIMER_FREQUENCY 1000 // once per millisecond
#define TIMER_PERIOD (NS_IN_S / TIMER_FREQUENCY)

static pstimer_t *timer = NULL;

#define TIMER_TYPE_OFF 0
#define TIMER_TYPE_PERIODIC 1
#define TIMER_TYPE_ABSOLUTE 2
#define TIMER_TYPE_RELATIVE 3

typedef struct client_timer {
    int id;
    int client_id;
    int timer_type;
    uint64_t periodic_ns;
    uint64_t timeout_time;
    struct client_timer *prev, *next;
} client_timer_t;

typedef struct client_state {
    int id;
    uint32_t completed;
    client_timer_t *timers;
} client_state_t;

/* sorted list of active timers */
static client_timer_t *timer_head = NULL;

/* declare the memory needed for the clients */
static client_state_t *client_state = NULL;

/* We want a small malloc region */
/*
#define SERVER_CORE_SIZE 8192
static char core_buf[SERVER_CORE_SIZE];
extern char *morecore_area;
extern size_t morecore_size;
*/

void the_timer_emit(unsigned int);
int the_timer_largest_badge(void);

static uint64_t the_current_time = 0;

void update_current_time_ns(uint64_t ns)
{
    the_current_time += ns;
}

static uint64_t current_time_ns()
{
    return the_current_time;
}

static void remove_timer(client_timer_t *timer)
{
    if (timer->prev) {
        timer->prev->next = timer->next;
    } else {
        assert(timer == timer_head);
        timer_head = timer->next;
    }
    if (timer->next) {
        timer->next->prev = timer->prev;
    }
}

static void insert_timer(client_timer_t *timer)
{
    client_timer_t *current, *next;
    for (current = NULL, next = timer_head; next && next->timeout_time < timer->timeout_time; current = next, next = next->next);
    timer->prev = current;
    timer->next = next;
    if (next) {
        next->prev = timer;
    }
    if (current) {
        current->next = timer;
    } else {
        timer_head = timer;
    }
}

static void signal_client(client_timer_t *timer, uint64_t current_time)
{
    the_timer_emit(timer->client_id + 1);
    client_state[timer->client_id].completed |= BIT(timer->id);
    remove_timer(timer);
    switch (timer->timer_type) {
    case TIMER_TYPE_OFF:
        assert(!"not possible");
        break;
    case TIMER_TYPE_PERIODIC:
        timer->timeout_time += timer->periodic_ns;
        insert_timer(timer);
        break;
    case TIMER_TYPE_ABSOLUTE:
    case TIMER_TYPE_RELATIVE:
        timer->timer_type = TIMER_TYPE_OFF;
        break;
    }
}

static void signal_clients(uint64_t current_time)
{
    while (timer_head && timer_head->timeout_time <= current_time) {
        signal_client(timer_head, current_time);
    }
}


void irq_handle(void)
{
    time_server_lock();
    update_current_time_ns(TIMER_PERIOD);
    signal_clients(current_time_ns());
    timer_handle_irq(timer, 0);
    irq_acknowledge();
    time_server_unlock();
}

static int _oneshot_relative(int cid, int tid, uint64_t ns)
{
    if (tid >= timers_per_client || tid < 0) {
        return -1;
    }
    time_server_lock();
    client_timer_t *t = &client_state[cid].timers[tid];
    if (t->timer_type != TIMER_TYPE_OFF) {
        remove_timer(t);
    }
    t->timer_type = TIMER_TYPE_RELATIVE;
    t->timeout_time = current_time_ns() + ns;
    insert_timer(t);
    time_server_unlock();
    return 0;
}

static int _oneshot_absolute(int cid, int tid, uint64_t ns)
{
    if (tid >= timers_per_client || tid < 0) {
        return -1;
    }
    time_server_lock();
    client_timer_t *t = &client_state[cid].timers[tid];
    if (t->timer_type != TIMER_TYPE_OFF) {
        remove_timer(t);
    }
    t->timer_type = TIMER_TYPE_ABSOLUTE;
    t->timeout_time = ns;
    insert_timer(t);
    time_server_unlock();
    return 0;
}

static int _periodic(int cid, int tid, uint64_t ns)
{
    if (tid >= timers_per_client || tid < 0) {
        return -1;
    }
    time_server_lock();
    client_timer_t *t = &client_state[cid].timers[tid];
    if (t->timer_type != TIMER_TYPE_OFF) {
        remove_timer(t);
    }
    t->timer_type = TIMER_TYPE_PERIODIC;
    t->periodic_ns = ns;
    t->timeout_time = current_time_ns() + ns;
    insert_timer(t);
    time_server_unlock();
    return 0;
}

static int _stop(int cid, int tid)
{
    if (tid >= timers_per_client || tid < 0) {
        return -1;
    }
    time_server_lock();
    client_timer_t *t = &client_state[cid].timers[tid];
    if (t->timer_type != TIMER_TYPE_OFF) {
        remove_timer(t);
        t->timer_type = TIMER_TYPE_OFF;
    }
    time_server_unlock();
    return 0;
}

static unsigned int _completed(int cid)
{
    unsigned int ret;
    time_server_lock();
    ret = client_state[cid].completed;
    client_state[cid].completed = 0;
    time_server_unlock();
    return ret;
}

static uint64_t _time(int cid)
{
    uint64_t ret;
    ret = current_time_ns();
    return ret;
}

/* substract 1 from the badge as we started counting badges at 1
 * to avoid using the 0 badge */
int the_timer_oneshot_relative(int id, uint64_t ns)
{
    return _oneshot_relative(the_timer_get_sender_id() - 1, id, ns);
}

int the_timer_oneshot_absolute(int id, uint64_t ns)
{
    return _oneshot_absolute(the_timer_get_sender_id() - 1, id, ns);
}

int the_timer_periodic(int id, uint64_t ns)
{
    return _periodic(the_timer_get_sender_id() - 1, id, ns);
}

int the_timer_stop(int id)
{
    return _stop(the_timer_get_sender_id() - 1, id);
}

unsigned int the_timer_completed()
{
    return _completed(the_timer_get_sender_id() - 1);
}

uint64_t the_timer_time()
{
    return _time(the_timer_get_sender_id() - 1);
}

uint64_t the_timer_tsc_frequency()
{
    return TIMER_FREQUENCY;
}

void pre_init(void)
{
    /*
        morecore_area = core_buf;
        morecore_size = SERVER_CORE_SIZE;
    */
}

void post_init()
{
    time_server_lock();
    client_state = malloc(sizeof(client_state_t) * the_timer_largest_badge());
    assert(client_state);
    for (int i = 0; i < the_timer_largest_badge(); i++) {
        client_state[i].id = i;
        client_state[i].completed = 0;
        client_state[i].timers = malloc(sizeof(client_timer_t) * timers_per_client);
        assert(client_state[i].timers);
        for (int j = 0; j < timers_per_client; j++) {
            client_state[i].timers[j] =
            (client_timer_t) {
                .id = j, .client_id = i, .timer_type = TIMER_TYPE_OFF, .prev = NULL, .next = NULL
            };
        }
    }

    uint32_t irq = 0;
    uint32_t offset = 0;
    switch (nvtmr_number) {
    case TMR0:
        offset = TMR0_OFFSET;
        irq = INT_NV_TMR0;
        break;
    case TMR1:
        offset = TMR1_OFFSET;
        irq = INT_NV_TMR1;
        break;
    case TMR2:
        offset = TMR2_OFFSET;
        irq = INT_NV_TMR2;
        break;
    case TMR3:
        offset = TMR3_OFFSET;
        irq = INT_NV_TMR3;
        break;
    case TMR4:
        offset = TMR4_OFFSET;
        irq = INT_NV_TMR4;
        break;
    case TMR5:
        offset = TMR5_OFFSET;
        irq = INT_NV_TMR5;
        break;
    case TMR6:
        offset = TMR6_OFFSET;
        irq = INT_NV_TMR6;
        break;
    case TMR7:
        offset = TMR7_OFFSET;
        irq = INT_NV_TMR7;
        break;
    case TMR8:
        offset = TMR8_OFFSET;
        irq = INT_NV_TMR8;
        break;
    case TMR9:
        offset = TMR9_OFFSET;
        irq = INT_NV_TMR9;
        break;
    default:
        ZF_LOGE("TimeServerNVTMR: Invalid timer device ID %d requested. Timer will not function.", nvtmr_number);
        return;
    }

    nv_tmr_config_t config;
    config.vaddr = (void*)timer_reg + offset;
    config.tmrus_vaddr = (void*)timer_reg + TMRUS_OFFSET;
    config.shared_vaddr = NULL;
    config.irq = irq;
    timer = tk1_get_timer(&config);
    assert(timer);
    irq_acknowledge();
    /* start timer */
    timer_start(timer);
    timer_periodic(timer, TIMER_PERIOD);
    time_server_unlock();
}
