/* 检查真实 SPI 驱动的分块发送行为与超时返回值。 */
#include "zf_common_typedef.h"
#include "zf_device_wifi_spi.h"
#include "platform.h"

static uint8 payload[5000], transmitted[5000];
static uint32 transmitted_length, packet_count, polls, phase;
static uint16 packet_lengths[4];
static int fault;

int gpio_get_level(int pin)
{
    (void)pin;
    ++polls;
    if (fault == 1) return 0;
    if (fault == 2 && packet_count >= 1) return 0;
    return 1;
}
void gpio_low(int pin) { (void)pin; phase = 0; }
void gpio_high(int pin) { (void)pin; }
void system_delay_us(uint32 duration) { assert(duration == 10); }
void system_delay_ms(uint32 duration) { (void)duration; assert(0); }
void gpio_init(int pin,int dir,int level,int mode) { (void)pin; (void)dir; (void)level; (void)mode; assert(0); }
void gpio_set_level(int pin,int level) { (void)pin; (void)level; assert(0); }
void spi_init(int bus,int mode,uint32 speed,int sck,int mosi,int miso,int cs)
{ (void)bus; (void)mode; (void)speed; (void)sck; (void)mosi; (void)miso; (void)cs; assert(0); }

static void capture(const uint8 *data, uint32 length)
{
    assert(transmitted_length + length <= sizeof(transmitted));
    memcpy(transmitted + transmitted_length, data, length);
    transmitted_length += length;
}
void spi_transfer_8bit(int bus,const uint8 *out,uint8 *in,uint32 length)
{
    (void)bus;
    if (phase++ == 0)
    {
        const wifi_spi_head_struct *head = (const wifi_spi_head_struct *)out;
        assert(length == 4 && head->command == WIFI_SPI_DATA && packet_count < 4);
        packet_lengths[packet_count++] = head->length;
        memset(in, 0, length);
        in[0] = WIFI_SPI_REPLY_DATA_END;
    }
    else
    {
        capture(out, length);
        memset(in, 0, length);
    }
}
void spi_write_8bit_array(int bus,const uint8 *data,uint32 length) { (void)bus; capture(data,length); }

static void reset_fixture(int mode)
{
    fault = mode; transmitted_length = 0; packet_count = 0; polls = 0;
}

int main(void)
{
    uint32 i;
    for (i=0; i<sizeof(payload); ++i) payload[i]=(uint8)(i%251);
    reset_fixture(0);
    assert(wifi_spi_send_buffer(payload,0) == 0 && packet_count == 0 && polls == 0);
    reset_fixture(1);
    assert(wifi_spi_send_buffer(payload,5000) == 5000 && packet_count == 0);
    assert(polls == 100000); /* 等待就绪的超时时限为 1000 毫秒，等待次数有界。 */
    reset_fixture(2);
    assert(wifi_spi_send_buffer(payload,5000) == 912);
    assert(packet_count == 1 && transmitted_length == 4088);
    assert(memcmp(transmitted,payload,4088) == 0);
    reset_fixture(0);
    assert(wifi_spi_send_buffer(payload,5000) == 0);
    assert(packet_count == 2 && packet_lengths[0] == 4088 && packet_lengths[1] == 912);
    assert(transmitted_length == 5000 && memcmp(transmitted,payload,5000) == 0);
    puts("PASS: zero length, first timeout, partial timeout, chunk bytes and recovery");
    return 0;
}
