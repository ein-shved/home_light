#define F_CPU 1000000UL  // 1 MHz
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>


#define NUM_KEYS 8
#define NUM_PWMS 3
#define PWM_STEP_ON 15
#define PWM_STEP_OFF 50

struct kpair{
    int key;
    int port;
    int tout;
} kpairs[NUM_KEYS];

struct pwm {
    unsigned int max;
    unsigned int cur;
} pwms[NUM_PWMS];

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
    TCCR1A=(1<<COM1A1)|(1<<COM1B1)|(1<<WGM10);
    TCCR1B=(1<<CS10);
    TCCR2 = (1<<COM21)|(1<<WGM20)|(1<<CS20);
    OCR1A=0x00;
    OCR1B=0x00;
    OCR2=0x00;
    for (int i=0; i < NUM_PWMS; ++i) {
        pwms[i].max = 0xffff;
    }
}

void handle_pwm(int index, struct kpair *k) {
    if (index >= NUM_PWMS) {
        return;
    }
    struct pwm *pwm = &pwms[index];
    if (k->port && (pwm->cur < pwm->max)) {
        if (pwm->cur > pwm->max - PWM_STEP_ON) {
            pwm->cur = pwm->max;
        } else {
            pwm->cur += PWM_STEP_ON;
        }
    } else if (!k->port && (pwm->cur > 0)) {
        if (pwm->cur < PWM_STEP_OFF) {
            pwm->cur = 0;
        } else {
            pwm->cur -= PWM_STEP_OFF;
        }
    }
    switch (index) {
    case 0:
        OCR1A = pwm->cur / 0x100;
        break;
    case 1:
        OCR1B = pwm->cur / 0x100;
        break;
    case 2:
        OCR2 = pwm->cur / 0x100;
        break;
    }
}

int main(void)
{
    DDRD = 0xff; //Set D-group as output
    DDRC = 0x00; //Set C-group as input
    DDRB = 0x0e;

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

