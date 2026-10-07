#include "main.h"

/* 屏幕与Wi-Fi保留14:32:41版同步发送流程，加入获准的C13开关和文本遥控接收。
 * 网络调用前后服务当前控制模式；同步连接、发送或重连期间主循环会等待。
 * 舵机1由servo_control独占；C12按配置选择遥控允许或台架测试；电机超时/S3停止由PIT0处理。 */
uint8_t image[MT9V03X_H][MT9V03X_W];
short hist_gram[256];
uint8_t left_line[MT9V03X_H];
uint8_t mid_line[MT9V03X_H];
uint8_t right_line[MT9V03X_H];

static void app_service_controls(void)
{
    /* 保留网络等待期间采到的S3事件；舵机模块仍只在主循环访问。 */
    if (motor_control_take_servo_stop()) servo_control_stop();
#if REMOTE_CONTROL_ENABLED
    remote_control_service(app_uptime_ms);
#else
    servo_control_poll(app_uptime_ms);
#endif
}

static uint32_t app_clock_ms(void)
{
    return app_uptime_ms;
}

static void app_init(void)
{
    camera_display_init();
    /* 上电只把舵机信号置低；遥控与S2台架测试由配置互斥，S3停止优先。 */
    servo_control_init(app_uptime_ms);
    encoder_feedback_init();
    remote_control_init();
    My_Pid_Init();
    /* 编码器控制仍为10ms；网络与舵机状态机使用独立1ms时钟。 */
    pit_ms_init(PIT_CH0, APP_CONTROL_PERIOD_MS);
    pit_ms_init(PIT_CH1, APP_SYSTEM_TICK_MS);
    interrupt_global_enable(0);

    while (mt9v03x_init())
    {
        camera_display_message("Camera init failed; check CSI/power");
        system_delay_ms(1000);
    }
    camera_display_message("Camera ready");
    /* 默认Wi-Fi关闭，仅初始化本地按键；按C13后才走原版同步建链。 */
    (void)camera_debug_init(left_line, mid_line, right_line);
    camera_display_message("PC " CAMERA_DEBUG_WIFI_SERVER_IP ":" CAMERA_DEBUG_WIFI_SERVER_PORT);
    /* 初始化过程中C12不允许启动电机；此后仍需稳定释放再新按下。 */
    motor_control_enable_test();
}

int main(void)
{
    clock_init(SYSTEM_CLOCK_600M);
    motor_control_init(); // 先建立电机零占空比，再初始化耗时外设。
    debug_init();
    system_delay_ms(300); // 启动时等待板上外设供电稳定，不用于运行期Wi-Fi等待。
    app_init();

    while (1)
    {
        app_service_controls();
        camera_debug_poll(app_uptime_ms);
        remote_control_poll(app_clock_ms);
        app_service_controls();
        if (mt9v03x_finish_flag)
        {
            /* 先确认完成标志，再短时间复制供应商完成缓冲；后续模块只读应用副本。 */
            mt9v03x_finish_flag = 0;
            memcpy(image[0], mt9v03x_image[0], sizeof(image));
            camera_debug_capture_frame(image[0]); // 无论Wi-Fi状态如何，先显示当前灰度。
            app_service_controls();  // 完成一次刷屏后及时处理停止键。

            get_hist_gram(image[0], MT9V03X_H, MT9V03X_W, hist_gram);
            unsigned char threshold = get_threshold_otsu(hist_gram);
            binaryzation_process(image[0], MT9V03X_H, MT9V03X_W, threshold);
            auxiliary_process(image[0], MT9V03X_H, MT9V03X_W, 127, left_line, mid_line, right_line);
            camera_debug_send_frame(image[0]); // 沿用历史版同步发送及未发送后缀有限重试。
        }
        /* 发送返回后再次检查按键；没有新图像时也逐圈处理按键和网络恢复。 */
        app_service_controls();
    }
}
