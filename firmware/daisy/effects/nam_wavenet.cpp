/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "nam_wavenet.h"
#include <cstring>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// Padé(3,2) tanh — accurate to ~2.4% over [-3,3], exact outside via clamp.
// Plain division: single VDIV per call, no FP↔INT domain crossing stall.
// Four independent calls pipeline: VDIVs issue at t=0,7,14,21 on M7.
// ─────────────────────────────────────────────────────────────────────────────
static inline float nam_tanh(float x) noexcept
{
    const float x2  = x * x;
    const float num = x * (27.0f + x2);
    const float den = 27.0f + 9.0f * x2;
    return num / den;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layer4::process_block
//
// Processes N frames through a single 4-channel WaveNet layer.
// Loop structure: sample-inner over N frames.
//
// For each sample n:
//   1. Write ins[n] into circular state buffer (s0 == ins, no extra read).
//   2. Read s1 = state[ptr-D], s2 = state[ptr-2D].
//   3. Conv: tmp[4] = conv_b + W0@ins[n] + W1@s1 + W2@s2 + mixin*cond[n]
//      Weights declared as local const → compiler register-allocates all 48 floats.
//      With NAM_BLOCK=48, the inner loop reuses register-resident weights.
//   4. Activation: tanh(tmp[k]) → a[k]
//   5. Accumulate head: head[n][k] += a[k]
//   6. 1×1 + residual: outs[n] = ins[n] + b1x1 + W1x1 @ a
// ─────────────────────────────────────────────────────────────────────────────
void NamWavenet::Layer4::process_block(const float ins[][4], float outs[][4],
                                       const float* cond, float head[][4],
                                       int N) noexcept
{
    // Hoist all weight matrices as local const.
    // GCC -O2 -ffast-math: loop-invariant, register-allocated across the N loop.
    const float c0_00 = w0T[0][0], c0_01 = w0T[0][1], c0_02 = w0T[0][2], c0_03 = w0T[0][3];
    const float c0_10 = w0T[1][0], c0_11 = w0T[1][1], c0_12 = w0T[1][2], c0_13 = w0T[1][3];
    const float c0_20 = w0T[2][0], c0_21 = w0T[2][1], c0_22 = w0T[2][2], c0_23 = w0T[2][3];
    const float c0_30 = w0T[3][0], c0_31 = w0T[3][1], c0_32 = w0T[3][2], c0_33 = w0T[3][3];

    const float c1_00 = w1T[0][0], c1_01 = w1T[0][1], c1_02 = w1T[0][2], c1_03 = w1T[0][3];
    const float c1_10 = w1T[1][0], c1_11 = w1T[1][1], c1_12 = w1T[1][2], c1_13 = w1T[1][3];
    const float c1_20 = w1T[2][0], c1_21 = w1T[2][1], c1_22 = w1T[2][2], c1_23 = w1T[2][3];
    const float c1_30 = w1T[3][0], c1_31 = w1T[3][1], c1_32 = w1T[3][2], c1_33 = w1T[3][3];

    const float c2_00 = w2T[0][0], c2_01 = w2T[0][1], c2_02 = w2T[0][2], c2_03 = w2T[0][3];
    const float c2_10 = w2T[1][0], c2_11 = w2T[1][1], c2_12 = w2T[1][2], c2_13 = w2T[1][3];
    const float c2_20 = w2T[2][0], c2_21 = w2T[2][1], c2_22 = w2T[2][2], c2_23 = w2T[2][3];
    const float c2_30 = w2T[3][0], c2_31 = w2T[3][1], c2_32 = w2T[3][2], c2_33 = w2T[3][3];

    const float cx_00 = w1x1T[0][0], cx_01 = w1x1T[0][1], cx_02 = w1x1T[0][2], cx_03 = w1x1T[0][3];
    const float cx_10 = w1x1T[1][0], cx_11 = w1x1T[1][1], cx_12 = w1x1T[1][2], cx_13 = w1x1T[1][3];
    const float cx_20 = w1x1T[2][0], cx_21 = w1x1T[2][1], cx_22 = w1x1T[2][2], cx_23 = w1x1T[2][3];
    const float cx_30 = w1x1T[3][0], cx_31 = w1x1T[3][1], cx_32 = w1x1T[3][2], cx_33 = w1x1T[3][3];

    const float mb0 = conv_b[0], mb1 = conv_b[1], mb2 = conv_b[2], mb3 = conv_b[3];
    const float mx0 = mixin_w[0], mx1 = mixin_w[1], mx2 = mixin_w[2], mx3 = mixin_w[3];
    const float bx0 = b1x1[0], bx1 = b1x1[1], bx2 = b1x1[2], bx3 = b1x1[3];

    const int ss  = state_size;
    const int nd  = neg_dilation;
    const int nd2 = neg_2dilation;
    int ptr = state_ptr;

    for (int n = 0; n < N; ++n)
    {
        // ── Write tap0 = ins[n] into circular state ───────────────────────
        float* const cw = state + ptr * 4;
        const float i0 = ins[n][0], i1 = ins[n][1],
                    i2 = ins[n][2], i3 = ins[n][3];
        cw[0] = i0; cw[1] = i1; cw[2] = i2; cw[3] = i3;

        // ── Tap positions ─────────────────────────────────────────────────
        int sp1 = ptr + nd;  if (sp1 >= ss) sp1 -= ss;
        int sp2 = ptr + nd2; if (sp2 >= ss) sp2 -= ss;

        ++ptr; if (ptr == ss) ptr = 0;

        const float* __restrict__ s1 = state + sp1 * 4;
        const float* __restrict__ s2 = state + sp2 * 4;

        // ── Conv: bias + W0@i + W1@s1 + W2@s2 + mixin*cond ───────────────
        // 4 independent accumulators — no FPU dependency stalls.
        float t0 = mb0, t1 = mb1, t2 = mb2, t3 = mb3;

        // Tap 0 (register-resident weights, no memory access)
        t0 += i0*c0_00 + i1*c0_10 + i2*c0_20 + i3*c0_30;
        t1 += i0*c0_01 + i1*c0_11 + i2*c0_21 + i3*c0_31;
        t2 += i0*c0_02 + i1*c0_12 + i2*c0_22 + i3*c0_32;
        t3 += i0*c0_03 + i1*c0_13 + i2*c0_23 + i3*c0_33;

        // Tap 1
        const float s1_0 = s1[0], s1_1 = s1[1], s1_2 = s1[2], s1_3 = s1[3];
        t0 += s1_0*c1_00 + s1_1*c1_10 + s1_2*c1_20 + s1_3*c1_30;
        t1 += s1_0*c1_01 + s1_1*c1_11 + s1_2*c1_21 + s1_3*c1_31;
        t2 += s1_0*c1_02 + s1_1*c1_12 + s1_2*c1_22 + s1_3*c1_32;
        t3 += s1_0*c1_03 + s1_1*c1_13 + s1_2*c1_23 + s1_3*c1_33;

        // Tap 2
        const float s2_0 = s2[0], s2_1 = s2[1], s2_2 = s2[2], s2_3 = s2[3];
        t0 += s2_0*c2_00 + s2_1*c2_10 + s2_2*c2_20 + s2_3*c2_30;
        t1 += s2_0*c2_01 + s2_1*c2_11 + s2_2*c2_21 + s2_3*c2_31;
        t2 += s2_0*c2_02 + s2_1*c2_12 + s2_2*c2_22 + s2_3*c2_32;
        t3 += s2_0*c2_03 + s2_1*c2_13 + s2_2*c2_23 + s2_3*c2_33;

        // Mixin conditioning
        const float cn = cond[n];
        t0 += mx0 * cn; t1 += mx1 * cn; t2 += mx2 * cn; t3 += mx3 * cn;

        // ── Activation (4 independent VDIVs, pipeline at throughput=7) ────
        const float a0 = nam_tanh(t0), a1 = nam_tanh(t1),
                    a2 = nam_tanh(t2), a3 = nam_tanh(t3);

        // ── Accumulate head ───────────────────────────────────────────────
        head[n][0] += a0; head[n][1] += a1;
        head[n][2] += a2; head[n][3] += a3;

        // ── 1×1 + residual ────────────────────────────────────────────────
        float o0 = i0 + bx0, o1 = i1 + bx1, o2 = i2 + bx2, o3 = i3 + bx3;
        o0 += a0*cx_00 + a1*cx_10 + a2*cx_20 + a3*cx_30;
        o1 += a0*cx_01 + a1*cx_11 + a2*cx_21 + a3*cx_31;
        o2 += a0*cx_02 + a1*cx_12 + a2*cx_22 + a3*cx_32;
        o3 += a0*cx_03 + a1*cx_13 + a2*cx_23 + a3*cx_33;

        outs[n][0] = o0; outs[n][1] = o1;
        outs[n][2] = o2; outs[n][3] = o3;
    }

    state_ptr = ptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layer2::process_block
// ─────────────────────────────────────────────────────────────────────────────
void NamWavenet::Layer2::process_block(const float ins[][2], float outs[][2],
                                       const float* cond, float head[][2],
                                       int N) noexcept
{
    // Hoist weights — 2×2 matrices, 4 floats each
    const float c0_00 = w0T[0][0], c0_01 = w0T[0][1];
    const float c0_10 = w0T[1][0], c0_11 = w0T[1][1];

    const float c1_00 = w1T[0][0], c1_01 = w1T[0][1];
    const float c1_10 = w1T[1][0], c1_11 = w1T[1][1];

    const float c2_00 = w2T[0][0], c2_01 = w2T[0][1];
    const float c2_10 = w2T[1][0], c2_11 = w2T[1][1];

    const float cx_00 = w1x1T[0][0], cx_01 = w1x1T[0][1];
    const float cx_10 = w1x1T[1][0], cx_11 = w1x1T[1][1];

    const float mb0 = conv_b[0],  mb1 = conv_b[1];
    const float mx0 = mixin_w[0], mx1 = mixin_w[1];
    const float bx0 = b1x1[0],    bx1 = b1x1[1];

    const int ss  = state_size;
    const int nd  = neg_dilation;
    const int nd2 = neg_2dilation;
    int ptr = state_ptr;

    for (int n = 0; n < N; ++n)
    {
        float* const cw = state + ptr * 2;
        const float i0 = ins[n][0], i1 = ins[n][1];
        cw[0] = i0; cw[1] = i1;

        int sp1 = ptr + nd;  if (sp1 >= ss) sp1 -= ss;
        int sp2 = ptr + nd2; if (sp2 >= ss) sp2 -= ss;

        ++ptr; if (ptr == ss) ptr = 0;

        const float* __restrict__ s1 = state + sp1 * 2;
        const float* __restrict__ s2 = state + sp2 * 2;

        float t0 = mb0, t1 = mb1;

        t0 += i0*c0_00 + i1*c0_10;
        t1 += i0*c0_01 + i1*c0_11;

        const float s1_0 = s1[0], s1_1 = s1[1];
        t0 += s1_0*c1_00 + s1_1*c1_10;
        t1 += s1_0*c1_01 + s1_1*c1_11;

        const float s2_0 = s2[0], s2_1 = s2[1];
        t0 += s2_0*c2_00 + s2_1*c2_10;
        t1 += s2_0*c2_01 + s2_1*c2_11;

        const float cn = cond[n];
        t0 += mx0 * cn; t1 += mx1 * cn;

        const float a0 = nam_tanh(t0), a1 = nam_tanh(t1);

        head[n][0] += a0; head[n][1] += a1;

        outs[n][0] = i0 + bx0 + a0*cx_00 + a1*cx_10;
        outs[n][1] = i1 + bx1 + a0*cx_01 + a1*cx_11;
    }

    state_ptr = ptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// init_state_ptrs
// ─────────────────────────────────────────────────────────────────────────────
void NamWavenet::init_state_ptrs()
{
    static constexpr int la0_d[7]  = { 1, 2, 4, 8, 16, 32, 64 };
    static constexpr int la1_d[13] = { 128, 256, 512, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };

    int off = 0;
    for (int i = 0; i < 7; ++i)
    {
        const int d  = la0_d[i];
        const int ss = (3-1)*d + 1;
        la0[i].state_size    = ss;
        la0[i].state_ptr     = 0;
        la0[i].neg_dilation  = ss - d;
        la0[i].neg_2dilation = ss - 2*d;
        la0[i].state         = la0_state + off;
        off += 4 * ss;
    }

    off = 0;
    for (int i = 0; i < 13; ++i)
    {
        const int d  = la1_d[i];
        const int ss = (3-1)*d + 1;
        la1[i].state_size    = ss;
        la1[i].state_ptr     = 0;
        la1[i].neg_dilation  = ss - d;
        la1[i].neg_2dilation = ss - 2*d;
        la1[i].state         = la1_state + off;
        off += 2 * ss;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// reset
// ─────────────────────────────────────────────────────────────────────────────
void NamWavenet::reset()
{
    // Zero state buffers using assignment (not memcpy — 4× faster on Daisy)
    for (int i = 0; i < 1044; ++i) la0_state[i] = 0.0f;
    for (int i = 0; i < 7702; ++i) la1_state[i] = 0.0f;
    for (int i = 0; i < 7;  ++i) la0[i].state_ptr = 0;
    for (int i = 0; i < 13; ++i) la1[i].state_ptr = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// load_weights — float[842]
//
// File weight order: conv[out][in][k], k=0 oldest, k=2 newest.
// Split at load time:
//   file k=2 → w0T[in][out]  (tap0 = current input)
//   file k=1 → w1T[in][out]
//   file k=0 → w2T[in][out]
// ─────────────────────────────────────────────────────────────────────────────
void NamWavenet::load_weights(const float* model_weights)
{
    init_state_ptrs();
    reset();

    const float* w = model_weights;

    // ── LA0 ──────────────────────────────────────────────────────────────
    for (int o = 0; o < 4; ++o) la0_rechannel_w[o] = *w++;

    for (int l = 0; l < 7; ++l)
    {
        for (int o = 0; o < 4; ++o)
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 3; ++k)
                {
                    const float val = *w++;
                    if      (k == 2) la0[l].w0T[i][o] = val;
                    else if (k == 1) la0[l].w1T[i][o] = val;
                    else             la0[l].w2T[i][o] = val;
                }

        for (int o = 0; o < 4; ++o) la0[l].conv_b[o]  = *w++;
        for (int o = 0; o < 4; ++o) la0[l].mixin_w[o] = *w++;
        for (int o = 0; o < 4; ++o)
            for (int i = 0; i < 4; ++i)
                la0[l].w1x1T[i][o] = *w++;
        for (int o = 0; o < 4; ++o) la0[l].b1x1[o] = *w++;
    }

    for (int o = 0; o < 2; ++o)
        for (int i = 0; i < 4; ++i)
            la0_head_wT[i][o] = *w++;

    // ── LA1 ──────────────────────────────────────────────────────────────
    for (int o = 0; o < 2; ++o)
        for (int i = 0; i < 4; ++i)
            la1_rechannel_wT[i][o] = *w++;

    for (int l = 0; l < 13; ++l)
    {
        for (int o = 0; o < 2; ++o)
            for (int i = 0; i < 2; ++i)
                for (int k = 0; k < 3; ++k)
                {
                    const float val = *w++;
                    if      (k == 2) la1[l].w0T[i][o] = val;
                    else if (k == 1) la1[l].w1T[i][o] = val;
                    else             la1[l].w2T[i][o] = val;
                }

        for (int o = 0; o < 2; ++o) la1[l].conv_b[o]  = *w++;
        for (int o = 0; o < 2; ++o) la1[l].mixin_w[o] = *w++;
        for (int o = 0; o < 2; ++o)
            for (int i = 0; i < 2; ++i)
                la1[l].w1x1T[i][o] = *w++;
        for (int o = 0; o < 2; ++o) la1[l].b1x1[o] = *w++;
    }

    la1_head_w[0] = *w++;
    la1_head_w[1] = *w++;
    la1_head_b    = *w++;
    head_scale    = *w++;

    assert((int)(w - model_weights) == 842);
}

// ─────────────────────────────────────────────────────────────────────────────
// forward — process exactly NAM_BLOCK samples
//
// Structure (layer-outer, sample-inner):
//
//   LA0:
//     rechannel input[n] → la0_buf_a[n][4]
//     zero la0_head
//     for each of 7 layers:
//       process_block(ping, pong, input, la0_head)
//       swap ping/pong pointers
//     project la0_head[n][4] → la1_head[n][2]  (initial g0,g1 for LA1)
//
//   LA1:
//     rechannel la0_buf_last[n][4] → la1_buf_a[n][2]
//     for each of 13 layers:
//       process_block(ping, pong, input, la1_head)
//       swap ping/pong
//
//   Output:
//     output[n] = head_scale * (la1_head_b + la1_head[n] @ la1_head_w)
// ─────────────────────────────────────────────────────────────────────────────
void NamWavenet::forward(const float* input, float* output) noexcept
{
    const int N = NAM_BLOCK;

    // ── LA0 rechannel: 1→4 ────────────────────────────────────────────────
    {
        const float r0 = la0_rechannel_w[0], r1 = la0_rechannel_w[1],
                    r2 = la0_rechannel_w[2], r3 = la0_rechannel_w[3];
        for (int n = 0; n < N; ++n)
        {
            const float x = input[n];
            la0_buf_a[n][0] = r0 * x; la0_buf_a[n][1] = r1 * x;
            la0_buf_a[n][2] = r2 * x; la0_buf_a[n][3] = r3 * x;
        }
    }

    // ── Zero head accumulators ────────────────────────────────────────────
    for (int n = 0; n < N; ++n)
    {
        la0_head[n][0] = la0_head[n][1] = la0_head[n][2] = la0_head[n][3] = 0.0f;
    }

    // ── LA0 layer loop ────────────────────────────────────────────────────
    // ping = la0_buf_a, pong = la0_buf_b; swap after each layer.
    {
        const float (*ping)[4] = la0_buf_a;
        float       (*pong)[4] = la0_buf_b;

        for (int l = 0; l < 7; ++l)
        {
            la0[l].process_block(ping, pong, input, la0_head, N);
            // Swap ping/pong via index (avoids const-cast issues)
            float (*tmp)[4] = pong; pong = (float(*)[4])ping; ping = tmp;
        }
        // After 7 swaps (odd): last output is in ping (= la0_buf_b)
        (void)ping;
    }
    // 7 layers, 7 swaps: output alternates a→b→a→b→a→b→a→b
    // After layer 0 writes to b, layer 1 writes to a, ..., layer 6 writes to b.
    // la0_buf_b now contains the final LA0 output (layer 6 outs)

    // ── Project LA0 head 4→2 → initialise la1_head ───────────────────────
    {
        const float h00 = la0_head_wT[0][0], h01 = la0_head_wT[0][1];
        const float h10 = la0_head_wT[1][0], h11 = la0_head_wT[1][1];
        const float h20 = la0_head_wT[2][0], h21 = la0_head_wT[2][1];
        const float h30 = la0_head_wT[3][0], h31 = la0_head_wT[3][1];
        for (int n = 0; n < N; ++n)
        {
            const float h0 = la0_head[n][0], h1 = la0_head[n][1],
                        h2 = la0_head[n][2], h3 = la0_head[n][3];
            la1_head[n][0] = h0*h00 + h1*h10 + h2*h20 + h3*h30;
            la1_head[n][1] = h0*h01 + h1*h11 + h2*h21 + h3*h31;
        }
    }

    // ── LA1 rechannel: 4→2 ───────────────────────────────────────────────
    {
        const float r00 = la1_rechannel_wT[0][0], r01 = la1_rechannel_wT[0][1];
        const float r10 = la1_rechannel_wT[1][0], r11 = la1_rechannel_wT[1][1];
        const float r20 = la1_rechannel_wT[2][0], r21 = la1_rechannel_wT[2][1];
        const float r30 = la1_rechannel_wT[3][0], r31 = la1_rechannel_wT[3][1];
        for (int n = 0; n < N; ++n)
        {
            const float* const src = la0_buf_b[n];
            la1_buf_a[n][0] = src[0]*r00 + src[1]*r10 + src[2]*r20 + src[3]*r30;
            la1_buf_a[n][1] = src[0]*r01 + src[1]*r11 + src[2]*r21 + src[3]*r31;
        }
    }

    // ── LA1 layer loop ────────────────────────────────────────────────────
    {
        const float (*ping)[2] = la1_buf_a;
        float       (*pong)[2] = la1_buf_b;

        for (int l = 0; l < 13; ++l)
        {
            la1[l].process_block(ping, pong, input, la1_head, N);
            float (*tmp)[2] = pong; pong = (float(*)[2])ping; ping = tmp;
        }
        // 13 swaps (odd) → last output in la1_buf_b
    }
    // la1_head now contains final accumulated head across all LA1 layers

    // ── Output: head → scalar ─────────────────────────────────────────────
    {
        const float w0 = la1_head_w[0], w1 = la1_head_w[1];
        const float b  = la1_head_b;
        const float sc = head_scale;
        for (int n = 0; n < N; ++n)
        {
            output[n] = sc * (b + la1_head[n][0]*w0 + la1_head[n][1]*w1);
        }
    }
}
