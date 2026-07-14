#include <linux/delay.h>
#include <linux/slab.h>
#include "semi_touch_test_5448.h"
#include "semi_touch_function.h"
#include "ztp_common.h"

#if SEMI_TOUCH_FACTORY_TEST_EN

#define MAX_TX_NUM_5472                          32
#define MAX_RX_NUM_5472                          40
#define MAX_CAP_DATA_SIZE                        (MAX_TX_NUM_5472 * MAX_RX_NUM_5472 * 2)
#define SAVE_LOG_NAME                            "/sdcard/chsc_factory_test_result.txt"

#define SHORT_TEST_THL                           600
#define RAWDATA_TEST_THL                         30
#define TEST_BEYOND_MAX_LIMIT		0x0001
#define TEST_BEYOND_MIN_LIMIT		0x0002
#define TEST_GT_SHORT				0x0400
extern int semi_touch_run_ram_code(unsigned char code);

#define semi_touch_log_file(fmt, ...) do { memset(gFactory.catch_buffer, 0, sizeof(gFactory.catch_buffer)); sprintf(gFactory.catch_buffer, fmt, ##__VA_ARGS__); semi_touch_log_file_imp(gFactory.catch_buffer); } while (0)
const short rawdata_min[] = {
    3798,4305,4345,4333,4231,4103,3967,3592,3553,3895,3853,3885,3874,3844,3857,2759,
    4287,4450,4453,4447,4354,4268,4150,3936,3891,4103,4055,4089,4090,4029,4068,3735,
    4316,4469,4472,4468,4376,4258,4150,4116,4101,4107,4048,4083,4084,4054,4093,3863,
    4300,4399,4406,4402,4309,4258,4149,4126,4047,4102,4045,4082,4079,3987,4026,3807,
    4246,4391,4400,4396,4307,4222,4109,4086,4038,4060,4001,4036,4038,3974,4014,3800,
    4246,4432,4440,4435,4345,4228,4120,4094,4075,4068,4009,4042,4043,4014,4055,3839,
    4251,4383,4395,4389,4294,4242,4131,4105,4024,4082,4023,4058,4058,3964,4007,3791,
    4221,4421,4434,4429,4337,4222,4108,4081,4066,4062,4001,4035,4039,4007,4049,3834,
    4199,4342,4359,4354,4259,4209,4094,4067,3986,4046,3986,4021,4023,3928,3969,3757,
    4197,4411,4430,4419,4328,4211,4097,4071,4053,4047,3987,4024,4026,3993,4036,3824,
    4181,4329,4350,4344,4248,4198,4081,4053,3968,4033,3973,4009,4012,3910,3952,3746,
    4183,4339,4363,4351,4255,4205,4089,4060,3975,4040,3982,4015,4017,3917,3960,3752,
    4198,4432,4461,4448,4354,4235,4116,4088,4076,4069,4010,4045,4048,4018,4062,3852,
    4198,4430,4461,4448,4352,4237,4120,4092,4073,4073,4013,4050,4051,4017,4062,3853,
    4185,4367,4400,4383,4286,4227,4108,4080,4004,4059,4001,4035,4038,3947,3990,3783,
    4168,4413,4450,4432,4334,4212,4094,4065,4051,4044,3987,4021,4025,3996,4066,3877,
    4245,4465,4504,4484,4391,4298,4180,4151,4106,4132,4073,4111,4116,4052,4143,3896,
    4151,4402,4444,4422,4324,4205,4082,4053,4034,4032,3976,4010,4012,3979,4025,3825,
    4213,4428,4472,4449,4355,4274,4154,4122,4071,4107,4050,4086,4088,4019,4069,3861,
    4129,4385,4431,4407,4311,4189,4067,4037,4024,4018,3961,3994,3999,3969,4012,3815,
    4233,4469,4516,4491,4398,4300,4180,4151,4112,4134,4079,4113,4117,4058,4107,3900,
    4147,4410,4458,4431,4335,4210,4090,4058,4043,4042,3984,4020,4021,3992,4036,3837,
    4264,4463,4511,4484,4390,4338,4217,4186,4107,4172,4118,4152,4156,4056,4101,3900,
    4224,4491,4541,4510,4417,4297,4178,4147,4137,4134,4079,4114,4117,4086,4129,3932,
    4409,4628,4674,4643,4555,4494,4381,4351,4286,4341,4289,4320,4326,4242,4289,4084,
    4383,4656,4706,4675,4585,4467,4354,4323,4320,4313,4262,4296,4300,4276,4322,4121,
    4504,4742,4789,4758,4677,4601,4495,4467,4428,4458,4407,4443,4447,4392,4435,4232,
    4523,4798,4844,4809,4727,4620,4511,4484,4480,4476,4428,4462,4467,4448,4490,4293,
    4660,4886,4927,4891,4818,4766,4667,4642,4589,4640,4592,4626,4634,4562,4610,4407,
    4664,4942,4983,4945,4871,4770,4673,4647,4646,4643,4599,4632,4637,4620,4666,4465,
    4753,4997,5031,4992,4926,4864,4776,4755,4727,4756,4712,4746,4752,4708,4756,4555,
    4836,5056,5083,5044,4984,4953,4868,4851,4801,4851,4812,4844,4848,4788,4833,4634,
    4863,5134,5151,5116,5052,4972,4895,4877,4881,4879,4840,4871,4875,4864,4909,4710,
    4976,5177,5186,5154,5090,5081,5013,4998,4947,5005,4967,4996,5002,4932,4976,4781,
    4998,5247,5238,5208,5135,5072,5005,4992,4998,4999,4963,4988,4992,4983,5026,4827,
    4493,5270,5218,5129,5097,5045,4996,4994,5026,5031,5017,5061,5088,5110,5177,4481,
};
const short rawdata_max[] = {
    8180,9270,9357,9332,9111,8836,8544,7736,7652,8388,8297,8366,8344,8278,8306,5941,
    9233,9583,9590,9577,9377,9192,8937,8475,8380,8836,8731,8806,8807,8675,8759,8044,
    9296,9623,9632,9622,9424,9170,8937,8864,8831,8843,8717,8793,8794,8730,8814,8320,
    9261,9473,9487,9479,9279,9170,8936,8885,8715,8834,8710,8790,8785,8586,8670,8198,
    9144,9457,9475,9468,9275,9093,8849,8799,8695,8744,8615,8692,8695,8558,8645,8184,
    9143,9543,9562,9552,9357,9105,8871,8815,8775,8759,8633,8705,8708,8645,8731,8268,
    9156,9440,9465,9451,9247,9136,8895,8841,8666,8792,8663,8738,8740,8537,8629,8163,
    9090,9521,9549,9538,9340,9091,8846,8789,8757,8747,8617,8689,8698,8629,8720,8257,
    9044,9352,9387,9377,9172,9065,8817,8758,8583,8713,8583,8659,8663,8458,8548,8090,
    9038,9499,9541,9517,9321,9069,8822,8768,8729,8716,8586,8666,8670,8600,8692,8236,
    9003,9322,9367,9356,9149,9041,8787,8729,8545,8685,8556,8633,8640,8421,8512,8066,
    9007,9343,9396,9370,9164,9055,8806,8744,8559,8699,8575,8646,8652,8435,8527,8080,
    9041,9543,9606,9578,9375,9119,8863,8804,8778,8762,8635,8712,8717,8653,8748,8295,
    9041,9541,9606,9578,9373,9123,8871,8811,8772,8771,8642,8722,8724,8650,8747,8297,
    9011,9403,9476,9438,9230,9102,8846,8786,8622,8741,8615,8689,8695,8499,8593,8146,
    8975,9504,9584,9543,9333,9072,8817,8754,8724,8709,8586,8660,8667,8605,8757,8349,
    9142,9616,9699,9657,9455,9256,9002,8940,8842,8898,8772,8853,8864,8726,8922,8390,
    8940,9479,9570,9522,9311,9055,8790,8727,8687,8684,8562,8635,8639,8569,8667,8237,
    9073,9536,9630,9581,9378,9203,8946,8877,8768,8843,8722,8799,8804,8654,8762,8314,
    8892,9444,9542,9492,9284,9021,8758,8694,8666,8653,8530,8601,8611,8548,8639,8215,
    9116,9623,9725,9671,9472,9261,9002,8939,8855,8904,8785,8857,8866,8740,8843,8398,
    8932,9497,9599,9542,9335,9066,8808,8740,8706,8703,8579,8657,8659,8597,8691,8264,
    9184,9612,9714,9655,9454,9342,9081,9016,8845,8985,8869,8941,8950,8734,8832,8400,
    9097,9672,9780,9711,9513,9254,8997,8930,8909,8902,8783,8859,8866,8800,8892,8467,
    9496,9966,10066,9998,9809,9678,9434,9370,9230,9347,9235,9304,9315,9136,9235,8796,
    9438,10028,10134,10068,9874,9620,9377,9310,9304,9287,9178,9252,9259,9209,9308,8876,
    9700,10213,10313,10246,10071,9909,9681,9620,9535,9601,9492,9567,9577,9458,9552,9114,
    9739,10333,10432,10355,10180,9949,9716,9657,9648,9639,9536,9609,9620,9578,9669,9245,
    10036,10522,10612,10533,10375,10263,10052,9997,9884,9991,9889,9962,9979,9825,9927,9490,
    10045,10642,10731,10649,10490,10273,10063,10008,10005,10000,9903,9976,9986,9949,10047,9616,
    10236,10761,10834,10752,10607,10476,10285,10239,10180,10242,10148,10221,10234,10140,10242,9809,
    10416,10889,10946,10862,10733,10666,10483,10448,10340,10446,10362,10432,10441,10311,10409,9980,
    10473,11057,11093,11018,10880,10708,10542,10502,10512,10507,10423,10490,10500,10476,10572,10144,
    10717,11149,11169,11099,10962,10942,10795,10763,10654,10778,10697,10759,10771,10621,10717,10297,
    10764,11300,11281,11216,11060,10924,10778,10752,10764,10766,10687,10742,10752,10731,10823,10396,
    9676,11349,11237,11046,10977,10865,10759,10756,10824,10834,10803,10899,10957,11005,11149,9650,
};

struct factory_test_init{
	unsigned char rowsCnt;
	unsigned char colsCnt;
	unsigned short sensor_2_ic_map[MAX_RX_NUM_5472 + MAX_TX_NUM_5472];
	char catch_buffer[100];
	unsigned char read_buffer[MAX_CAP_DATA_SIZE];
    struct file *file;
	loff_t pos;
};
static struct factory_test_init gFactory;
int semi_touch_test_prepare(void)
{
	int ret = 0, index = 0;
	/* unsigned char read_buffer[256]; */
	struct m_ctp_cmd_std_t cmd_send_tp;
	struct m_ctp_rsp_std_t ack_from_tp;

	close_esd_function(st_dev.stc.custom_function_en);
	gFactory.pos = 0;

	semi_touch_reset(no_report_after_reset);
	msleep(150);
	cmd_send_tp.id = CMD_IDENTITY;
	ret = cmd_send_to_tp(&cmd_send_tp, &ack_from_tp, 2000);
	check_return_if_fail(ret, NULL);
	if ((ack_from_tp.d0 == 0xE902) && (ack_from_tp.d1 == 0x16fd)) {
		for (index = 0; index < 3; index++) {
			ret = semi_touch_read_bytes(0x20000080, gFactory.read_buffer, 256);
			check_return_if_fail(ret, NULL);
			if (caculate_checksum_u16((unsigned short *)gFactory.read_buffer, 256) == 0) {
				break;
			}
			kernel_log_d("config checksum mismatch, retry = %d\n", index);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			tpd_print_zlog("semi_touch_test_prepare config checksum mismatch, retry = %d\n", index);
			tpd_zlog_record_notify(TP_CRC_ERROR_NO);
#endif
		}

		gFactory.rowsCnt = gFactory.read_buffer[0x1a];
		gFactory.colsCnt = gFactory.read_buffer[0x19];
		for (index = 0; index < gFactory.colsCnt; index++) {
			gFactory.sensor_2_ic_map[index] = gFactory.read_buffer[0x90 + index];
		}
		for (index = 0; index < gFactory.rowsCnt; index++) {
			gFactory.sensor_2_ic_map[index + MAX_TX_NUM_5472] = gFactory.read_buffer[0xb0 + index];
		}
	} else {
		ret = -SEMI_DRV_ERR_NO_INIT;
	}
		kernel_log_d("row = %d, col = %d\n", gFactory.rowsCnt, gFactory.colsCnt);
		return ret;
}

void semi_touch_log_file_imp(char *sztext)
{
	tpd_copy_to_tp_firmware_data(sztext);
}

void semi_touch_print_matrix(short *matrix, int rows, int cols)
{
	int row = 0, col = 0;

	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			semi_touch_log_file("%-6d", *matrix);
			matrix++;
		}
		semi_touch_log_file("\n");
	}
	semi_touch_log_file("\n");
}

int semi_touch_rawdata_test(void)
{
	int ret = 0, raw_averate = 0, failer_cnt = 0;
	int row = 0, col = 0, channels = 0, index = 0;
	short *rawdata_p = NULL;
	const short *max_p = rawdata_max, *min_p = rawdata_min;
	unsigned short *pTail = NULL;
	struct m_ctp_cmd_std_t cmd_send_tp;
	struct m_ctp_rsp_std_t ack_from_tp;

	semi_touch_log_file("\n\n----------------------MCap RawData Test------------------------------\n\n");
	channels = gFactory.rowsCnt * gFactory.colsCnt + gFactory.rowsCnt + gFactory.colsCnt;
	for (index = 0; index < 5; index++) {
		cmd_send_tp.id = CMD_CTP_SSCAN;
		cmd_send_tp.d0 = 0;
		ret = cmd_send_to_tp(&cmd_send_tp, &ack_from_tp, 200);
		check_return_if_fail(ret, NULL);
		msleep(50);
		ret = semi_touch_read_bytes(st_dev.stc.rawdata_addr, gFactory.read_buffer, (channels * 2 + 6 + 3) & 0xfffc);
		check_return_if_fail(ret, NULL);
		pTail = (unsigned short *)&gFactory.read_buffer[channels * 2];
		if (*pTail + *(pTail + 1) == 0xffff) {
			if (*pTail == caculate_checksum_u16((unsigned short *)gFactory.read_buffer, channels * 2)) {
				break;
			}
		}
		cmd_send_tp.id = CMD_CTP_SSCAN;
		cmd_send_tp.d0 = 1;
		ret = cmd_send_to_tp(&cmd_send_tp, &ack_from_tp, 200);
		check_return_if_fail(ret, NULL);
	}
	rawdata_p = (short *)gFactory.read_buffer;
	semi_touch_log_file("mcap_rawdata:\n");
	semi_touch_print_matrix(rawdata_p, gFactory.rowsCnt, gFactory.colsCnt);
	for (index = 0; index < gFactory.rowsCnt * gFactory.colsCnt; index++) {
		raw_averate += *rawdata_p;
		rawdata_p++;
	}
	raw_averate /= (gFactory.rowsCnt * gFactory.colsCnt);
	semi_touch_log_file("\navage = %d\n", raw_averate);
	rawdata_p = (short *)gFactory.read_buffer;
	for (row = 0; row < gFactory.rowsCnt; row++) {
		for (col = 0; col < gFactory.colsCnt; col++) {
			/* not realy used node */
			if (row == 0 && col == 6) {
			} else {
				if (*rawdata_p > *max_p) {
					failer_cnt++;
					semi_touch_log_file("node(%d, %d) out of range: %d(%d-%d)\n", row, col, *rawdata_p, *min_p, *max_p);
				} else if (*rawdata_p < *min_p) {
					failer_cnt++;
					semi_touch_log_file("node(%d, %d) out of range: %d(%d-%d)\n", row, col, *rawdata_p, *min_p, *max_p);
				}
			}
			min_p++;
			max_p++;
			rawdata_p++;
		}
	}
	if (failer_cnt) {
		semi_touch_log_file("\nrawdata test fail\n");
		return 1;
	}
	semi_touch_log_file("\nrawdata test pass\n");
	return 0;
}

int semi_touch_short_test(void)
{
	int ret = 0, tx = 0, rx = 0, loop = 0, failer_cnt = 0;
	short *short_p = NULL, ic_channle = 0;
	unsigned int u32_para_buff[4];
	short short_maxtrix[2][MAX_RX_NUM_5472];

	semi_touch_log_file("\n\n------------------------------Short Test------------------------------\n\n");
	ret = semi_touch_run_ram_code(1 /*RAM_CODE_SHORT_DATA_SHARE */);
	check_return_if_fail(ret, NULL);
	for (loop = 0; loop < 10; loop++) {
		msleep(30);
		u32_para_buff[0] = 0;

		semi_touch_read_bytes(0x20000000, (unsigned char *)u32_para_buff, 12);
		if (u32_para_buff[0] == 0x45000000) {
			break;
		}
	}
	if (u32_para_buff[0] == 0x45000000) {
		msleep(30);
		ret = semi_touch_read_bytes(u32_para_buff[1], gFactory.catch_buffer, (MAX_TX_NUM_5472 + MAX_RX_NUM_5472) * 2);
		check_return_if_fail(ret, NULL);
		short_p = (short *)gFactory.catch_buffer;
		for (tx = 0; tx < gFactory.rowsCnt; tx++) {
			ic_channle = gFactory.sensor_2_ic_map[tx];
			short_maxtrix[0][tx] = short_p[ic_channle];
		}
		for (rx = 0; rx < gFactory.colsCnt; rx++) {
			ic_channle = gFactory.sensor_2_ic_map[rx + MAX_TX_NUM_5472];
			short_maxtrix[1][rx] = short_p[ic_channle + MAX_TX_NUM_5472];
		}
		semi_touch_print_matrix(&short_maxtrix[0][0], 1, gFactory.rowsCnt);
		semi_touch_print_matrix(&short_maxtrix[1][0], 1, gFactory.colsCnt);
		for (tx = 0; tx < gFactory.rowsCnt; tx++) {
			if (short_maxtrix[0][tx] > SHORT_TEST_THL) {
				failer_cnt++;
				semi_touch_log_file("node(%d, %d) out of range: %d\n", 0, tx, short_maxtrix[0][tx]);
			}
		}
		for (rx = 0; rx < gFactory.colsCnt; rx++) {
			if (short_maxtrix[1][rx] > SHORT_TEST_THL) {
				failer_cnt++;
				semi_touch_log_file("node(%d, %d) out of range: %d\n", 1, rx, short_maxtrix[1][rx]);
			}
		}
	} else {
		failer_cnt++;
	}
	if (failer_cnt) {
		semi_touch_log_file("\nshort test fail\n");
		return 2;
	}
	semi_touch_log_file("\nshort test pass\n");
	return 0;
}

void semi_touch_factory_test_over(void)
{
	semi_touch_reset(do_report_after_reset);
	open_esd_function(st_dev.stc.custom_function_en);
}

void semi_get_tpd_channel_info(unsigned char *rowsCnt, unsigned char *colsCnt)
{
	*rowsCnt = gFactory.rowsCnt;
	*colsCnt = gFactory.colsCnt;
}

int semi_touch_start_factory_test(void)
{
	int ret = 0;

	ret = semi_touch_test_prepare();
	if (ret) {
		st_dev.tp_self_test_result = 1;
		return ret;
	}
	ret = semi_touch_rawdata_test();
	if (ret) 
		st_dev.tp_self_test_result = st_dev.tp_self_test_result | TEST_BEYOND_MIN_LIMIT |TEST_BEYOND_MAX_LIMIT;
	ret = semi_touch_short_test();
	if (ret) 
		st_dev.tp_self_test_result = st_dev.tp_self_test_result | TEST_GT_SHORT;
	semi_touch_factory_test_over();
	/* if(0 == ret)
		sprintf(detail, "rawdata test = %s\n, short test = %s\n", "PASS", "PASS");
	else if(1 == ret)
		sprintf(detail, "rawdata test = %s\n", "NG");
	else if(2 == ret)
		sprintf(detail, "short test = %s\n", "NG");
	else
		sprintf(detail, "exception, code = %d\n", ret); */
	return ret;
}

#endif

