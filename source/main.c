#define F_CPU 1000000UL  // 1 MHz
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdbool.h>


#define NUM_LIGHTS 3

#define DEF_PWM_MAX 0xffff
#define DEF_PWM_STEP_WARMING 15
#define DEF_PWM_STEP_COOLING 50
#define DEF_PWM_STEP_CONTROL_K 3

#define PROTECT_FALSE_POSITIVE_MIN_PRC 80
#define PROTECT_BOUNCE 100

#define KEY_HOLD_MIN 500

enum lstate {
    LSTATE_OFF = 0,
    LSTATE_ON = 1,
    LSTATE_WARMING_UP = 1 << 1 | LSTATE_ON,
    LSTATE_COOLING_DOWN = 1 << 2 | LSTATE_ON,
    LSTATE_WARM = 1 << 3 | LSTATE_ON,
    LSTATE_COOL = 1 << 4 | LSTATE_ON,
};

#define RUNSTATE(state) \
        on_kevent_ ## state (l, e)

enum kevent {
    KEVENT_PRESSED,
    KEVENT_UP,
    KEVENT_ONCE,
    KEVENT_TICK,

    KEVENT_NONE,
};
#define ON_KEVENT(state) \
    static inline void on_kevent_ ## state(struct light *l, enum kevent e)

struct light{
    enum lstate state;
    unsigned int pwm_max;
    unsigned int pwm_cur;
    unsigned char pwm_step_warming;
    unsigned char pwm_step_cooling;
    unsigned char pwm_step_control_k;
} lights [NUM_LIGHTS];

#define KSTATE_HIGH 1
#define KSTATE_WENT_ONCE  2

struct key {
    unsigned char state_flags;
    unsigned int tick_counter;
    unsigned int high_tick_counter;
} keys [NUM_LIGHTS];

static inline bool lstate_on (enum lstate lstate)
{
    return lstate & LSTATE_ON;
}

bool light_pwm_up(struct light *l, unsigned char step, unsigned int max)
{
    if (max <= step || max - step <= l->pwm_cur) {
        l->pwm_cur = max;
        return true;
    }
    l->pwm_cur += step;
    return false;
}
bool light_pwm_down(struct light *l, unsigned char step, unsigned int min)
{
    if (min >= step || min + step >= l->pwm_cur) {
        l->pwm_cur = min;
        return true;
    }
    l->pwm_cur -= step;
    return false;
}

ON_KEVENT(OFF)
{
    switch(e) {
    case KEVENT_PRESSED:
        l->state = LSTATE_WARMING_UP;
        break;
    case KEVENT_ONCE:
        l->state = LSTATE_ON;
        break;
    case KEVENT_TICK:
        if (l->pwm_cur > 0) {
            light_pwm_down(l, l->pwm_step_cooling, 0);
        }
        break;
    default:
        return;
        break;
    }
}
ON_KEVENT(ON)
{
    switch(e) {
    case KEVENT_PRESSED:
        l->state = LSTATE_COOLING_DOWN;
        break;
    case KEVENT_ONCE:
        l->state = LSTATE_OFF;
        break;
    case KEVENT_TICK:
        if (l->pwm_cur > l->pwm_max) {
            light_pwm_down(l, l->pwm_step_warming, l->pwm_max);
        } else if (l->pwm_cur < l->pwm_max) {
            light_pwm_up(l, l->pwm_step_warming, l->pwm_max);
        }
        break;
    default:
        return;
        break;
    }
}

ON_KEVENT(WARMING_UP)
{
    switch (e) {
    case KEVENT_UP:
        l->state = LSTATE_WARM;
        break;
    case KEVENT_TICK:
        if(light_pwm_up(l, l->pwm_step_warming / l->pwm_step_control_k,
                    DEF_PWM_MAX)) {
            l->state = LSTATE_WARM;
        }
        break;
    case KEVENT_ONCE:
        l->state = LSTATE_OFF;
        break;
    default:
        return;
        break;
    }
}

ON_KEVENT(COOLING_DOWN)
{
    switch (e) {
    case KEVENT_UP:
        l->state = LSTATE_COOL;
        break;
    case KEVENT_TICK:
        if(light_pwm_down(l, l->pwm_step_cooling / l->pwm_step_control_k, 0)) {
            l->state = LSTATE_OFF;
        }
        break;
    case KEVENT_ONCE:
        l->state = LSTATE_OFF;
        break;
    default:
        return;
        break;
    }
}

ON_KEVENT(WARM)
{
    switch (e) {
    case KEVENT_PRESSED:
        l->state = LSTATE_COOLING_DOWN;
        break;
    case KEVENT_ONCE:
        l->state = LSTATE_OFF;
        break;
    default:
        return;
        break;
    }
}
ON_KEVENT(COOL)
{
    switch (e) {
    case KEVENT_PRESSED:
        l->state = LSTATE_WARMING_UP;
        break;
    case KEVENT_ONCE:
        l->state = LSTATE_OFF;
        break;
    default:
        return;
        break;
    }
}
void on_kevent(struct light *l, enum kevent e)
{
#define CSTATE(state) \
    case LSTATE_ ## state: \
        RUNSTATE(state); \
        break

    switch (l->state) {
        CSTATE(OFF);
        CSTATE(ON);
        CSTATE(WARMING_UP);
        CSTATE(COOLING_DOWN);
        CSTATE(WARM);
        CSTATE(COOL);
    }
#undef CSTATE
}

enum kevent key_event_gen(struct key *k, unsigned char pin)
{
    int kstate = !!(k->state_flags & KSTATE_HIGH);
    pin = !!pin;
    if (k->tick_counter == 0) {
        if (kstate != pin) {
            k->state_flags = pin ? KSTATE_HIGH : 0;
            k->tick_counter = 1;
            if (pin) {
                k->high_tick_counter = 1;
            }
        }
        return KEVENT_NONE;
    }
    ++k->tick_counter;
    if (kstate && pin) {
        ++k->high_tick_counter;
    }
    if (k->tick_counter < PROTECT_BOUNCE) {
        return KEVENT_NONE;
    }
    if (k->tick_counter == PROTECT_BOUNCE && kstate) {
        /* When counter reached BOUNCE level, but the percent high ticks was
         * too little, we assume that it was an interference and reset
         * counter */
        if ((k->high_tick_counter * 100) / k->tick_counter <
                PROTECT_FALSE_POSITIVE_MIN_PRC)
        {
            k->tick_counter = 1;
            k->high_tick_counter = 1;
            return KEVENT_NONE;
        }
    }
    if (!kstate) {
        enum kevent ev = (k->state_flags & KSTATE_WENT_ONCE) ?
            KEVENT_NONE : KEVENT_UP;
        k->tick_counter = 0;
        k->high_tick_counter = 0;
        return ev;
    }
    if (!pin) {
        k->tick_counter = 1;
        k->high_tick_counter = 0;
        k->state_flags = KSTATE_WENT_ONCE;
        return KEVENT_ONCE;
    }
    if (k->tick_counter >= KEY_HOLD_MIN) {
        k->tick_counter = 0;
        k->high_tick_counter = 0;
        return KEVENT_PRESSED;
    }
    return KEVENT_NONE;
}

void key_process(struct key *k, unsigned char pin, struct light *l)
{
    enum kevent ev = key_event_gen(k, pin);
    if (ev != KEVENT_NONE) {
        on_kevent(l, ev);
    }
    on_kevent(l, KEVENT_TICK);
}
void put_pwm(struct light *l, int index)
{
    if (index >= NUM_LIGHTS) {
        return;
    }
    switch (index) {
    case 0:
        OCR2 = l->pwm_cur / 0x100;
        break;
    case 1:
        OCR1A = l->pwm_cur / 0x100;
        break;
    case 2:
        OCR1B = l->pwm_cur / 0x100;
        break;
    }
}


//void pinval(struct kpair *k, int pin)
//{
//    if (pin && k->key != pin && k->tout == 0) {
//        k->port = !k->port;
//        k->tout = 300;
//    }
//    k->key = pin;
//}
//
//void handle_pwm(int index, struct kpair *k) {
//    if (index >= NUM_PWMS) {
//        return;
//    }
//    struct pwm *pwm = &pwms[index];
//    if (k->port && (pwm->cur < pwm->max)) {
//        if (pwm->cur > pwm->max - PWM_STEP_ON) {
//            pwm->cur = pwm->max;
//        } else {
//            pwm->cur += PWM_STEP_ON;
//        }
//    } else if (!k->port && (pwm->cur > 0)) {
//        if (pwm->cur < PWM_STEP_OFF) {
//            pwm->cur = 0;
//        } else {
//            pwm->cur -= PWM_STEP_OFF;
//        }
//    }
//    switch (index) {
//    case 0:
//        OCR1A = pwm->cur / 0x100;
//        break;
//    case 1:
//        OCR1B = pwm->cur / 0x100;
//        break;
//    case 2:
//        OCR2 = pwm->cur / 0x100;
//        break;
//    }
//}

void init_pwm (void)
{
    TCCR1A=(1<<COM1A1)|(1<<COM1B1)|(1<<WGM10);
    TCCR1B=(1<<CS10);
    TCCR2 = (1<<COM21)|(1<<WGM20)|(1<<CS20);
    OCR1A=0x00;
    OCR1B=0x00;
    OCR2=0x00;
}
void init_data(void)
{
    memset(&lights, 0, sizeof(lights));
    memset(&keys, 0, sizeof(keys));

    for (int i=0; i < NUM_LIGHTS; ++i) {
        lights[i].pwm_max = DEF_PWM_MAX;
        lights[i].pwm_step_warming = DEF_PWM_STEP_WARMING;
        lights[i].pwm_step_cooling = DEF_PWM_STEP_COOLING;
        lights[i].pwm_step_control_k = DEF_PWM_STEP_CONTROL_K;
    }
}


int main(void)
{
    DDRD = 0b0111; //Set D-group as output
    DDRC = 0x00; //Set C-group as input
    DDRB = 0x0e;

    init_pwm();
    init_data();
    while (1) {
        unsigned char val = 0;
        for (int i=0; i < NUM_LIGHTS; ++i) {
            unsigned mask = 1 << i;
            struct key *k = keys + i;
            struct light *l = lights + i;
            key_process(k, !!(PINC & mask), l);
            put_pwm(l, i);
            if (lstate_on(l->state)) {
                val |= mask;
            }
        }
        PORTD = val;
    }
    return 0;
}

