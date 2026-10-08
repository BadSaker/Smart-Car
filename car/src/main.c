#include "main.h"

/* C15自动驾驶、C14停车、C12遥控允许；两种驱动模式互斥。
 * 屏幕/Wi-Fi保留冻结的同步驱动，网络阻塞时PIT按视觉/舵机新鲜度停车。
 * 图像处理优先于本帧显示与传输；舵机仅由主循环写入，电机仅由PIT写入。 */
uint8_t image[MT9V03X_H][MT9V03X_W];
static track_vision_result_t vision;
static volatile uint32_t camera_frame_sequence;
static volatile uint32_t camera_frame_completed_ms;
static csi_transfer_callback_t original_camera_callback;
uint8_t left_line[MT9V03X_H];
uint8_t mid_line[MT9V03X_H];
uint8_t right_line[MT9V03X_H];

/* 只包装供应商完成回调，不改库和CSI双缓冲队列。 */
static void app_camera_completed(CSI_Type *base,csi_handle_t *handle,status_t status,void *data)
{
    if(original_camera_callback)original_camera_callback(base,handle,status,data);
    if(status==kStatus_CSI_FrameDone && mt9v03x_finish_flag){
        camera_frame_completed_ms=app_uptime_ms;
        ++camera_frame_sequence;
    }
}
static void app_service_controls(void)
{
    static uint8_t was_autonomous;
    static uint32_t generation;
    autonomous_drive_status_t state;
    if(motor_control_take_servo_stop())servo_control_stop();
    autonomous_control_get_status(&state);
    if(state.active!=was_autonomous || state.run_generation!=generation){
        /* 切换模式不继承旧网络指令或上一次环岛状态。 */
        remote_control_init();track_vision_reset();
        was_autonomous=state.active;generation=state.run_generation;
    }
    if(!autonomous_control_service(app_uptime_ms)){
#if REMOTE_CONTROL_ENABLED
        remote_control_service(app_uptime_ms);
#endif
    }
}

static uint32_t app_clock_ms(void)
{
    return app_uptime_ms;
}

static void app_init(void)
{
    camera_display_init();
    /* 上电无舵机PWM；C15只用于自动驾驶，不再调用舵机摆动测试。 */
    servo_control_init(app_uptime_ms);
    encoder_feedback_init();
    remote_control_init();
    autonomous_control_init();
    track_vision_reset();
    /* 编码器控制仍为10ms；网络与舵机状态机使用独立1ms时钟。 */
    pit_ms_init(PIT_CH0, APP_CONTROL_PERIOD_MS);
    pit_ms_init(PIT_CH1, APP_SYSTEM_TICK_MS);
    interrupt_global_enable(0);

    while (mt9v03x_init())
    {
        camera_display_message("Camera init failed; check CSI/power");
        system_delay_ms(1000);
    }
    {
        uint32_t mask=__get_PRIMASK();
        __disable_irq();
        original_camera_callback=csi_handle.callback;
        csi_handle.callback=app_camera_completed;
        mt9v03x_finish_flag=0;
        __set_PRIMASK(mask);
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
        if(!autonomous_control_is_active())remote_control_poll(app_clock_ms);
        app_service_controls();
        if (mt9v03x_finish_flag)
        {
            uint32_t mask,sequence,completed_ms;
            const uint8_t *completed_image;
            autonomous_drive_status_t state;
            /* 临界区只取缓冲地址与元数据，不在关中断期间复制或处理整幅图。 */
            mask=__get_PRIMASK();__disable_irq();
            sequence=camera_frame_sequence;completed_ms=camera_frame_completed_ms;
            completed_image=mt9v03x_image[0];mt9v03x_finish_flag=0;
            __set_PRIMASK(mask);
            memcpy(image[0],completed_image,sizeof image);
            /* 复制期间换帧则丢弃，不能把混合或久存缓冲伪装成新鲜图像。 */
            if(sequence!=camera_frame_sequence ||
               (uint32_t)(app_uptime_ms-completed_ms)>AUTO_FRAME_MAX_AGE_MS)continue;
            autonomous_control_get_status(&state);
            if(!state.active)track_vision_reset();
            track_vision_process(image[0],completed_ms,state.distance_mm,&vision);
            autonomous_control_publish_vision(&vision,sequence,completed_ms);
            memcpy(left_line,vision.left,sizeof left_line);
            memcpy(mid_line,vision.center,sizeof mid_line);
            memcpy(right_line,vision.right,sizeof right_line);
            app_service_controls();
            camera_debug_capture_frame(image[0]);
            app_service_controls();
            binaryzation_process(image[0],MT9V03X_H,MT9V03X_W,vision.threshold);
            camera_debug_send_frame(image[0]); // 保留历史版同步图传及有限重试。

        }
        /* 发送返回后再次检查按键；没有新图像时也逐圈处理按键和网络恢复。 */
        app_service_controls();
    }
}
