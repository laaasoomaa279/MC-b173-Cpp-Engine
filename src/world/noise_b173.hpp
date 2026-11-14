#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

// ============================================================================
// CANONICAL PROCEDURAL PSEUDORANDOM & MULTI-OCTAVE NOISE ENGINE (SPEC-2011)
// ============================================================================
// Technical Subsystem Architecture:
// 1. 48-Bit Linear Congruential Generator (LCG):
//    - State Transition Equation:
//        X_{n+1} = (X_n * MULTIPLIER + ADDEND) mod MASK
//      where:
//        MULTIPLIER = 0x5DEECE66D (25214903917)
//        ADDEND     = 0xB (11)
//        MASK       = 2^48 - 1 (0xFFFFFFFFFFFF)
//    - Polar Box-Muller Transformation for Gaussian distribution:
//        s = v1^2 + v2^2, where v1, v2 in [-1, 1], rejecting s >= 1 or s == 0
//        multiplier = sqrt(-2.0 * ln(s) / s)
//        Z0 = v1 * multiplier, Z1 = v2 * multiplier
// 2. 2D Simplex Thermodynamic Climate Noise Generator:
//    - Coordinate Skew Transform:
//        s = (x + y) * F2, where F2 = (sqrt(3) - 1) / 2 ~= 0.3660254037844386
//    - Coordinate Unskew Transform:
//        t = (i + j) * G2, where G2 = (3 - sqrt(3)) / 6 ~= 0.21132486540518713
//    - Kernel Falloff & Radial Contribution:
//        t_k = 0.5 - x_k^2 - y_k^2; if t_k >= 0: n_k = t_k^4 * (grad_k . (x_k, y_k))
//        Total Amplitude = 70.0 * sum(n_k)
// 3. 3D/2D Improved Gradient Noise & Integer Overflow Singularities (Far Lands):
//    - Quintic Hermite Interpolation Curve:
//        fade(t) = t^3 * (t * (t * 6.0 - 15.0) + 10.0)
//    - JLS (Java Language Specification) 32-bit Signed Integer Conversion:
//        Values are cast to int32 via saturation clamping to [-2^31, 2^31 - 1].
//    - Geometric Discontinuity Horizon:
//        When |X| or |Z| >= (2^31 - 1) / (684.412 / 4) ~= 12,550,824 blocks,
//        the integer grid coordinate overflows, causing the noise field to
//        saturate and manifest as massive vertical cliff walls extending to Y=128.
// ============================================================================

class JavaRandomB173 {
    static constexpr uint64_t MULT = 0x5DEECE66DULL;
    static constexpr uint64_t ADD  = 0xBULL;
    static constexpr uint64_t MASK = (1ULL << 48) - 1ULL;
    uint64_t seed = 0;
    double   nextNextGaussian = 0.0;
    bool     haveNextNextGaussian = false;

public:
    JavaRandomB173() { setSeed(0); }
    explicit JavaRandomB173(int64_t s) { setSeed(s); }

    void setSeed(int64_t s) {
        seed = (static_cast<uint64_t>(s) ^ MULT) & MASK;
        haveNextNextGaussian = false;
    }

    uint32_t next(int bits) {
        seed = (seed * MULT + ADD) & MASK;
        return static_cast<uint32_t>(seed >> (48 - bits));
    }

    int32_t nextInt() { return static_cast<int32_t>(next(32)); }

    int32_t nextInt(int32_t bound) {
        if (bound <= 0) return 0;
        if ((bound & -bound) == bound) {
            return static_cast<int32_t>((static_cast<int64_t>(bound) * static_cast<int64_t>(next(31))) >> 31);
        }
        int32_t bits, val;
        do {
            bits = static_cast<int32_t>(next(31));
            val  = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
    }

    int64_t nextLong() {
        return (static_cast<int64_t>(static_cast<int32_t>(next(32))) << 32) +
                static_cast<int64_t>(static_cast<int32_t>(next(32)));
    }

    bool nextBoolean() { return next(1) != 0; }

    float nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

    double nextDouble() {
        uint64_t hi = static_cast<uint64_t>(next(26)) << 27;
        uint64_t lo = static_cast<uint64_t>(next(27));
        return static_cast<double>(hi + lo) / static_cast<double>(1ULL << 53);
    }

    double nextGaussian() {
        if (haveNextNextGaussian) {
            haveNextNextGaussian = false;
            return nextNextGaussian;
        }
        double v1, v2, s;
        do {
            v1 = 2.0 * nextDouble() - 1.0;
            v2 = 2.0 * nextDouble() - 1.0;
            s  = v1 * v1 + v2 * v2;
        } while (s >= 1.0 || s == 0.0);
        double multiplier = std::sqrt(-2.0 * std::log(s) / s);
        nextNextGaussian     = v2 * multiplier;
        haveNextNextGaussian = true;
        return v1 * multiplier;
    }
};

class SimplexNoise2DB173 {
    int    p[512]{};
    double xo = 0.0, yo = 0.0, zo = 0.0;

    static constexpr double F2 = 0.3660254037844386;  // (sqrt(3) - 1) / 2
    static constexpr double G2 = 0.21132486540518713; // (3 - sqrt(3)) / 6

    static inline int fastFloor(double x) {
        return (x > 0.0) ? static_cast<int>(x) : (static_cast<int>(x) - 1);
    }

public:
    void init(JavaRandomB173& rand) {
        xo = rand.nextDouble() * 256.0;
        yo = rand.nextDouble() * 256.0;
        zo = rand.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) p[i] = i;
        for (int i = 0; i < 256; ++i) {
            int j = rand.nextInt(256 - i) + i;
            std::swap(p[i], p[j]);
            p[i + 256] = p[i];
        }
    }

    double sample(double x, double y, double amp, double freq) const {
        static constexpr int grad3[12][2] = {
            {1,1},{-1,1},{1,-1},{-1,-1},
            {1,0},{-1,0},{1,0},{-1,0},
            {0,1},{0,-1},{0,1},{0,-1}
        };

        double xIn = x * freq + xo;
        double yIn = y * freq + yo;
        double s   = (xIn + yIn) * F2;
        int i = fastFloor(xIn + s);
        int j = fastFloor(yIn + s);
        double t  = static_cast<double>(i + j) * G2;
        double X0 = static_cast<double>(i) - t;
        double Y0 = static_cast<double>(j) - t;
        double x0 = xIn - X0;
        double y0 = yIn - Y0;

        int i1 = (x0 > y0) ? 1 : 0;
        int j1 = (x0 > y0) ? 0 : 1;

        double x1 = x0 - static_cast<double>(i1) + G2;
        double y1 = y0 - static_cast<double>(j1) + G2;
        double x2 = x0 - 1.0 + 2.0 * G2;
        double y2 = y0 - 1.0 + 2.0 * G2;

        int ii  = i & 255;
        int jj  = j & 255;
        int gi0 = p[ii + p[jj]] % 12;
        int gi1 = p[ii + i1 + p[jj + j1]] % 12;
        int gi2 = p[ii + 1 + p[jj + 1]] % 12;

        double n0 = 0.0, n1 = 0.0, n2 = 0.0;
        double t0 = 0.5 - x0 * x0 - y0 * y0;
        if (t0 >= 0.0) {
            t0 *= t0;
            n0 = t0 * t0 * (grad3[gi0][0] * x0 + grad3[gi0][1] * y0);
        }
        double t1 = 0.5 - x1 * x1 - y1 * y1;
        if (t1 >= 0.0) {
            t1 *= t1;
            n1 = t1 * t1 * (grad3[gi1][0] * x1 + grad3[gi1][1] * y1);
        }
        double t2 = 0.5 - x2 * x2 - y2 * y2;
        if (t2 >= 0.0) {
            t2 *= t2;
            n2 = t2 * t2 * (grad3[gi2][0] * x2 + grad3[gi2][1] * y2);
        }

        return 70.0 * (n0 + n1 + n2) * amp;
    }
};

class OctaveSimplex2DB173 {
    std::vector<SimplexNoise2DB173> octaves;

public:
    void init(JavaRandomB173& rand, int count) {
        octaves.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) octaves[i].init(rand);
    }

    double sample(double x, double z, double baseFreq, double lacunarity, double persistence) const {
        double total = 0.0;
        double freq  = 1.0;
        double amp   = 1.0;
        for (const auto& oct : octaves) {
            total += oct.sample(x, z, amp, baseFreq * freq);
            freq  *= lacunarity;
            amp   /= persistence;
        }
        return total;
    }
};

class PerlinNoiseB173 {
    int    p[512]{};
    double xo = 0.0, yo = 0.0, zo = 0.0;

    static inline double fade(double t) {
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }
    static inline double lerp(double t, double a, double b) {
        return a + t * (b - a);
    }
    static inline double grad(int h, double x, double y, double z) {
        h &= 15;
        double u = (h < 8) ? x : y;
        double v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
    static inline double grad2D(int h, double x, double z) {
        h &= 15;
        double u = (h < 8) ? x : 0.0;
        double v = (h < 4) ? 0.0 : ((h == 12 || h == 14) ? x : z);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    static inline int32_t javaDoubleToInt(double d) {
        if (d >= 2147483647.0)  return 2147483647;
        if (d <= -2147483648.0) return -2147483648;
        return static_cast<int32_t>(d);
    }

public:
    void init(JavaRandomB173& rand) {
        xo = rand.nextDouble() * 256.0;
        yo = rand.nextDouble() * 256.0;
        zo = rand.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) p[i] = i;
        for (int i = 0; i < 256; ++i) {
            int j = rand.nextInt(256 - i) + i;
            std::swap(p[i], p[j]);
            p[i + 256] = p[i];
        }
    }

    double sample3D(double x, double y, double z) const {
        double d0 = x + xo;
        double d1 = y + yo;
        double d2 = z + zo;

        int32_t i = javaDoubleToInt(d0);
        int32_t j = javaDoubleToInt(d1);
        int32_t k = javaDoubleToInt(d2);
        if (d0 < static_cast<double>(i)) --i;
        if (d1 < static_cast<double>(j)) --j;
        if (d2 < static_cast<double>(k)) --k;

        int X = i & 255;
        int Y = j & 255;
        int Z = k & 255;

        double px = d0 - static_cast<double>(i);
        double py = d1 - static_cast<double>(j);
        double pz = d2 - static_cast<double>(k);

        double u = fade(px);
        double v = fade(py);
        double w = fade(pz);

        int A  = p[X] + Y;
        int AA = p[A] + Z;
        int AB = p[A + 1] + Z;
        int B  = p[X + 1] + Y;
        int BA = p[B] + Z;
        int BB = p[B + 1] + Z;

        return lerp(w,
            lerp(v,
                lerp(u, grad(p[AA], px, py, pz), grad(p[BA], px - 1.0, py, pz)),
                lerp(u, grad(p[AB], px, py - 1.0, pz), grad(p[BB], px - 1.0, py - 1.0, pz))),
            lerp(v,
                lerp(u, grad(p[AA + 1], px, py, pz - 1.0), grad(p[BA + 1], px - 1.0, py, pz - 1.0)),
                lerp(u, grad(p[AB + 1], px, py - 1.0, pz - 1.0), grad(p[BB + 1], px - 1.0, py - 1.0, pz - 1.0))));
    }

    double sample2D(double x, double z) const {
        double d0 = x + xo;
        double d2 = z + zo;

        int32_t i = javaDoubleToInt(d0);
        int32_t k = javaDoubleToInt(d2);
        if (d0 < static_cast<double>(i)) --i;
        if (d2 < static_cast<double>(k)) --k;

        int X = i & 255;
        int Z = k & 255;

        double px = d0 - static_cast<double>(i);
        double pz = d2 - static_cast<double>(k);

        double u = fade(px);
        double w = fade(pz);

        int A  = p[X] + 0;
        int AA = p[A] + Z;
        int B  = p[X + 1] + 0;
        int BA = p[B] + Z;

        return lerp(w,
            lerp(u, grad2D(p[AA], px, pz), grad2D(p[BA], px - 1.0, pz)),
            lerp(u, grad2D(p[AA + 1], px, pz - 1.0), grad2D(p[BA + 1], px - 1.0, pz - 1.0)));
    }
};

class OctaveNoiseB173 {
    std::vector<PerlinNoiseB173> octaves;

public:
    void init(JavaRandomB173& rand, int count) {
        octaves.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) octaves[i].init(rand);
    }

    double sample3D(double x, double y, double z, double fx, double fy, double fz) const {
        double total = 0.0, amp = 1.0;
        for (const auto& o : octaves) {
            total += o.sample3D(x * fx * amp, y * fy * amp, z * fz * amp) / amp;
            amp *= 0.5;
        }
        return total;
    }

    double sample2D(double x, double z, double fx, double fz) const {
        double total = 0.0, amp = 1.0;
        for (const auto& o : octaves) {
            total += o.sample2D(x * fx * amp, z * fz * amp) / amp;
            amp *= 0.5;
        }
        return total;
    }
};