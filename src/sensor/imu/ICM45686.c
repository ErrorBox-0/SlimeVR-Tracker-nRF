#include <math.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/i2c.h>

#include "ICM45686.h"
#include "../sensor_none.h"

static const float accel_sensitivity = 16.0f / 32768.0f; // Always 16G
static const float gyro_sensitivity = 1000.0f / 32768.0f; // Always 2000dps

static uint8_t last_accel_odr = 0xff;
static uint8_t last_gyro_odr = 0xff;
static const float clock_reference = 32000;
static float clock_scale = 1; // ODR is scaled by clock_rate/clock_reference

static int heder_reset_err = 0;

LOG_MODULE_REGISTER(ICM45686, LOG_LEVEL_DBG);

int icm45_init(const struct i2c_dt_spec *dev_i2c, float clock_rate, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int err = 0;
	if (clock_rate > 0)
	{
		clock_scale = clock_rate / clock_reference;
		err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_IOC_PAD_SCENARIO_OVRD, 0x06); // override pin 9 to CLKIN
		err |= i2c_reg_update_byte_dt(dev_i2c, ICM45686_RTC_CONFIG, 0x20, 0x20); // enable external CLKIN
//		err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_RTC_CONFIG, 0x23); // enable external CLKIN (0x20, default register value is 0x03)
	}
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	err |= icm45_update_odr(dev_i2c, accel_time, gyro_time, accel_actual_time, gyro_actual_time);
//	k_msleep(50); // 10ms Accel, 30ms Gyro startup
	k_msleep(1); // fuck i dont wanna wait that long
	// err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG3, 0x06); // enable FIFO gyro only
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG0, 0x40 | 0b000111); // set FIFO Stream mode, set FIFO depth to 2K bytes
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG3, 0x07); // begin FIFO stream (FIFO gyro only)
	if (err)
		LOG_ERR("I2C error");
	return (err < 0 ? err : 0);
}

void icm45_shutdown(const struct i2c_dt_spec *dev_i2c)
{
	last_accel_odr = 0xff; // reset last odr
	last_gyro_odr = 0xff; // reset last odr
	int err = i2c_reg_write_byte_dt(dev_i2c, ICM45686_REG_MISC2, 0x02); // Don't need to wait for ICM to finish reset
	if (err)
		LOG_ERR("I2C error");
}

int icm45_update_odr(const struct i2c_dt_spec *dev_i2c, float accel_time, float gyro_time, float *accel_actual_time, float *gyro_actual_time)
{
	int ODR;
	uint8_t ACCEL_UI_FS_SEL = ACCEL_UI_FS_SEL_16G;
	uint8_t GYRO_UI_FS_SEL = GYRO_UI_FS_SEL_1000DPS;
	uint8_t ACCEL_MODE;
	uint8_t GYRO_MODE;
	uint8_t ACCEL_ODR;
	uint8_t GYRO_ODR;
    accel_time *=2;
	// Calculate accel
	if (accel_time <= 0 || accel_time == INFINITY) // off, standby interpreted as off
	{
		ACCEL_MODE = ACCEL_MODE_OFF;
		ODR = 0;
	}
	else
	{
		ACCEL_MODE = ACCEL_MODE_LN;
		ODR = 1 / accel_time;
		ODR /= clock_scale; // scale clock
	}

	if (ACCEL_MODE != ACCEL_MODE_LN)
	{
		ACCEL_ODR = 0;
		accel_time = 0; // off
	}
	else if (ODR > 3200) // TODO: this is absolutely awful
	{
		ACCEL_ODR = ACCEL_ODR_6_4kHz;
		accel_time = 1.0 / 6400;
	}
	else if (ODR > 1600)
	{
		ACCEL_ODR = ACCEL_ODR_3_2kHz;
		accel_time = 1.0 / 3200;
	}
	else if (ODR > 800)
	{
		ACCEL_ODR = ACCEL_ODR_1_6kHz;
		accel_time = 1.0 / 1600;
	}
	else if (ODR > 400)
	{
		ACCEL_ODR = ACCEL_ODR_800Hz;
		accel_time = 1.0 / 800;
	}
	else if (ODR > 200)
	{
		ACCEL_ODR = ACCEL_ODR_400Hz;
		accel_time = 1.0 / 400;
	}
	else if (ODR > 100)
	{
		ACCEL_ODR = ACCEL_ODR_200Hz;
		accel_time = 1.0 / 200;
	}
	else if (ODR > 50)
	{
		ACCEL_ODR = ACCEL_ODR_100Hz;
		accel_time = 1.0 / 100;
	}
	else if (ODR > 25)
	{
		ACCEL_ODR = ACCEL_ODR_50Hz;
		accel_time = 1.0 / 50;
	}
	else if (ODR > 12.5)
	{
		ACCEL_ODR = ACCEL_ODR_25Hz;
		accel_time = 1.0 / 25;
	}
	else
	{
		ACCEL_ODR = ACCEL_ODR_12_5Hz;
		accel_time = 1.0 / 12.5;
	}
	accel_time /= clock_scale; // scale clock

	// Calculate gyro
	if (gyro_time <= 0) // off
	{
		GYRO_MODE = GYRO_MODE_OFF;
		ODR = 0;
	}
	else if (gyro_time == INFINITY) // standby
	{
		GYRO_MODE = GYRO_MODE_STANDBY;
		ODR = 0;
	}
	else
	{
		GYRO_MODE = GYRO_MODE_LN;
		ODR = 1 / gyro_time;
		ODR /= clock_scale; // scale clock
	}

	if (GYRO_MODE != GYRO_MODE_LN)
	{
		GYRO_ODR = 0;
		gyro_time = 0; // off
	}
	else if (ODR > 3200) // TODO: this is absolutely awful
	{
		GYRO_ODR = GYRO_ODR_6_4kHz;
		gyro_time = 1.0 / 6400;
	}
	else if (ODR > 1600)
	{
		GYRO_ODR = GYRO_ODR_3_2kHz;
		gyro_time = 1.0 / 3200;
	}
	else if (ODR > 800)
	{
		GYRO_ODR = GYRO_ODR_1_6kHz;
		gyro_time = 1.0 / 1600;
	}
	else if (ODR > 400)
	{
		GYRO_ODR = GYRO_ODR_800Hz;
		gyro_time = 1.0 / 800;
	}
	else if (ODR > 200)
	{
		GYRO_ODR = GYRO_ODR_400Hz;
		gyro_time = 1.0 / 400;
	}
	else if (ODR > 100)
	{
		GYRO_ODR = GYRO_ODR_200Hz;
		gyro_time = 1.0 / 200;
	}
	else if (ODR > 50)
	{
		GYRO_ODR = GYRO_ODR_100Hz;
		gyro_time = 1.0 / 100;
	}
	else if (ODR > 25)
	{
		GYRO_ODR = GYRO_ODR_50Hz;
		gyro_time = 1.0 / 50;
	}
	else if (ODR > 12.5)
	{
		GYRO_ODR = GYRO_ODR_25Hz;
		gyro_time = 1.0 / 25;
	}
	else
	{
		GYRO_ODR = GYRO_ODR_12_5Hz;
		gyro_time = 1.0 / 12.5;
	}
	gyro_time /= clock_scale; // scale clock

	if (last_accel_odr == ACCEL_ODR && last_gyro_odr == GYRO_ODR) // if both were already configured
		return 1;

	int err = 0;
	// only if the power mode has changed
	// if (last_accel_odr == 0xff || last_gyro_odr == 0xff || (last_accel_odr == 0 ? 0 : 1) != (ACCEL_ODR == 0 ? 0 : 1) || (last_gyro_odr == 0 ? 0 : 1) != (GYRO_ODR == 0 ? 0 : 1))
	// { // TODO: can't tell difference between gyro off and gyro standby
		err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_PWR_MGMT0, GYRO_MODE << 2 | ACCEL_MODE); // set accel and gyro modes
		k_busy_wait(250); // wait >200us // TODO: is this needed?
	// }
	last_accel_odr = ACCEL_ODR;
	last_gyro_odr = GYRO_ODR;

	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_ACCEL_CONFIG0, ACCEL_UI_FS_SEL << 4 | ACCEL_ODR); // set accel ODR and FS
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_GYRO_CONFIG0, GYRO_UI_FS_SEL << 4 | GYRO_ODR); // set gyro ODR and FS
	if (err)
		LOG_ERR("I2C error");

	*accel_actual_time = accel_time;
	*gyro_actual_time = gyro_time;

	return 0;
}

// #define PAKET_SIZE 16
// uint16_t icm45_fifo_read(const struct i2c_dt_spec *dev_i2c, uint8_t *data, uint16_t len) // TODO: check if working
// {
// 	if(heder_reset_err>5){
// 		printk("Error!!!! Spiiiiin!!!");
// 		heder_reset_err = 0;
// 		i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG0, 0x00);
// 		k_busy_wait(250); 
// 		i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG0, 0x40 | 0b000111); // set FIFO Stream mode, set FIFO depth to 2K bytes
// 		i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG3, 0x07);
// 		k_busy_wait(250); 
// 	}

// 	uint8_t rawCount[2];
// 	int err = i2c_burst_read_dt(dev_i2c, ICM45686_FIFO_COUNT_0, &rawCount[0], 2);
// 	uint16_t packets = (uint16_t)(rawCount[1] << 8 | rawCount[0]); // Turn the 16 bits into a unsigned 16-bit value
// 	uint16_t count = packets * PAKET_SIZE; // FIFO packet size is 8 bytes for Gyro-only
// 	uint16_t limit = len / PAKET_SIZE;
// 	if(packets == 0) return 0;
// 	// printk("%d %d \n",packets, count);
// 	if (packets > limit)
// 	{
// 		packets = limit;
// 		count = packets * PAKET_SIZE;
// 	}
// 	uint16_t offset = 0;
// 	uint8_t addr = ICM45686_FIFO_DATA;
// 	err |= i2c_write_dt(dev_i2c, &addr, 1); // Start read buffer
// 	while (count > 0)
// 	{
// 		err |= i2c_read_dt(dev_i2c, &data[offset], count > 128 ? 128 : count); // Read less than 255 at a time (for nRF52832)
// 		offset += 128;
// 		count = count > 128 ? count - 128 : 0;
// 	}
// 	if (err)
// 		LOG_ERR("I2C error");
// 	// else if (packets != 0) // keep reading until FIFO is empty
// 	// 	packets += icm45_fifo_read(dev_i2c, &data[packets * PAKET_SIZE], len - packets * PAKET_SIZE);
// 	return packets;
// }
#define PAKET_SIZE 16
uint16_t icm45_fifo_read(const struct i2c_dt_spec *dev_i2c, uint8_t *data, uint16_t len)
{
    uint8_t rawCount[2];
    int err = 0;
    uint16_t total_packets = 0; // 전체 읽은 패킷 수
    uint16_t offset = 0;        // 데이터 버퍼의 오프셋
	if(heder_reset_err>5){
		printk("Error, Spiiiiin!!!");
		heder_reset_err = 0;
		i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG0, 0x00);
		k_busy_wait(250); 
		i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG0, 0x40 | 0b000111); // set FIFO Stream mode, set FIFO depth to 2K bytes
		i2c_reg_write_byte_dt(dev_i2c, ICM45686_FIFO_CONFIG3, 0x07);
		k_busy_wait(250); 
	}
    do
    {
        // 1. FIFO에 저장된 데이터 개수 확인
        err = i2c_burst_read_dt(dev_i2c, ICM45686_FIFO_COUNT_0, &rawCount[0], 2);
        if (err)
        {
            LOG_ERR("I2C error during FIFO count read");
            break;
        }

        uint16_t packets = (uint16_t)(rawCount[1] << 8 | rawCount[0]); // 패킷 수 계산
        if (packets == 0)
            break; // 더 이상 읽을 데이터가 없으면 종료

        uint16_t count = packets * PAKET_SIZE; // 총 바이트 수
        uint16_t limit = len / PAKET_SIZE;     // 버퍼 제한에 따른 패킷 수

        // 버퍼 한도를 초과하지 않도록 패킷 수를 조정
        if (packets > limit)
        {
            packets = limit;
            count = packets * PAKET_SIZE;
        }

        // 2. FIFO 데이터 읽기 시작
        uint8_t addr = ICM45686_FIFO_DATA;
        err = i2c_write_dt(dev_i2c, &addr, 1); // FIFO 읽기 시작
        if (err)
        {
            LOG_ERR("I2C error during FIFO read start");
            break;
        }

        while (count > 0)
        {
            uint16_t chunk_size = count > 128 ? 128 : count; // 한 번에 최대 128바이트 읽기
            err = i2c_read_dt(dev_i2c, &data[offset], chunk_size);
            if (err)
            {
                LOG_ERR("I2C error during FIFO data read");
                break;
            }

            offset += chunk_size;
            count -= chunk_size;
        }

        // 읽은 패킷 수를 누적
        total_packets += packets;

        // 버퍼의 남은 공간이 없으면 종료
        len -= packets * PAKET_SIZE;
        if (len < PAKET_SIZE)
            break;

    } while (1); // FIFO에 남은 데이터가 있을 때까지 반복

    return total_packets;
}

int icm45_fifo_process(uint16_t index, uint8_t *data, float g[3], float a[3])
{

	int res = 0;
	index *= 8; // Packet size 8 bytes
	// TODO: No way to tell if packet is empty?
	// combine into 16 bit values


	uint8_t header = data[0];
	if(!(header&0b01100000)){
		heder_reset_err++;
		printk("Header Error, %x, %d \n",header, heder_reset_err);
		return 0;
	}

	float rawa[3];
	rawa[0]= (int16_t)(data[2] << 8 | data[1]);
	rawa[1]= (int16_t)(data[4] << 8 | data[3]);
	rawa[2]= (int16_t)(data[6] << 8 | data[5]);

	if(data[1]!=0&&data[2]!=128){
		for (int i = 0; i < 3; i++) // x, y, z
			rawa[i] *= accel_sensitivity;
		memcpy(a, rawa, sizeof(rawa));
		res +=1;
	}
	else{
		a[0] = 0;
		a[1] = 0;
		a[2] = 0;
	}
	g[0]= (int16_t)(data[8] << 8 | data[7]);
	g[1]= (int16_t)(data[10] << 8 | data[9]);
	g[2]= (int16_t)(data[12] << 8 | data[11]);
	for (int i = 0; i < 3; i++) // x, y, z
		g[i] *= gyro_sensitivity;
	res +=2;
	return res;
}

void icm45_accel_read(const struct i2c_dt_spec *dev_i2c, float a[3])
{
	uint8_t rawAccel[6];
	int err = i2c_burst_read_dt(dev_i2c, ICM45686_ACCEL_DATA_X1_UI, &rawAccel[0], 6);
	if (err)
		LOG_ERR("I2C error");
	for (int i = 0; i < 3; i++) // x, y, z
	{
		a[i] = (int16_t)((((int16_t)rawAccel[(i * 2) + 1]) << 8) | rawAccel[i * 2]);
		a[i] *= accel_sensitivity;
	}
}

void icm45_gyro_read(const struct i2c_dt_spec *dev_i2c, float g[3])
{
	uint8_t rawGyro[6];
	int err = i2c_burst_read_dt(dev_i2c, ICM45686_GYRO_DATA_X1_UI, &rawGyro[0], 6);
	if (err)
		LOG_ERR("I2C error");
	for (int i = 0; i < 3; i++) // x, y, z
	{
		g[i] = (int16_t)((((int16_t)rawGyro[(i * 2) + 1]) << 8) | rawGyro[i * 2]);
		g[i] *= gyro_sensitivity;
	}
}

float icm45_temp_read(const struct i2c_dt_spec *dev_i2c)
{
	uint8_t rawTemp[2];
	int err = i2c_burst_read_dt(dev_i2c, ICM45686_TEMP_DATA1_UI, &rawTemp[0], 2);
	if (err)
		LOG_ERR("I2C error");
	// Temperature in Degrees Centigrade = (TEMP_DATA / 128) + 25
	float temp = (int16_t)((((int16_t)rawTemp[1]) << 8) | rawTemp[0]);
	temp /= 128;
	temp += 25;
	return temp;
}

void icm45_setup_WOM(const struct i2c_dt_spec *dev_i2c) // TODO: check if working
{
	uint8_t interrupts;
	uint8_t ireg_buf[5];
	int err = i2c_reg_read_byte_dt(dev_i2c, ICM45686_INT1_STATUS0, &interrupts); // clear reset done int flag // TODO: is this needed
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_INT1_CONFIG0, 0x00); // disable default interrupt (RESET_DONE)
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_ACCEL_CONFIG0, ACCEL_UI_FS_SEL_8G << 4 | ACCEL_ODR_200Hz); // set accel ODR and FS
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_PWR_MGMT0, ACCEL_MODE_LP); // set accel and gyro modes
	ireg_buf[0] = ICM45686_IPREG_SYS2; // address is a word, icm is big endian
	ireg_buf[1] = ICM45686_IPREG_SYS2_REG_129;
	ireg_buf[2] = 0x00; // set ACCEL_LP_AVG_SEL to 1x
	err |= i2c_burst_write_dt(dev_i2c, ICM45686_IREG_ADDR_15_8, ireg_buf, 3); // write buffer
	// should already be defaulted to AULP
//	ireg_buf[0] = ICM45686_IPREG_TOP1;
//	ireg_buf[1] = ICM45686_SMC_CONTROL_0;
//	ireg_buf[2] = 0x60; // set ACCEL_LP_CLK_SEL to AULP
//	err |= i2c_burst_write_dt(dev_i2c, ICM45686_IREG_ADDR_15_8, ireg_buf, 3); // write buffer
	ireg_buf[0] = ICM45686_IPREG_TOP1;
	ireg_buf[1] = ICM45686_ACCEL_WOM_X_THR;
	ireg_buf[2] = 0x08; // set wake thresholds // 8 x 3.9 mg is ~31.25 mg
	ireg_buf[3] = 0x08; // set wake thresholds
	ireg_buf[4] = 0x08; // set wake thresholds
	err |= i2c_burst_write_dt(dev_i2c, ICM45686_IREG_ADDR_15_8, ireg_buf, 5); // write buffer
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_TMST_WOM_CONFIG, 0x14); // enable WOM, enable WOM interrupt
	err |= i2c_reg_write_byte_dt(dev_i2c, ICM45686_INT1_CONFIG1, 0x0E); // route WOM interrupt
	if (err)
		LOG_ERR("I2C error");
}

const sensor_imu_t sensor_imu_icm45686 = {
	*icm45_init,
	*icm45_shutdown,

	*icm45_update_odr,

	*icm45_fifo_read,
	*icm45_fifo_process,
	*icm45_accel_read,
	*icm45_gyro_read,
	*icm45_temp_read,

	*icm45_setup_WOM,
	
	*imu_none_ext_setup,
	*imu_none_fifo_process_ext,
	*imu_none_ext_read,
	*imu_none_ext_passthrough
};
