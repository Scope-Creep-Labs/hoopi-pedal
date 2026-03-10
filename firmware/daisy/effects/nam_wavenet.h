/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// nam_wavenet.h  —  Block-processing WaveNet, STM32H750 Cortex-M7
//
// Usage:
//   static NamWavenet model DSY_RAM_D2;
//   model.load_weights(weights_array);
//   // In audio callback — N must equal NAM_BLOCK exactly:
//   model.forward(input_buf, output_buf);
//
// Key architectural changes vs single-sample version:
//
//  1. BLOCK PROCESSING (NAM_BLOCK=48 samples)
//     Loop order: layer-outer, sample-inner.
//     Each layer's conv is a proper GEMM: W[ch×ch] hoisted into registers,
//     reused across all 48 columns.  Weight loads amortised 48×.
//     Cost: ~3.5 KB working buffers (la0_buf_a/b, la0_head, la1_buf_a/b, la1_head).
//
//  2. UNROLLED GEMM KERNELS (4×4 and 2×2, compile-time specialised)
//     Weights declared as local const → GCC register-allocates.
//     Confirmed 3.10× vs Eigen on same Daisy hardware (jfsantos.dev benchmarks).
//
//  3. PLAIN '/' FOR PADÉ TANH — single VDIV, no FP↔INT domain crossing.
//     4 independent VDIVs pipeline: throughput=7 → 28 cycles for 4 results.
//
//  4. TRANSPOSED WEIGHT LAYOUT [in][out] — 4 independent accumulators.
//
//  5. s0 == ins — tap0 = current input, no state read needed.
//
//  6. NO std::memcpy for audio data — plain assignment loops.
//     Newlib memcpy is 4.3× slower than register stores on Daisy.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int NAM_BLOCK = 48;  // must match Daisy callback buffer size

struct NamWavenet
{
    // ── 4-channel layer ───────────────────────────────────────────────────
    struct Layer4
    {
        float w0T[4][4];    // tap0 (= current input), [in][out]
        float w1T[4][4];    // tap1 = state[ptr-D],   [in][out]
        float w2T[4][4];    // tap2 = state[ptr-2D],  [in][out]
        float conv_b[4];
        float mixin_w[4];
        float w1x1T[4][4];  // 1×1 projection, [in][out]
        float b1x1[4];

        float* state;
        int    state_size;
        int    state_ptr;
        int    neg_dilation;   // = state_size - dilation
        int    neg_2dilation;  // = state_size - 2*dilation

        // Process N frames: ins[N][4] → outs[N][4], accumulate head[N][4].
        // cond[N] = raw input samples (per-sample conditioning).
        void process_block(const float ins[][4], float outs[][4],
                           const float* cond, float head[][4],
                           int N) noexcept;
    };

    // ── 2-channel layer ───────────────────────────────────────────────────
    struct Layer2
    {
        float w0T[2][2];
        float w1T[2][2];
        float w2T[2][2];
        float conv_b[2];
        float mixin_w[2];
        float w1x1T[2][2];
        float b1x1[2];

        float* state;
        int    state_size;
        int    state_ptr;
        int    neg_dilation;
        int    neg_2dilation;

        void process_block(const float ins[][2], float outs[][2],
                           const float* cond, float head[][2],
                           int N) noexcept;
    };

    // ── Layer Array 0: 7 × 4-channel ──────────────────────────────────────
    float  la0_rechannel_w[4];    // 1→4 input projection
    float  la0_head_wT[4][2];     // 4→2 head projection, [in][out]
    Layer4 la0[7];
    float  la0_state[1044];       // sum((2d+1)*4) for d in {1,2,4,8,16,32,64}

    // ── Layer Array 1: 13 × 2-channel ─────────────────────────────────────
    float  la1_rechannel_wT[4][2];  // 4→2 rechannel, [in][out]
    float  la1_head_w[2];
    float  la1_head_b;
    Layer2 la1[13];
    float  la1_state[7702];  // sum((2d+1)*2) for d in {128,256,512,1,2,4,8,16,32,64,128,256,512}

    float  head_scale;

    // ── Block working buffers ─────────────────────────────────────────────
    // Ping-pong layer I/O + head accumulators. Re-used every call.
    // Total: (48*4 + 48*4 + 48*4)*4 + (48*2 + 48*2 + 48*2)*4 = 768*3 + 384*3 = 3456 bytes
    float la0_buf_a[NAM_BLOCK][4];   // ping: LA0 layer input
    float la0_buf_b[NAM_BLOCK][4];   // pong: LA0 layer output
    float la0_head [NAM_BLOCK][4];   // LA0 skip-connection accumulator

    float la1_buf_a[NAM_BLOCK][2];
    float la1_buf_b[NAM_BLOCK][2];
    float la1_head [NAM_BLOCK][2];

    // ── API ───────────────────────────────────────────────────────────────
    void reset();
    void load_weights(const float* model_weights);

    // Process exactly NAM_BLOCK samples. input/output must be NAM_BLOCK floats.
    void forward(const float* input, float* output) noexcept;

private:
    void init_state_ptrs();
};
