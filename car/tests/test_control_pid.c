#include "control_pid.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)
#define NEAR(value, expected) do { float actual = (value); \
    CHECK(isfinite(actual) && fabsf(actual - (expected)) < 0.0001f); \
} while (0)

/* 期望值由常量轨迹手工推导，直接运行真实模块而不仿制控制算法。 */
static control_pid_config_t config_default(void)
{
    const control_pid_config_t config = {2.0f, 0.0f, 0.0f,
                                        -5.0f, 7.0f, 4.0f, 0.0f};
    return config;
}

static int test_proportional(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 3.0f, 1.0f, 0.5f, 0.01f), 4.5f);
    NEAR(control_pid_step(&pid, 10.0f, 0.0f, 0.0f, 0.02f), 7.0f);
    NEAR(control_pid_step(&pid, -10.0f, 0.0f, 0.0f, 0.02f), -5.0f);
    NEAR(control_pid_step(&pid, 1.0f, 1.0f, 3.0f, 0.02f), 3.0f);
    CHECK(pid.valid);
    return 0;
}

static int test_integral_time(void)
{
    control_pid_t fast, slow;
    control_pid_config_t config = config_default();
    int i;
    config.kp = 0.0f;
    config.ki = 2.0f;
    CHECK(control_pid_init(&fast, &config));
    CHECK(control_pid_init(&slow, &config));
    for (i = 0; i < 10; ++i)
        (void)control_pid_step(&fast, 1.0f, 0.0f, 0.0f, 0.01f);
    NEAR(control_pid_step(&slow, 1.0f, 0.0f, 0.0f, 0.1f), 0.2f);
    NEAR(fast.integral, 0.2f);
    NEAR(control_pid_step(&fast, 0.0f, 0.0f, 0.0f, 0.1f), 0.2f);
    return 0;
}

static int test_integral_limit(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    config.kp = 0.0f;
    config.ki = 2.0f;
    config.integral_limit = 0.5f;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 2.0f, 0.0f, 0.0f, 1.0f), 0.5f);
    NEAR(control_pid_step(&pid, -2.0f, 0.0f, 0.0f, 1.0f), -0.5f);
    config.integral_limit = 0.0f;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 2.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    return 0;
}

static int test_antiwindup(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    int i;
    config.ki = 2.0f;
    CHECK(control_pid_init(&pid, &config));
    for (i = 0; i < 100; ++i)
        NEAR(control_pid_step(&pid, 10.0f, 0.0f, 0.0f, 0.1f), 7.0f);
    NEAR(pid.integral, 0.0f);
    NEAR(control_pid_step(&pid, 0.0f, 0.0f, 0.0f, 0.1f), 0.0f);
    for (i = 0; i < 100; ++i)
        NEAR(control_pid_step(&pid, -10.0f, 0.0f, 0.0f, 0.1f), -5.0f);
    NEAR(pid.integral, 0.0f);
    NEAR(control_pid_step(&pid, 1.0f, 0.0f, 0.0f, 0.1f), 2.2f);
    return 0;
}

static int test_feedforward_unwind(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    config.kp = 0.0f;
    config.ki = 1.0f;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 2.0f, 0.0f, 0.0f, 1.0f), 2.0f);
    NEAR(control_pid_step(&pid, 1.0f, 0.0f, 10.0f, 1.0f), 7.0f);
    NEAR(pid.integral, 2.0f);
    NEAR(control_pid_step(&pid, -1.0f, 0.0f, 10.0f, 1.0f), 7.0f);
    NEAR(pid.integral, 1.0f);
    NEAR(control_pid_step(&pid, 0.0f, 0.0f, 0.0f, 1.0f), 1.0f);
    return 0;
}

static int test_signed_gain(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    config.kp = 0.0f;
    config.ki = -1.0f;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 1.0f, 0.0f, 10.0f, 1.0f), 7.0f);
    NEAR(pid.integral, -1.0f);
    control_pid_reset(&pid);
    NEAR(control_pid_step(&pid, 1.0f, 0.0f, -10.0f, 1.0f), -5.0f);
    NEAR(pid.integral, 0.0f);
    return 0;
}

static int test_integral_only_saturation(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    config.kp = 0.0f;
    config.ki = 1.0f;
    config.integral_limit = 20.0f;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 10.0f, 0.0f, 0.0f, 1.0f), 7.0f);
    NEAR(pid.integral, 0.0f);
    NEAR(control_pid_step(&pid, -10.0f, 0.0f, 0.0f, 1.0f), -5.0f);
    NEAR(pid.integral, 0.0f);
    return 0;
}

static int test_derivative_measurement(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    config.kp = 0.0f;
    config.kd = 2.0f;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 0.0f, 10.0f, 0.0f, 0.25f), 0.0f);
    NEAR(control_pid_step(&pid, 100.0f, 10.0f, 0.0f, 0.25f), 0.0f);
    NEAR(control_pid_step(&pid, 100.0f, 10.5f, 0.0f, 0.25f), -4.0f);
    NEAR(control_pid_step(&pid, 100.0f, 10.5f, 0.0f, 0.25f), 0.0f);
    return 0;
}

static int test_derivative_filter(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    config.kp = 0.0f;
    config.kd = 2.0f;
    config.derivative_tau_s = 0.25f;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 0.0f, 0.0f, 0.0f, 0.25f), 0.0f);
    NEAR(control_pid_step(&pid, 0.0f, 1.0f, 0.0f, 0.25f), -4.0f);
    NEAR(control_pid_step(&pid, 0.0f, 1.0f, 0.0f, 0.25f), -2.0f);
    NEAR(control_pid_step(&pid, 0.0f, 1.0f, 0.0f, 0.5f), -0.6666667f);
    return 0;
}

static int test_reset(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    config.ki = 1.0f;
    config.kd = 1.0f;
    config.derivative_tau_s = 0.1f;
    CHECK(control_pid_init(&pid, &config));
    (void)control_pid_step(&pid, 3.0f, 1.0f, 0.0f, 0.1f);
    (void)control_pid_step(&pid, 3.0f, 2.0f, 0.0f, 0.1f);
    CHECK(pid.integral > 0.0f && pid.derivative < 0.0f);
    control_pid_reset(&pid);
    CHECK(pid.valid);
    NEAR(pid.integral, 0.0f);
    NEAR(pid.derivative, 0.0f);
    CHECK(!pid.has_previous_measurement);
    NEAR(control_pid_step(&pid, 30.0f, 30.0f, 0.0f, 0.1f), 0.0f);
    return 0;
}

static int test_invalid_input(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    /* 覆盖每个参数的NaN、正负无穷，以及零/负周期。 */
    const float rows[][4] = {
        {NAN, 0, 0, 1}, {INFINITY, 0, 0, 1}, {-INFINITY, 0, 0, 1},
        {0, NAN, 0, 1}, {0, INFINITY, 0, 1}, {0, -INFINITY, 0, 1},
        {0, 0, NAN, 1}, {0, 0, INFINITY, 1}, {0, 0, -INFINITY, 1},
        {0, 0, 0, NAN}, {0, 0, 0, INFINITY}, {0, 0, 0, -INFINITY},
        {0, 0, 0, 0}, {0, 0, 0, -1}
    };
    unsigned int i;
    config.ki = 1.0f;
    config.kd = 1.0f;
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
    {
        CHECK(control_pid_init(&pid, &config));
        (void)control_pid_step(&pid, 2.0f, 1.0f, 0.0f, 0.1f);
        CHECK(pid.integral > 0.0f);
        NEAR(control_pid_step(&pid, rows[i][0], rows[i][1],
                              rows[i][2], rows[i][3]), 0.0f);
        CHECK(!pid.valid);
        NEAR(pid.integral, 0.0f);
        NEAR(pid.derivative, 0.0f);
        CHECK(!pid.has_previous_measurement);
        NEAR(control_pid_step(&pid, 30.0f, 30.0f, 0.0f, 0.1f), 0.0f);
        CHECK(pid.valid);
    }
    return 0;
}

static int test_invalid_config(void)
{
    control_pid_t pid;
    control_pid_config_t good = config_default();
    control_pid_config_t bad;
    unsigned int i;
    CHECK(control_pid_init(&pid, &good));
    for (i = 0; i < 10U; ++i)
    {
        bad = good;
        switch (i)
        {
        case 0: bad.kp = NAN; break;
        case 1: bad.ki = INFINITY; break;
        case 2: bad.kd = -INFINITY; break;
        case 3: bad.output_min = NAN; break;
        case 4: bad.output_max = INFINITY; break;
        case 5: bad.output_min = bad.output_max; break;
        case 6: bad.output_min = bad.output_max + 1.0f; break;
        case 7: bad.integral_limit = -1.0f; break;
        case 8: bad.derivative_tau_s = -1.0f; break;
        default: bad.derivative_tau_s = NAN; break;
        }
        CHECK(!control_pid_init(&pid, &bad));
        CHECK(!pid.valid);
        NEAR(control_pid_step(&pid, 1.0f, 0.0f, 1.0f, 0.1f), 0.0f);
        CHECK(!pid.valid);
    }
    CHECK(!control_pid_init(&pid, NULL));
    CHECK(!control_pid_init(NULL, &good));
    control_pid_reset(NULL);
    NEAR(control_pid_step(NULL, 1, 0, 0, 1), 0.0f);
    memset(&pid, 0, sizeof(pid));
    NEAR(control_pid_step(&pid, 1, 0, 0, 1), 0.0f);
    CHECK(!pid.valid);
    return 0;
}

static int test_overflow(void)
{
    control_pid_t pid;
    control_pid_config_t config = config_default();
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, FLT_MAX, -FLT_MAX, 0.0f, 1.0f), 0.0f);
    CHECK(!pid.valid);
    config.kp = FLT_MAX;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 2.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    CHECK(!pid.valid);
    config = config_default();
    config.ki = FLT_MAX;
    CHECK(control_pid_init(&pid, &config));
    NEAR(control_pid_step(&pid, 2.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    CHECK(!pid.valid);
    config = config_default();
    config.kd = FLT_MAX;
    CHECK(control_pid_init(&pid, &config));
    (void)control_pid_step(&pid, 0.0f, 0.0f, 0.0f, 0.1f);
    NEAR(control_pid_step(&pid, 0.0f, 2.0f, 0.0f, 0.1f), 0.0f);
    CHECK(!pid.valid);
    CHECK(!pid.has_previous_measurement);
    return 0;
}

int main(int argc, char **argv)
{
    typedef int (*test_function_t)(void);
    const struct { const char *name; test_function_t function; } cases[] = {
        {"proportional", test_proportional}, {"integral-time", test_integral_time},
        {"integral-limit", test_integral_limit}, {"antiwindup", test_antiwindup},
        {"feedforward-unwind", test_feedforward_unwind}, {"signed-gain", test_signed_gain},
        {"integral-only", test_integral_only_saturation},
        {"derivative-measurement", test_derivative_measurement},
        {"derivative-filter", test_derivative_filter}, {"reset", test_reset},
        {"invalid-input", test_invalid_input}, {"invalid-config", test_invalid_config},
        {"overflow", test_overflow}
    };
    unsigned int i;
    if (argc != 2) return 2;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if (strcmp(argv[1], cases[i].name) == 0)
        {
            int result = cases[i].function();
            if (result == 0) printf("PASS %s\n", cases[i].name);
            return result;
        }
    }
    return 2;
}
