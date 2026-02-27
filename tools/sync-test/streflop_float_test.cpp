// STREFLOP Float Comparison Test
// Generates deterministic floating-point results for cross-architecture comparison.
// See STREFLOP_FLOAT_TEST_PROMPT.md for specification.

#include "streflop.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

// --- Configuration macros (set by CMake) ---
#ifndef SFTEST_MODE
#define SFTEST_MODE "UNKNOWN"
#endif
#ifndef SFTEST_COMPILER
#define SFTEST_COMPILER "unknown"
#endif

// --- Architecture detection ---
#if defined(__aarch64__) || defined(__arm64__)
static const char* ARCH_STR = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
static const char* ARCH_STR = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
static const char* ARCH_STR = "x86";
#else
static const char* ARCH_STR = "unknown";
#endif

// --- Deterministic PRNG (xorshift32, independent of streflop) ---
struct XorShift32 {
	uint32_t s;
	explicit XorShift32(uint32_t seed) : s(seed ? seed : 1) {}
	uint32_t next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	// Uniform float in [0, 1)
	float uniform01() {
		return (next() >> 8) * (1.0f / 16777216.0f);
	}
	// Uniform float in [lo, hi)
	float range(float lo, float hi) {
		return lo + uniform01() * (hi - lo);
	}
};

// --- Bit extraction ---
static uint32_t f32bits(float f) {
	uint32_t b;
	memcpy(&b, &f, 4);
	return b;
}

static uint64_t f64bits(double d) {
	uint64_t b;
	memcpy(&b, &d, 8);
	return b;
}

// --- Test result record ---
// Precision: 'F' = float32, 'D' = float64
struct Result {
	uint32_t id;
	char     prec;       // 'F' or 'D'
	char     cat[8];     // category: arith, trans, double, compnd
	char     op[24];     // operation name
	uint64_t input_a;    // first input bits
	uint64_t input_b;    // second input bits (0 if unary)
	uint64_t result;     // raw bits of result
	double   result_val; // float value for display
};

static std::vector<Result> g_results;
static uint32_t g_id = 1;

static void rec_f1(const char* cat, const char* op, float in, float res) {
	Result r{};
	r.id = g_id++;
	r.prec = 'F';
	strncpy(r.cat, cat, 7);
	strncpy(r.op, op, 23);
	r.input_a = f32bits(in);
	r.input_b = 0;
	r.result = f32bits(res);
	r.result_val = res;
	g_results.push_back(r);
}

static void rec_f2(const char* cat, const char* op, float a, float b, float res) {
	Result r{};
	r.id = g_id++;
	r.prec = 'F';
	strncpy(r.cat, cat, 7);
	strncpy(r.op, op, 23);
	r.input_a = f32bits(a);
	r.input_b = f32bits(b);
	r.result = f32bits(res);
	r.result_val = res;
	g_results.push_back(r);
}

static void rec_d1(const char* cat, const char* op, double in, double res) {
	Result r{};
	r.id = g_id++;
	r.prec = 'D';
	strncpy(r.cat, cat, 7);
	strncpy(r.op, op, 23);
	r.input_a = f64bits(in);
	r.input_b = 0;
	r.result = f64bits(res);
	r.result_val = res;
	g_results.push_back(r);
}

static void rec_d2(const char* cat, const char* op, double a, double b, double res) {
	Result r{};
	r.id = g_id++;
	r.prec = 'D';
	strncpy(r.cat, cat, 7);
	strncpy(r.op, op, 23);
	r.input_a = f64bits(a);
	r.input_b = f64bits(b);
	r.result = f64bits(res);
	r.result_val = res;
	g_results.push_back(r);
}

// --- Input generation ---
// Generates N floats spanning normal, edge, and stress-test ranges.
static std::vector<float> generate_inputs(int N) {
	std::vector<float> v;
	v.reserve(N);
	XorShift32 rng(0xDEADBEEF);

	// First: special values (16 values)
	v.push_back(0.0f);
	v.push_back(-0.0f);
	v.push_back(1.0f);
	v.push_back(-1.0f);
	v.push_back(0.5f);
	v.push_back(-0.5f);
	v.push_back(2.0f);
	v.push_back(-2.0f);
	float inf_val;
	uint32_t inf_bits = 0x7F800000;
	memcpy(&inf_val, &inf_bits, 4);
	v.push_back(inf_val);           // +INF
	uint32_t ninf_bits = 0xFF800000;
	float ninf_val;
	memcpy(&ninf_val, &ninf_bits, 4);
	v.push_back(ninf_val);          // -INF
	uint32_t nan_bits = 0x7FC00000;
	float nan_val;
	memcpy(&nan_val, &nan_bits, 4);
	v.push_back(nan_val);           // NaN
	v.push_back(1e-40f);            // denormal
	v.push_back(1e-38f);            // near-zero normal
	v.push_back(1e+38f);            // near-max
	v.push_back(-1e-40f);           // negative denormal
	v.push_back(-1e+38f);           // negative near-max

	// Stress-test values for transcendentals (64 values)
	float pi_f = 3.14159265358979323846f;
	for (int i = 0; i < 16; i++) {
		float t = (float)i / 15.0f;
		v.push_back(t * pi_f);            // 0 to pi
		v.push_back(t * pi_f * 0.5f);     // 0 to pi/2
		v.push_back(t * 2.0f - 1.0f);     // -1 to 1 (for asin/acos domain)
		v.push_back(t * 0.999f);           // near 0 to near 1
	}

	// Random normal-range values (fill remaining)
	while ((int)v.size() < N) {
		float kind = rng.uniform01();
		if (kind < 0.25f) {
			v.push_back(rng.range(-1.0f, 1.0f));        // small
		} else if (kind < 0.50f) {
			v.push_back(rng.range(-1000.0f, 1000.0f));   // medium
		} else if (kind < 0.75f) {
			v.push_back(rng.range(-1e6f, 1e6f));         // large
		} else {
			v.push_back(rng.range(1e-10f, 1e-4f));       // tiny positive
		}
	}

	return v;
}

// --- Category A: Basic arithmetic (uses hardware FPU via Simple type) ---
static void test_arithmetic(const std::vector<float>& inputs) {
	int N = (int)inputs.size();
	// Use consecutive pairs; skip special values at front to avoid INF/NaN in arithmetic
	int start = 16; // skip specials
	for (int i = start; i + 1 < N && i < start + 2000; i += 2) {
		streflop::Simple a(inputs[i]);
		streflop::Simple b(inputs[i + 1]);

		// Avoid division by zero
		streflop::Simple b_safe = (b == streflop::Simple(0.0f)) ? streflop::Simple(1.0f) : b;

		rec_f2("arith", "add", inputs[i], inputs[i + 1], (float)(a + b));
		rec_f2("arith", "sub", inputs[i], inputs[i + 1], (float)(a - b));
		rec_f2("arith", "mul", inputs[i], inputs[i + 1], (float)(a * b));
		rec_f2("arith", "div", inputs[i], (float)b_safe, (float)(a / b_safe));

		// FMA-like: a * b + next value (tests if compiler emits FMA)
		if (i + 2 < N) {
			streflop::Simple c(inputs[i + 2]);
			rec_f2("arith", "muladd", inputs[i], inputs[i + 1], (float)(a * b + c));
		}
	}
}

// --- Category B: Streflop transcendentals (bundled libm) ---
static void test_transcendentals(const std::vector<float>& inputs) {
	int N = (int)inputs.size();

	for (int i = 0; i < N && i < 2000; i++) {
		float fi = inputs[i];
		streflop::Simple x(fi);

		// Skip non-finite inputs for functions that don't handle them well
		float fx = (float)x;
		uint32_t xbits = f32bits(fx);
		bool is_finite = ((xbits & 0x7F800000) != 0x7F800000);
		if (!is_finite) continue;

		// Functions valid for all finite x
		rec_f1("trans", "sqrt_abs", fi, (float)streflop::sqrt(streflop::fabs(x)));
		rec_f1("trans", "fabs",     fi, (float)streflop::fabs(x));
		rec_f1("trans", "floor",    fi, (float)streflop::floor(x));
		rec_f1("trans", "ceil",     fi, (float)streflop::ceil(x));
		rec_f1("trans", "round",    fi, (float)streflop::round(x));
		rec_f1("trans", "trunc",    fi, (float)streflop::trunc(x));
		rec_f1("trans", "sin",      fi, (float)streflop::sin(x));
		rec_f1("trans", "cos",      fi, (float)streflop::cos(x));
		rec_f1("trans", "tan",      fi, (float)streflop::tan(x));
		rec_f1("trans", "sinh",     fi, (float)streflop::sinh(x));
		rec_f1("trans", "cosh",     fi, (float)streflop::cosh(x));
		rec_f1("trans", "tanh",     fi, (float)streflop::tanh(x));

		// exp/log — need positive or specific domain
		if (fx > -80.0f && fx < 80.0f) {
			rec_f1("trans", "exp", fi, (float)streflop::exp(x));
		}
		if (fx > 0.0f) {
			rec_f1("trans", "log",   fi, (float)streflop::log(x));
			rec_f1("trans", "log2",  fi, (float)streflop::log2(x));
			rec_f1("trans", "log10", fi, (float)streflop::log10(x));
			rec_f1("trans", "sqrt",  fi, (float)streflop::sqrt(x));
			rec_f1("trans", "cbrt",  fi, (float)streflop::cbrt(x));
		}

		// asin/acos — domain [-1, 1]
		if (fx >= -1.0f && fx <= 1.0f) {
			rec_f1("trans", "asin", fi, (float)streflop::asin(x));
			rec_f1("trans", "acos", fi, (float)streflop::acos(x));
		}

		// atan — full domain
		rec_f1("trans", "atan", fi, (float)streflop::atan(x));

		// atan2, pow, fmod — use next value as second argument
		if (i + 1 < N) {
			float fi2 = inputs[i + 1];
			streflop::Simple y(fi2);
			float fy = (float)y;
			uint32_t ybits = f32bits(fy);
			bool y_finite = ((ybits & 0x7F800000) != 0x7F800000);
			if (y_finite) {
				rec_f2("trans", "atan2", fi, fi2, (float)streflop::atan2(x, y));
				if (fx > 0.0f && fy > -20.0f && fy < 20.0f) {
					rec_f2("trans", "pow", fi, fi2, (float)streflop::pow(x, y));
				}
				if (fy != 0.0f) {
					rec_f2("trans", "fmod", fi, fi2, (float)streflop::fmod(x, y));
				}
			}
		}
	}
}

// --- Category C: Double precision (arithmetic only) ---
// NOTE: streflop's bundled libm only has flt-32 (float) implementations.
// Double-precision transcendentals (sin, cos, etc.) are declared in SMath.h
// but have no implementation. Only double arithmetic is testable.
static void test_double_precision(const std::vector<float>& inputs) {
	int N = (int)inputs.size();
	int start = 16;

	for (int i = start; i + 1 < N && i < start + 1000; i += 2) {
		double da = (double)inputs[i];
		double db = (double)inputs[i + 1];
		streflop::Double a(da);
		streflop::Double b(db);

		streflop::Double b_safe = (b == streflop::Double(0.0)) ? streflop::Double(1.0) : b;

		rec_d2("double", "add", da, db, (double)(a + b));
		rec_d2("double", "sub", da, db, (double)(a - b));
		rec_d2("double", "mul", da, db, (double)(a * b));
		rec_d2("double", "div", da, (double)b_safe, (double)(a / b_safe));

		// FMA-like: a * b + c
		if (i + 2 < N) {
			streflop::Double c((double)inputs[i + 2]);
			rec_d2("double", "muladd", da, db, (double)(a * b + c));
		}
	}
}

// --- Category D: Compound operations (simulate engine patterns) ---
static void test_compound(const std::vector<float>& inputs) {
	int N = (int)inputs.size();
	int start = 80; // past specials and trig stress values

	// Normalize float3: x/sqrt(x*x + y*y + z*z)
	for (int i = start; i + 2 < N && i < start + 300; i += 3) {
		streflop::Simple x(inputs[i]);
		streflop::Simple y(inputs[i + 1]);
		streflop::Simple z(inputs[i + 2]);
		streflop::Simple len2 = x * x + y * y + z * z;
		if ((float)len2 > 0.0f) {
			streflop::Simple len = streflop::sqrt(len2);
			streflop::Simple nx = x / len;
			rec_f1("compnd", "norm_x", inputs[i], (float)nx);
		}
	}

	// Dot product: a.x*b.x + a.y*b.y + a.z*b.z
	for (int i = start; i + 5 < N && i < start + 300; i += 6) {
		streflop::Simple ax(inputs[i]),   ay(inputs[i + 1]), az(inputs[i + 2]);
		streflop::Simple bx(inputs[i + 3]), by(inputs[i + 4]), bz(inputs[i + 5]);
		streflop::Simple dot = ax * bx + ay * by + az * bz;
		rec_f2("compnd", "dot3", inputs[i], inputs[i + 3], (float)dot);
	}

	// Linear interpolation: a + t*(b-a), t in [0,1]
	for (int i = start; i + 1 < N && i < start + 200; i += 2) {
		streflop::Simple a(inputs[i]);
		streflop::Simple b(inputs[i + 1]);
		streflop::Simple t(0.3f);
		streflop::Simple lerp_val = a + t * (b - a);
		rec_f2("compnd", "lerp", inputs[i], inputs[i + 1], (float)lerp_val);
	}

	// Distance: sqrt((x1-x2)^2 + (y1-y2)^2 + (z1-z2)^2)
	for (int i = start; i + 5 < N && i < start + 300; i += 6) {
		streflop::Simple x1(inputs[i]),   y1(inputs[i + 1]), z1(inputs[i + 2]);
		streflop::Simple x2(inputs[i + 3]), y2(inputs[i + 4]), z2(inputs[i + 5]);
		streflop::Simple dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
		streflop::Simple dist = streflop::sqrt(dx * dx + dy * dy + dz * dz);
		rec_f2("compnd", "dist3", inputs[i], inputs[i + 3], (float)dist);
	}

	// Accumulated sum of 1000 small values (tests rounding accumulation)
	{
		streflop::Simple acc(0.0f);
		XorShift32 rng(12345);
		for (int i = 0; i < 1000; i++) {
			float small = rng.range(1e-6f, 1e-3f);
			acc += streflop::Simple(small);
		}
		rec_f1("compnd", "accum1000", 0.0f, (float)acc);
	}

	// Accumulated sum with alternating signs (catastrophic cancellation test)
	{
		streflop::Simple acc(0.0f);
		XorShift32 rng(54321);
		for (int i = 0; i < 1000; i++) {
			float small = rng.range(1e-4f, 1e-2f);
			if (i & 1) small = -small;
			acc += streflop::Simple(small);
		}
		rec_f1("compnd", "cancel1000", 0.0f, (float)acc);
	}
}

// --- Output: binary file ---
static bool write_binary(const char* path) {
	FILE* f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "ERROR: Cannot open %s for writing\n", path);
		return false;
	}

	// Header
	fwrite("SFLT", 1, 4, f);
	uint32_t version = 1;
	fwrite(&version, 4, 1, f);

	const char* mode = SFTEST_MODE;
	fwrite(mode, 1, strlen(mode) + 1, f);
	fwrite(ARCH_STR, 1, strlen(ARCH_STR) + 1, f);

	uint32_t count = (uint32_t)g_results.size();
	fwrite(&count, 4, 1, f);

	// Records: id(4) + prec(1) + result_bits(8) = 13 bytes each
	for (auto& r : g_results) {
		fwrite(&r.id, 4, 1, f);
		fwrite(&r.prec, 1, 1, f);
		fwrite(&r.result, 8, 1, f);
	}

	fclose(f);
	return true;
}

// --- Output: text file ---
static bool write_text(const char* path) {
	FILE* f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "ERROR: Cannot open %s for writing\n", path);
		return false;
	}

	time_t now = time(nullptr);
	struct tm* t = localtime(&now);
	char datebuf[32];
	strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", t);

	fprintf(f, "# STREFLOP Float Test Results\n");
	fprintf(f, "# Mode:     %s\n", SFTEST_MODE);
	fprintf(f, "# Arch:     %s\n", ARCH_STR);
	fprintf(f, "# Compiler: %s\n", SFTEST_COMPILER);
	fprintf(f, "# Date:     %s\n", datebuf);
	fprintf(f, "# Total:    %u tests\n", (uint32_t)g_results.size());
	fprintf(f, "# FP-contract: off\n");
	fprintf(f, "#\n");
	fprintf(f, "# %-8s %-6s %-6s %-14s %-18s %-18s %s\n",
	        "TestID", "Prec", "Cat", "Operation", "InputA(hex)", "Result(hex)", "Result(value)");

	for (auto& r : g_results) {
		if (r.prec == 'F') {
			fprintf(f, "  %-8u %-6c %-6s %-14s %08X         %08X           %.9g\n",
			        r.id, r.prec, r.cat, r.op,
			        (uint32_t)r.input_a,
			        (uint32_t)r.result,
			        r.result_val);
		} else {
			fprintf(f, "  %-8u %-6c %-6s %-14s %016llX %016llX %.17g\n",
			        r.id, r.prec, r.cat, r.op,
			        (unsigned long long)r.input_a,
			        (unsigned long long)r.result,
			        r.result_val);
		}
	}

	fclose(f);
	return true;
}

// --- Main ---
int main(int argc, char* argv[]) {
	printf("STREFLOP Float Comparison Test\n");
	printf("  Mode:     %s\n", SFTEST_MODE);
	printf("  Arch:     %s\n", ARCH_STR);
	printf("  Compiler: %s\n", SFTEST_COMPILER);
	printf("  FP-contract: off (set by CMake)\n");
	printf("\n");

	// Initialize streflop FPU state
	printf("Initializing streflop (%s)...\n", SFTEST_MODE);
	streflop::streflop_init<streflop::Simple>();
	printf("  Simple init OK\n");
	streflop::streflop_init<streflop::Double>();
	printf("  Double init OK\n");

	// Generate inputs
	const int NUM_INPUTS = 10000;
	printf("Generating %d deterministic input values...\n", NUM_INPUTS);
	auto inputs = generate_inputs(NUM_INPUTS);

	// Run test categories
	printf("Running Category A: Basic arithmetic...\n");
	size_t before = g_results.size();
	test_arithmetic(inputs);
	printf("  %zu tests\n", g_results.size() - before);

	printf("Running Category B: Transcendentals...\n");
	before = g_results.size();
	test_transcendentals(inputs);
	printf("  %zu tests\n", g_results.size() - before);

	printf("Running Category C: Double precision...\n");
	before = g_results.size();
	test_double_precision(inputs);
	printf("  %zu tests\n", g_results.size() - before);

	printf("Running Category D: Compound operations...\n");
	before = g_results.size();
	test_compound(inputs);
	printf("  %zu tests\n", g_results.size() - before);

	printf("\nTotal: %zu tests\n\n", g_results.size());

	// Determine output prefix
	char prefix[256];
	if (argc > 1) {
		snprintf(prefix, sizeof(prefix), "%s", argv[1]);
	} else {
		// Strip "STREFLOP_" prefix for filename
		const char* mode_short = SFTEST_MODE;
		if (strncmp(mode_short, "STREFLOP_", 9) == 0)
			mode_short += 9;
		snprintf(prefix, sizeof(prefix), "streflop_results_%s_%s", mode_short, ARCH_STR);
	}

	// Write outputs
	char bin_path[512], txt_path[512];
	snprintf(bin_path, sizeof(bin_path), "%s.bin", prefix);
	snprintf(txt_path, sizeof(txt_path), "%s.txt", prefix);

	printf("Writing binary: %s\n", bin_path);
	if (!write_binary(bin_path)) return 1;

	printf("Writing text:   %s\n", txt_path);
	if (!write_text(txt_path)) return 1;

	printf("\nDone. Compare with:\n");
	printf("  python3 compare_results.py <reference>.bin %s\n", bin_path);

	return 0;
}
