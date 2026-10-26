/*
 * Continuous ADC capture with DMA.
 * The ADC runs free at SAMPLE_RATE * ADC_OVERSAMPLE (per channel); DMA moves the samples into one
 * of two buffers. Every call of capture_block() waits for the current buffer (HOP_SIZE output
 * samples = 16 ms), restarts the DMA on the other buffer at once (the ADC FIFO bridges the
 * few microseconds) and slides the new samples into g_samples. So no audio is lost, and each
 * analysis window overlaps the previous one by half.
 *
 * Stereo (@stereo on): the ADC alternates GP26 (left) and GP27 (right). The FIFO has only four
 * places - one lost value would swap the channels from then on. So the DMA restart is protected
 * from interrupts, and after an overflow the capture is restarted, beginning with the left channel.
 * Pico 2 W: while the Ultimate's stream arrives it is used instead, through a jitter buffer.
 * In both cases stereo.c checks that the second channel really carries its own signal.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "analysis.h"
#include "stereo.h"
#ifdef C64VIZ_WIFI
#include "net/net.h"
#include "net/jbuf.h"
#include "net/diag.h"
#endif

#define RAW_HOP (HOP_SIZE * ADC_OVERSAMPLE)

static uint16_t s_raw[2][RAW_HOP * 2];                    /* room for both channels */
static int  s_dma;
static int  s_cur = 0;
static bool s_running = false;
static bool s_stereo_on = false;                          /* @stereo on: read the second channel too */
static stereo_t s_st;
static int  s_src = -1, s_src_stable = -1;               /* 0 = ADC, 1 = stream */
static uint32_t s_src_at;
/* the stereo check restarts only when the other source stays for 2 s: a short gap in the stream
   (the Pico then reads the ADC for a few frames) must not throw away the result */
static void use_source(int src) {
    if (src != s_src) { s_src = src; s_src_at = time_us_32(); return; }
    if (s_src != s_src_stable && time_us_32() - s_src_at > 2000000u) { s_src_stable = s_src; stereo_reset(&s_st); }
}
static uint32_t s_resyncs = 0;
static uint16_t s_hl[HOP_SIZE], s_hr[HOP_SIZE];          /* the new hop of both channels (ADC or stream) */

/* ---- CPU load: idle time = spin-loop rounds counted while waiting for audio. Interrupts that run during
   the wait (the WLAN!) take rounds away, so their time counts as work, not as idle. */
static uint32_t s_spin_until;                             /* time target for done_time() */
static float    s_rounds_per_us = 0.0f;                   /* calibrated in audio_init() */
static uint32_t s_idle_rounds = 0;
static bool __no_inline_not_in_flash_func(done_time)(void) { return (int32_t)(timer_hw->timerawl - s_spin_until) >= 0; }
static bool __no_inline_not_in_flash_func(done_dma)(void) { return !dma_channel_is_busy(s_dma); }
static bool __no_inline_not_in_flash_func(dma_nearly_done)(void) { return dma_hw->ch[s_dma].transfer_count <= 16; }
static uint32_t __no_inline_not_in_flash_func(spin)(bool (*done)(void)) { uint32_t n = 0; while (!done()) n++; return n; }

uint32_t audio_take_idle_us(void) {
    uint32_t r = s_idle_rounds;
    s_idle_rounds = 0;
    return s_rounds_per_us > 0.0f ? (uint32_t)((float)r / s_rounds_per_us) : 0;
}

static void start_dma(int buf) {
    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);
    dma_channel_configure(s_dma, &c, s_raw[buf], &adc_hw->fifo, RAW_HOP * (s_stereo_on ? 2 : 1), true);
}
static void stop_adc(void) {
    if (!s_running) return;
    adc_run(false); dma_channel_abort(s_dma); s_running = false;
}
/* (re)start: always begins with the left channel, so the pairs in the buffer are left, right, left, ... */
static void start_adc(void) {
    adc_run(false);
    adc_fifo_drain();
    adc_select_input(ADC_CHANNEL);
    adc_set_round_robin(s_stereo_on ? (1u << ADC_CHANNEL) | (1u << ADC_CHANNEL_R) : 0);
    /* ADC clock is 48 MHz; period of samples = (1 + div) cycles */
    adc_set_clkdiv(48000000.0f / ((float)SAMPLE_RATE * ADC_OVERSAMPLE * (s_stereo_on ? 2 : 1)) - 1.0f);
    hw_set_bits(&adc_hw->fcs, ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS);   /* clear the sticky flags */
    start_dma(s_cur);
    adc_run(true);
    s_running = true;
}

void audio_init(void) {
    adc_init();
    adc_gpio_init(ADC_GPIO);
    adc_fifo_setup(true, true, 1, false, false);           /* FIFO on, DMA request at 1 sample */
    s_dma = dma_claim_unused_channel(true);
    uint32_t irq = save_and_disable_interrupts();         /* calibrate: rounds per microsecond without interrupts */
    s_spin_until = timer_hw->timerawl + 2000;
    uint32_t n = spin(done_time);
    restore_interrupts(irq);
    s_rounds_per_us = (float)n / 2000.0f;
    for (int i = 0; i < BLOCK_SIZE; i++) g_samples[i] = g_samples_r[i] = 2048;
    stereo_reset(&s_st);
}

void audio_set_stereo(bool on) {
    stop_adc();                                            /* restarted with the new setting on the next block */
    s_stereo_on = on;
    if (on) adc_gpio_init(ADC_GPIO_R);
    /* bias supply for the right input (alternative to a second wire on ADC_VREF): GP28 high while stereo is on,
       high impedance otherwise. 100 kOhm to GP27 draws ~16 uA - nothing for an output. Harmless if unused. */
    gpio_init(PIN_STEREO_BIAS);
    if (on) { gpio_put(PIN_STEREO_BIAS, 1); gpio_set_dir(PIN_STEREO_BIAS, GPIO_OUT); }
    else gpio_set_dir(PIN_STEREO_BIAS, GPIO_IN);
    stereo_reset(&s_st);
    g_stereo = false;
}
int audio_stereo_state(void) { return s_stereo_on ? (int)s_st.state : -1; }
void audio_stereo_hint(int chips) { if (s_stereo_on) stereo_hint(&s_st, chips); }
uint32_t audio_resyncs(void) { return s_resyncs; }

/* one new hop: with r (stereo on) the check decides what is shown; without r, l is the mono signal */
static void deliver(const uint16_t *l, const uint16_t *r, bool from_stream) {
    static uint16_t mix[HOP_SIZE];
    const uint16_t *main = l;
    g_stereo = false;
    if (r) {
        stereo_state_t st = stereo_update(&s_st, l, r, HOP_SIZE);
#ifdef C64VIZ_WIFI
        if (s_st.window && s_st.verdict != s_st.state)     /* a window that disagrees with the current result */
            DIAG("audio: Fenster %s (Korrelation %.2f, rechts/links %.2f, %d in Folge)", stereo_name(s_st.verdict),
                 (double)s_st.corr, (double)s_st.ratio, s_st.run);
#endif
        switch (st) {
            case ST_STEREO: case ST_FILE_STEREO: g_stereo = true; break;
            case ST_EMPTY_R: main = l; break;
            case ST_EMPTY_L: main = r; break;               /* only the right side is wired */
            default:                                       /* still checking, or the same signal on both sides */
                if (from_stream) { for (int i = 0; i < HOP_SIZE; i++) mix[i] = (uint16_t)((l[i] + r[i] + 1) / 2); main = mix; }
                else main = l;                             /* analog: GP26 is the main input */
                break;
        }
        memmove(g_samples_r, g_samples_r + HOP_SIZE, (BLOCK_SIZE - HOP_SIZE) * sizeof(uint16_t));
        memcpy(g_samples_r + (BLOCK_SIZE - HOP_SIZE), r, HOP_SIZE * sizeof(uint16_t));
    }
    memmove(g_samples, g_samples + HOP_SIZE, (BLOCK_SIZE - HOP_SIZE) * sizeof(uint16_t));
    memcpy(g_samples + (BLOCK_SIZE - HOP_SIZE), main, HOP_SIZE * sizeof(uint16_t));
    uint16_t mn = 4095, mx = 0;
    for (int i = 0; i < HOP_SIZE; i++) {
        if (main[i] < mn) mn = main[i];
        if (main[i] > mx) mx = main[i];
        if (g_stereo) { if (r[i] < mn) mn = r[i]; if (r[i] > mx) mx = r[i]; }
    }
    g_clipped = (mn <= CLIP_LOW) || (mx >= CLIP_HIGH);
}

#ifdef C64VIZ_WIFI
/* Pico 2 W: the Ultimate's stream, paced by the Pico's own clock through the jitter buffer */
static jbuf_t   s_jb;
static uint32_t s_next_us;
static void capture_stream(void) {
    uint16_t *l = s_hl, *r = s_hr;
    if (s_src != 1) { jbuf_reset(&s_jb); s_next_us = time_us_32(); }
    use_source(1);
    int32_t ahead = (int32_t)(s_next_us - time_us_32());
    if (ahead > 0) { s_spin_until = s_next_us; s_idle_rounds += spin(done_time); }   /* wait for the frame time */
    else if (ahead < -100000) s_next_us = time_us_32();    /* far behind (e.g. a flash write): no catch-up rush */
    int skip, take = jbuf_take(&s_jb, net_audio_avail(), &skip), got = 0;
    if (skip) net_audio_skip(skip);
    if (take == JB_WAIT) {                                 /* a long WLAN burst used the reserve up: wait as before */
        uint32_t until = time_us_32() + 40000;
        while (net_audio_avail() < HOP_SIZE && (int32_t)(until - time_us_32()) > 0) {
            s_spin_until = timer_hw->timerawl + 250; s_idle_rounds += spin(done_time);
        }
        got = net_audio_read2(l, r, HOP_SIZE);
        s_next_us = time_us_32();                          /* the clock continues from here */
    } else if (take > 0) got = net_audio_read2(l, r, take);
    uint16_t hl = got ? l[got - 1] : 2048, hr = got ? r[got - 1] : 2048;
    for (int i = got; i < HOP_SIZE; i++) { l[i] = hl; r[i] = hr; }   /* missing samples: hold the last value */
    s_next_us += (uint32_t)s_jb.period_us;
    if (s_stereo_on) deliver(l, r, true);
    else {                                                 /* without stereo: the mix of both, as always */
        for (int i = 0; i < HOP_SIZE; i++) l[i] = (uint16_t)((l[i] + r[i] + 1) / 2);
        deliver(l, NULL, true);
    }
}
uint32_t audio_stream_waits(void) { return s_jb.waits; }
#endif

/* Waits for the next HOP_SIZE samples (16 ms) and slides them into g_samples. */
void capture_block(void) {
#ifdef C64VIZ_WIFI
    if (net_audio_active()) { stop_adc(); capture_stream(); return; }
#endif
    use_source(0);
    if (!s_running) start_adc();
    s_idle_rounds += spin(dma_nearly_done);                /* wait for the block (and measure the idle time) */
    /* the switch to the other buffer must not be held up by an interrupt: the FIFO only bridges 4 values */
    uint32_t irq = save_and_disable_interrupts();
    s_idle_rounds += spin(done_dma);
    int done = s_cur;
    s_cur ^= 1;
    start_dma(s_cur);                                       /* restart immediately: no gap */
    restore_interrupts(irq);
    __compiler_memory_barrier();
    bool lost = adc_hw->fcs & ADC_FCS_OVER_BITS;            /* a value was lost (e.g. during a flash write) */

    uint16_t *l = s_hl, *r = s_hr;
    const uint16_t *src = s_raw[done];
    int ch = s_stereo_on ? 2 : 1;
    for (int i = 0; i < HOP_SIZE; i++) {
        uint32_t al = 0, ar = 0;
        for (int k = 0; k < ADC_OVERSAMPLE; k++) {
            al += src[(i * ADC_OVERSAMPLE + k) * ch] & 0x0FFF;
            if (ch == 2) ar += src[(i * ADC_OVERSAMPLE + k) * 2 + 1] & 0x0FFF;
        }
        l[i] = (uint16_t)(al / ADC_OVERSAMPLE);
        r[i] = (uint16_t)(ar / ADC_OVERSAMPLE);
    }
    deliver(l, s_stereo_on ? r : NULL, false);
    if (lost) {
        hw_set_bits(&adc_hw->fcs, ADC_FCS_OVER_BITS);
        if (s_stereo_on) { stop_adc(); s_resyncs++; }      /* the pairs may be shifted: start again with left */
    }
}
