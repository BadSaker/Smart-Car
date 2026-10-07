/* 回归检查使用与固件相同的图像处理和 PID C 源码。 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "img_process.h"
#include "pid.h"

static int failures;
static int checks;
#define CHECK(condition, name) do { ++checks; if (!(condition)) { \
    ++failures; printf("FAIL: %s (line %d)\n", name, __LINE__); } } while (0)

static void test_histogram_repeated_frames(void)
{
    unsigned char image[] = {0, 128, 255, 255};
    short histogram[256];
    unsigned i;
    for (i = 0; i < 256; ++i) histogram[i] = 77;
    get_hist_gram(image, 2, 2, histogram);
    CHECK(histogram[0] == 1 && histogram[128] == 1 && histogram[255] == 2,
          "histogram clears all 256 bins before counting");
    get_hist_gram(image, 2, 2, histogram);
    CHECK(histogram[128] == 1 && histogram[255] == 2,
          "histogram does not accumulate previous frames");
    CHECK(histogram[200] == 0, "unseen high-intensity bins are zero");
}

static void test_threshold_and_binary(void)
{
    short histogram[256] = {0};
    unsigned char image[] = {0, 30, 31, 220};
    histogram[30] = 8;
    histogram[220] = 8;
    CHECK(get_threshold_otsu(histogram) == 30, "two-tone Otsu threshold");
    binaryzation_process(image, 2, 2, 30);
    CHECK(image[0] == 0 && image[1] == 0 && image[2] == 255 && image[3] == 255,
          "binary threshold splits equality to black");
    memset(histogram, 0, sizeof(histogram));
    histogram[255] = 16;
    CHECK(get_threshold_otsu(histogram) == 255, "uniform white frame");
    memset(histogram, 0, sizeof(histogram));
    CHECK(get_threshold_otsu(histogram) == 0, "empty histogram returns zero");
}

static void test_line_outputs_are_current_frame(void)
{
    unsigned char black[24] = {0};
    unsigned char white[24];
    unsigned char narrow[] = {255, 255, 255, 255};
    unsigned char left[3] = {99, 99, 99};
    unsigned char mid[3] = {99, 99, 99};
    unsigned char right[3] = {99, 99, 99};
    unsigned i;
    auxiliary_process(black, 3, 8, 127, left, mid, right);
    for (i = 0; i < 3; ++i)
        CHECK(left[i] < 8 && mid[i] < 8 && right[i] < 8, "black frame returns valid indices");
    memset(white, 255, sizeof(white));
    auxiliary_process(white, 3, 8, 127, left, mid, right);
    CHECK(left[0] == 0 && right[0] == 7 && mid[0] == 3, "white frame has default boundaries");
    left[0] = right[0] = mid[0] = 99;
    auxiliary_process(narrow, 1, 4, 127, left, mid, right);
    CHECK(left[0] == 0 && right[0] == 3 && mid[0] == 1, "narrow frame initializes output boundaries");
}

static void test_pid_integral_limit_and_period(void)
{
    pid_param_t pid;
    unsigned i;
    float output;
    Pid_Param_Init(&pid, 0, 2, 0, 5, 1000);
    for (i = 0; i < 10; ++i) output = PidLocCtrl(&pid, 10, 1);
    CHECK(fabsf(output - 10) < 0.001f, "integral clamp survives output calculation");
    CHECK(fabsf(pid.integrator - 5) < 0.001f, "stored integral is bounded");
    output = PidLocCtrl(&pid, -10, 1);
    CHECK(fabsf(output + 10) < 0.001f, "integral recovers immediately when error reverses");
    Pid_Param_Init(&pid, 2, 0, 0, 5, 3);
    CHECK(PidLocCtrl(&pid, 10, 1) == 3, "positive output saturation");
    CHECK(PidLocCtrl(&pid, -10, 1) == -3, "negative output saturation");
    Pid_Param_Init(&pid, 1, 1, 1, 5, 1000);
    CHECK(PidLocCtrl(&pid, 2, 0) == 0 && pid.integrator == 0,
          "zero period does not divide or mutate PID state");
    CHECK(PidIncCtrl(&pid, 2, -1) == 0 && pid.last_error == 0,
          "negative period does not mutate incremental PID state");
}

int main(void)
{
    test_histogram_repeated_frames();
    test_threshold_and_binary();
    test_line_outputs_are_current_frame();
    test_pid_integral_limit_and_period();
    printf("Algorithm checks: %d passed, %d failed (%d total)\n", checks - failures, failures, checks);
    return failures ? 1 : 0;
}
