#define F_CPU 1000000UL  // 1 MHz
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>


#define NUM_KEYS 8
#define PWM_STEP_ON 15
#define PWM_STEP_OFF 25

struct kpair{
    int key;
    int port;
    int tout;
    unsigned int pwm_max;
    unsigned int pwm_cur;
} kpairs[NUM_KEYS];

void pinval(struct kpair *k, int pin)
{
    if (pin && k->key != pin && k->tout == 0) {
        k->port = !k->port;
        k->tout = 300;
    }
    k->key = pin;
}

void init_pwm (void)
{
    TCCR1A=(1<<COM1A1)|(1<<WGM10);
    TCCR1B=(1<<CS10);
    OCR1A=0x00;
    for (int i=0; i < NUM_KEYS; ++i) {
        kpairs[i].pwm_max = 0xffff;
    }
}

void handle_pwm(int index, struct kpair *k) {
    if (index > 0) {
        return;
    }
    if (k->port && (k->pwm_cur < k->pwm_max)) {
        if (k->pwm_cur > k->pwm_max - PWM_STEP_ON) {
            k->pwm_cur = k->pwm_max;
        } else {
            k->pwm_cur += PWM_STEP_ON;
        }
    } else if (!k->port && (k->pwm_cur > 0)) {
        if (k->pwm_cur < PWM_STEP_OFF) {
            k->pwm_cur = 0;
        } else {
            k->pwm_cur -= PWM_STEP_OFF;
        }
    }
    OCR1A = k->pwm_cur / 0x100;
}

int main(void)
{
    int pwm = 0;
    int pwmp = 0;
    int pwms = 1;
    DDRD = 0xff; //Set D-group as output
    DDRC = 0x00; //Set C-group as input
    DDRB=0x02;

    memset (&kpairs, 0, sizeof(kpairs));
    init_pwm();
    while (1) {
        int val = 0;
        for (int i=0; i < NUM_KEYS; ++i) {
            unsigned mask = 1 << i;
            struct kpair *k = kpairs + i;
            pinval(k, !!(PINC & mask));
            if (k->port) {
                val |= mask;
            }
            if (k->tout > 0) {
                --k->tout;
            }
            handle_pwm(i, k);
        }
        PORTD = val;
    }
    return 0;
}

