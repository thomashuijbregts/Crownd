#pragma once

#include "Definitions.h"

namespace simd {
#if HAS_AVX512
    using vec = __m512i;
    using vec_h = __m256i;
    using vec_q = __m128i;
    using vec_f = __m512;

    ForceInline vec setzero() { return _mm512_setzero_si512(); }
    ForceInline vec set1_epi8(int x) { return _mm512_set1_epi8(x); }
    ForceInline vec set1_epi16(int16_t x) { return _mm512_set1_epi16(x); }
    ForceInline vec set1_epi32(int32_t x) { return _mm512_set1_epi32(x); }
    ForceInline vec_f set1_ps(float x) { return _mm512_set1_ps(x); }

    ForceInline vec  load(const void* p) { return _mm512_load_si512(p); }
    ForceInline void store(void* p, vec v) { _mm512_store_si512(p, v); }
    ForceInline vec_h load_h(const void* p) { return _mm256_load_si256((const __m256i*)p); }
    ForceInline void store_h(void* p, vec_h v) { _mm256_store_si256((__m256i*)p, v); }
    ForceInline void store_q(void* p, vec_q v) { _mm_store_si128((__m128i*)p, v); }

    ForceInline vec add_epi16(vec a, vec b) { return _mm512_add_epi16(a, b); }
    ForceInline vec sub_epi16(vec a, vec b) { return _mm512_sub_epi16(a, b); }
    ForceInline vec min_epi16(vec a, vec b) { return _mm512_min_epi16(a, b); }
    ForceInline vec max_epi16(vec a, vec b) { return _mm512_max_epi16(a, b); }
    ForceInline vec mulhrs_epi16(vec a, vec b) { return _mm512_mulhrs_epi16(a, b); }
    ForceInline vec slli_epi16(vec a, int n) { return _mm512_slli_epi16(a, n); }

    ForceInline vec add_epi32(vec a, vec b) { return _mm512_add_epi32(a, b); }
    ForceInline vec min_epi32(vec a, vec b) { return _mm512_min_epi32(a, b); }
    ForceInline vec max_epi32(vec a, vec b) { return _mm512_max_epi32(a, b); }
    ForceInline vec srli_epi32(vec a, int n) { return _mm512_srli_epi32(a, n); }
    ForceInline int reduce_add_epi32(vec v) { return _mm512_reduce_add_epi32(v); }

    ForceInline vec_f cvtepi32_ps(vec v) { return _mm512_cvtepi32_ps(v); }
    ForceInline vec   cvtps_epi32(vec_f v) { return _mm512_cvtps_epi32(v); }
    ForceInline vec_f mul_ps(vec_f a, vec_f b) { return _mm512_mul_ps(a, b); }

    ForceInline vec   cvtepi8_epi16(vec_h v) { return _mm512_cvtepi8_epi16(v); }
    ForceInline vec_h cvtepi16_epi8(vec v) { return _mm512_cvtepi16_epi8(v); }
    ForceInline vec_q cvtepi32_epi8(vec v) { return _mm512_cvtepi32_epi8(v); }
    ForceInline vec   zext(vec_h v) { return _mm512_zextsi256_si512(v); }

    ForceInline vec mask_permutexvar_epi8(vec src, U64 m, vec idx, vec v) { return _mm512_mask_permutexvar_epi8(src, m, idx, v); }
    ForceInline vec shuffle_epi8(vec a, vec b) { return _mm512_shuffle_epi8(a, b); }
    ForceInline U64 test_epi8_mask(vec a, vec b) { return _mm512_test_epi8_mask(a, b); }
    ForceInline vec mask_mov_epi8(vec src, U64 m, vec a) { return _mm512_mask_mov_epi8(src, m, a); }

    ForceInline vec dpbusd(vec acc, vec a, vec b) {
#if defined(__AVX512VNNI__)
        return _mm512_dpbusd_epi32(acc, a, b);
#else
        vec i16 = _mm512_maddubs_epi16(a, b);
        vec one = _mm512_set1_epi16(1);
        return _mm512_add_epi32(acc, _mm512_madd_epi16(i16, one));
#endif
    }
#else
    using vec = __m256i;
    using vec_h = __m128i;
    using vec_q = __m128i;
    using vec_f = __m256;

    ForceInline vec setzero() { return _mm256_setzero_si256(); }
    ForceInline vec set1_epi8(int x) { return _mm256_set1_epi8(x); }
    ForceInline vec set1_epi16(int16_t x) { return _mm256_set1_epi16(x); }
    ForceInline vec set1_epi32(int32_t x) { return _mm256_set1_epi32(x); }
    ForceInline vec_f set1_ps(float x) { return _mm256_set1_ps(x); }

    ForceInline vec load(const void* p) { return _mm256_load_si256((const __m256i*)p); }
    ForceInline void store(void* p, vec v) { _mm256_store_si256((__m256i*)p, v); }
    ForceInline vec_h load_h(const void* p) { return _mm_load_si128((const __m128i*)p); }
    ForceInline void store_h(void* p, vec_h v) { _mm_store_si128((__m128i*)p, v); }
    ForceInline void store_q(void* p, vec_q v) { _mm_storel_epi64((__m128i*)p, v); }

    ForceInline vec add_epi16(vec a, vec b) { return _mm256_add_epi16(a, b); }
    ForceInline vec sub_epi16(vec a, vec b) { return _mm256_sub_epi16(a, b); }
    ForceInline vec min_epi16(vec a, vec b) { return _mm256_min_epi16(a, b); }
    ForceInline vec max_epi16(vec a, vec b) { return _mm256_max_epi16(a, b); }
    ForceInline vec mulhrs_epi16(vec a, vec b) { return _mm256_mulhrs_epi16(a, b); }
    ForceInline vec slli_epi16(vec a, int n) { return _mm256_slli_epi16(a, n); }

    ForceInline vec add_epi32(vec a, vec b) { return _mm256_add_epi32(a, b); }
    ForceInline vec min_epi32(vec a, vec b) { return _mm256_min_epi32(a, b); }
    ForceInline vec max_epi32(vec a, vec b) { return _mm256_max_epi32(a, b); }
    ForceInline vec srli_epi32(vec a, int n) { return _mm256_srli_epi32(a, n); }

    ForceInline int reduce_add_epi32(vec v) {
        vec_h s = _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
        vec_h hi64 = _mm_unpackhi_epi64(s, s);
        vec_h sum64 = _mm_add_epi32(hi64, s);
        vec_h hi32 = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
        vec_h sum32 = _mm_add_epi32(sum64, hi32);
        return _mm_cvtsi128_si32(sum32);
    }

    ForceInline vec_f cvtepi32_ps(vec v) { return _mm256_cvtepi32_ps(v); }
    ForceInline vec   cvtps_epi32(vec_f v) { return _mm256_cvtps_epi32(v); }
    ForceInline vec_f mul_ps(vec_f a, vec_f b) { return _mm256_mul_ps(a, b); }

    ForceInline vec cvtepi8_epi16(vec_h v) { return _mm256_cvtepi8_epi16(v); }

    ForceInline vec_h cvtepi16_epi8(vec v) {
        return _mm_packus_epi16(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
    }

    ForceInline vec_q cvtepi32_epi8(vec v) {
        const vec_h p16 = _mm_packs_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
        return _mm_packus_epi16(p16, p16);
    }

    ForceInline vec dpbusd(vec acc, vec a, vec b) {
#if defined(__AVXVNNI__)
        return _mm256_dpbusd_avx_epi32(acc, a, b);
#else
        vec i16 = _mm256_maddubs_epi16(a, b);
        vec one = _mm256_set1_epi16(1);
        return _mm256_add_epi32(acc, _mm256_madd_epi16(i16, one));
#endif
    }
#endif
}