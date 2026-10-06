#include "a2.h"

#include "placement.h"

const int a2_kernel[A2_LAYERS] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
const int a2_dilation[A2_LAYERS] = {1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239,
                                    1, 13, 1, 3, 7, 17, 41, 101, 239};

DTCM_DATA a2_weights_t a2_weights_dtcm;
const a2_weights_t* a2_w = &a2_weights_dtcm;

static uint32_t rng_state;

static float uniform(float scale)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return scale * ((float)(rng_state >> 8) * (1.0f / 8388608.0f) - 1.0f);
}

void a2_make_weights(a2_weights_t* w)
{
    rng_state = 12345u;
    for (int l = 0; l < A2_LAYERS; l++) {
        a2_layer_t* L = &w->layer[l];
        const int K = a2_kernel[l];
        const float conv_scale = 0.9f / (float)(A2_C * K);
        for (int k = 0; k < A2_KMAX; k++)
            for (int i = 0; i < A2_C; i++)
                for (int o = 0; o < A2_C; o++)
                    L->w[k][i][o] = k < K ? uniform(conv_scale * 3.0f) : 0.0f;
        for (int o = 0; o < A2_C; o++) {
            L->b[o] = uniform(0.05f);
            L->b1[o] = uniform(0.02f);
            for (int i = 0; i < A2_C; i++)
                L->w1[i][o] = uniform(0.25f);
        }
    }
    for (int o = 0; o < A2_C; o++)
        w->in_w[o] = 0.5f + uniform(0.3f);
}

int a2_total_lookback(void)
{
    int s = 0;
    for (int l = 0; l < A2_LAYERS; l++)
        s += a2_lookback(l);
    return s;
}

int a2_macs_per_sample(void)
{
    int m = A2_C; /* input projection */
    for (int l = 0; l < A2_LAYERS; l++)
        m += A2_C * A2_C * a2_kernel[l] + A2_C * A2_C;
    return m;
}

float a2_input(long i)
{
    /* Plucks every 2400 samples: a decaying triangle-ish wave, float only. */
    const long n = i % 2400;
    const long note = (i / 2400) % 5;
    const float period = 60.0f + 17.0f * (float)note;
    float ph = (float)n / period;
    ph -= (float)(long)ph;
    const float tri = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    float env = 1.0f - (float)n / 2400.0f;
    return 0.4f * env * env * tri;
}
