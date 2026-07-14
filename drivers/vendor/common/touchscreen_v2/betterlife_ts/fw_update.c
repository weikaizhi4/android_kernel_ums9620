#include "bl_ts.h"
#include "bl_fw.h"

extern char btl_firmware_name[];

static int btl_i2c_transfer(unsigned char i2c_addr, unsigned char *buf, int len, unsigned char rw)
{
	int ret;

	switch (rw) {
	case I2C_WRITE:
		ret = btl_i2c_write(g_btl_ts->client, i2c_addr, buf, len);
		break;
	case I2C_READ:
		ret = btl_i2c_read(g_btl_ts->client, i2c_addr, buf, len);
		break;
	}
	if (ret < 0) {
		BTL_DEBUG("i2c transfer error___\n");
		return -EPERM;
	}

	return 0;
}

static int btl_read_fw(unsigned char i2c_addr, unsigned char reg_addr, unsigned char *buf, int len)
{
	int ret;

	ret = btl_i2c_write(g_btl_ts->client, i2c_addr, &reg_addr, 1);
	if (ret < 0) {
		goto IIC_COMM_ERROR;
	}
	ret = btl_i2c_read(g_btl_ts->client, i2c_addr, buf, len);
	if (ret < 0) {
		goto IIC_COMM_ERROR;
	}

IIC_COMM_ERROR:
	if (ret < 0) {
		BTL_DEBUG("i2c transfer error___\n");
		return -EPERM;
	}

	return 0;
}

int btl_soft_reset_switch_int_wakemode(void)
{
	unsigned char cmd[4];
	int ret = 0x00;

	cmd[0] = RW_REGISTER_CMD;
	cmd[1] = ~cmd[0];
	cmd[2] = CHIP_ID_REG;
	cmd[3] = 0xe8;

	ret = btl_i2c_transfer(CTP_SLAVE_ADDR, cmd, 4, I2C_WRITE);
	if (ret < 0) {
		BTL_DEBUG(" failed:i2c write flash error___\n");
	}

	return ret;
}

int btl_get_chip_id(unsigned char *buf)
{
	unsigned char cmd[3];
	int ret = 0x00;
	unsigned char retry = 3;

	BTL_DEBUG("enter\n");

	while (retry--) {
		cmd[0] = RW_REGISTER_CMD;
		cmd[1] = ~cmd[0];
		cmd[2] = CHIP_ID_REG;

		SET_WAKEUP_LOW;
		btl_i2c_lock();
		ret = btl_i2c_transfer(BTL_FLASH_I2C_ADDR, cmd, 3, I2C_WRITE);
		if (ret < 0) {
			BTL_DEBUG("i2c write flash error___\n");
			goto GET_CHIP_ID_ERROR;
		}

		ret = btl_i2c_transfer(BTL_FLASH_I2C_ADDR, buf, 1, I2C_READ);
		if (ret < 0) {
			BTL_DEBUG("i2c read flash error___\n");
			goto GET_CHIP_ID_ERROR;
		}

		BTL_DEBUG("buf = %x\n", *buf);
		if (*buf == BTL_FLASH_ID) {
			btl_i2c_unlock();
			SET_WAKEUP_HIGH;
			break;
		}

GET_CHIP_ID_ERROR:
		btl_i2c_unlock();
		SET_WAKEUP_HIGH;
		continue;
	}

	return ret;
}

static void btl_switch_protocol(void)
{
	int ret;
	unsigned char cmd[] = { 'U', 'F', 'O' };

	ret = btl_i2c_transfer(CTP_SLAVE_ADDR, cmd, sizeof(cmd), I2C_WRITE);
	if (ret < 0) {
		BTL_DEBUG("failed\n");
	}
	MDELAY(5);
}

#if (UPDATE_MODE == I2C_UPDATE_MODE)
void btl_enter_update_with_i2c(void)
{
	unsigned char cmd[200] = { 0x00 };
	int i = 0;
	int ret = 0;

	for (i = 0; i < sizeof(cmd); i += 2) {
		cmd[i] = 0x5a;
		cmd[i + 1] = 0xa5;
	}

	ret = btl_i2c_transfer(CTP_SLAVE_ADDR, cmd, sizeof(cmd), I2C_WRITE);
	if (ret < 0) {
		BTL_DEBUG("failed:send 5a a5 error___\n");
		goto error;
	}
	MDELAY(50);

error:
	return;
}

void btl_exit_update_with_i2c(void)
{
	int ret = 0;
	unsigned char cmd[2] = { 0x5a, 0xa5 };

	ret = btl_i2c_transfer(CTP_SLAVE_ADDR, cmd, sizeof(cmd), I2C_WRITE);
	if (ret < 0) {
		BTL_DEBUG("failed:send 5a a5 error___\n");
	}

	MDELAY(20);
#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif
	btl_switch_protocol();
}
#endif

#if (UPDATE_MODE == INT_UPDATE_MODE)
void btl_enter_update_with_int(void)
{
	btl_ts_set_intmode(0);
	btl_ts_set_intup(0);
	btl_soft_reset_switch_int_wakemode();
	MDELAY(50);
}

void btl_exit_update_with_int(void)
{
	MDELAY(20);
	btl_ts_set_intup(1);
	MDELAY(20);
	btl_ts_set_intmode(1);
#if defined(RESET_PIN_WAKEUP)
	btl_ts_reset_wakeup();
#endif
	btl_switch_protocol();
}
#endif

int btl_get_fwArgPrj_id(unsigned char *buf)
{
	BTL_DEBUG("enter\n");
	return btl_read_fw(CTP_SLAVE_ADDR, BTL_FWVER_PJ_ID_REG, buf, 3);
}

int btl_get_prj_id(unsigned char *buf)
{
	BTL_DEBUG("enter\n");
	return btl_read_fw(CTP_SLAVE_ADDR, BTL_PRJ_ID_REG, buf, 1);
}

int btl_get_cob_id(unsigned char *buf)
{
	BTL_DEBUG("enter\n");
	return btl_read_fw(CTP_SLAVE_ADDR, COB_ID_REG, buf, 6);
}

#ifdef BTL_UPDATE_FIRMWARE_ENABLE
static int btl_get_protect_flag(void)
{
	unsigned char ret = 0;
	unsigned char protectFlag = 0x00;

	BTL_DEBUG("enter\n");
	ret = btl_read_fw(CTP_SLAVE_ADDR, BTL_PROTECT_REG, &protectFlag, 1);
	if (ret < 0) {
		BTL_DEBUG("failed,ret = %x\n", ret);
		return 0;
	}
	if (protectFlag == 0x55) {
		BTL_DEBUG("protectFlag = %x\n", protectFlag);
		return 1;
	}
	return 0;
}

static int btl_get_specific_argument_for_self_ctp(unsigned int *arguOffset, unsigned char *cobID,
						  unsigned char *fw_data, unsigned int fw_size,
						  unsigned char arguCount)
{
	unsigned char convertCobId[12] = { 0x00 };
	unsigned char i = 0;
	unsigned int cobArguAddr = fw_size - arguCount * BTL_ARGUMENT_FLASH_SIZE;

	BTL_DEBUG("fw_size is %x\n", fw_size);
	BTL_DEBUG("arguCount is %d\n", arguCount);
	BTL_DEBUG("cobArguAddr is %x\n", cobArguAddr);

	for (i = 0; i < sizeof(convertCobId); i++) {
		if (i % 2) {
			convertCobId[i] = cobID[i / 2] & 0x0f;
		} else {
			convertCobId[i] = (cobID[i / 2] & 0xf0) >> 4;
		}
		BTL_DEBUG("before convert:convertCobId[%d] is %x\n", i, convertCobId[i]);
		if (convertCobId[i] < 10) {
			convertCobId[i] = '0' + convertCobId[i];
		} else {
			convertCobId[i] = 'a' + convertCobId[i] - 10;
		}
		BTL_DEBUG("after convert:convertCobId[%d] is %x\n", i, convertCobId[i]);
	}

	BTL_DEBUG("convertCobId is:\n");
	for (i = 0; i < 12; i++) {
		BTL_DEBUG("%x  ", convertCobId[i]);
	}
	BTL_DEBUG("\n");

	for (i = 0; i < arguCount; i++) {
		if (memcmp
		    (convertCobId, fw_data + cobArguAddr + i * BTL_ARGUMENT_FLASH_SIZE + BTL_COB_ID_OFFSET, 12)) {
			BTL_DEBUG("This argu is not the specific argu\n");
		} else {
			*arguOffset = cobArguAddr + i * BTL_ARGUMENT_FLASH_SIZE;
			BTL_DEBUG("This argu is the specific argu, and arguOffset is %x\n", *arguOffset);
			break;
		}
	}

	if (i == arguCount) {
		*arguOffset = BTL_ARGUMENT_BASE_OFFSET;
		return -EPERM;
	}
	return 0;
}

static int btl_get_specific_argument_for_self_interactive_ctp(unsigned int *arguOffset, unsigned char prjID,
							      unsigned char *fw_data, unsigned int fw_size,
							      unsigned char arguCount)
{
	int i = 0;
	unsigned char binPrjID = 0x00;

	BTL_DEBUG_FUNC();
	BTL_DEBUG("prjID = %d, fw_size = %x, arguCount = %d\n", prjID, fw_size, arguCount);
	for (i = 0; i < arguCount; i++) {
		binPrjID = fw_data[BTL_ARGUMENT_BASE_OFFSET + i * MAX_FLASH_SIZE + BTL_PROJECT_ID_OFFSET];
		BTL_DEBUG("i = %d, binPrjID = %d\n", i, binPrjID);
		if (prjID == binPrjID) {
			*arguOffset = i * MAX_FLASH_SIZE;
			break;
		}
	}

	if (i >= arguCount) {
		return -EPERM;
	}
	return 0;
}

static unsigned char btl_get_argument_count_for_self_ctp(unsigned char *fw_data, unsigned int fw_size)
{
	unsigned char i = 0;
	unsigned int addr = 0;

	addr = fw_size;
	BTL_DEBUG("addr is %x\n", addr);
	while (addr > (BTL_ARGUMENT_BASE_OFFSET + BTL_ARGUMENT_FLASH_SIZE)) {
		addr = addr - BTL_ARGUMENT_FLASH_SIZE;
		if (memcmp(fw_data + addr, ARGU_MARK, sizeof(ARGU_MARK) - 1)) {
			BTL_DEBUG("arguMark found flow complete");
			break;
		}
		i++;
		BTL_DEBUG("arguMark founded\n");
	}
	BTL_DEBUG("The argument count is %d\n", i);
	return i;
}

static unsigned int btl_get_cob_project_down_size_arguCnt_for_self_ctp(unsigned char *fw_data,
								       unsigned int fw_size,
								       unsigned char *arguCnt)
{
	unsigned int downSize = 0;

	*arguCnt = btl_get_argument_count_for_self_ctp(fw_data, fw_size);
	downSize = fw_size - (*arguCnt) * BTL_ARGUMENT_FLASH_SIZE - FLASH_PAGE_SIZE;
	return downSize;
}

static unsigned char btl_get_argument_count_for_self_interactive_ctp(unsigned char *fw_data,
								     unsigned int fw_size)
{
	unsigned char i = 0;

	i = fw_size / MAX_FLASH_SIZE;
	BTL_DEBUG("The argument count is %d\n", i);
	return i;
}

static unsigned int btl_get_cob_project_down_size_arguCnt_for_interactive_ctp(unsigned char *fw_data,
									      unsigned int fw_size,
									      unsigned char *arguCnt)
{
	unsigned int downSize = 0;

	*arguCnt = btl_get_argument_count_for_self_interactive_ctp(fw_data, fw_size);
	downSize = MAX_FLASH_SIZE;
	return downSize;
}

static unsigned char btl_is_cob_project_for_self(unsigned char *fw_data, int fw_size)
{
	unsigned char arguKey[4] = { 0xaa, 0x55, 0x09, 0x09 };
	unsigned char *pfw;

	pfw = fw_data + fw_size - 4;

	if (fw_size % FLASH_PAGE_SIZE) {
		return 0;
	}
	if (memcmp(arguKey, pfw, 4)) {
		return 0;
	}
	return 1;
}

static unsigned char btl_is_cob_project_for_self_interactive(unsigned char *fw_data, int fw_size)
{
	if ((fw_size > MAX_FLASH_SIZE) && ((fw_size % MAX_FLASH_SIZE) == 0)) {
		return 1;
	}
	return 0;
}

static int btl_get_fw_checksum(unsigned short *fw_checksum)
{
	unsigned char buf[3];
	unsigned char checksum_ready = 0;
	int retry = 5;
	int ret = 0x00;

	BTL_DEBUG("enter\n");

	buf[0] = CHECKSUM_CAL_REG;
	buf[1] = CHECKSUM_CAL;
	ret = btl_i2c_transfer(CTP_SLAVE_ADDR, buf, 2, I2C_WRITE);
	if (ret < 0) {
		BTL_DEBUG("write checksum cmd error___\n");
		return -EPERM;
	}
	MDELAY(FW_CHECKSUM_DELAY_TIME);

	ret = btl_read_fw(CTP_SLAVE_ADDR, CHECKSUM_REG, buf, 3);
	if (ret < 0) {
		BTL_DEBUG("read checksum error___\n");
		return -EPERM;
	}

	checksum_ready = buf[0];

	while ((retry--) && (checksum_ready != CHECKSUM_READY)) {

		MDELAY(50);
		ret = btl_read_fw(CTP_SLAVE_ADDR, CHECKSUM_REG, buf, 3);
		if (ret < 0) {
			BTL_DEBUG("read checksum error___\n");
			return -EPERM;
		}

		checksum_ready = buf[0];
	}

	if (checksum_ready != CHECKSUM_READY) {
		BTL_DEBUG("read checksum fail___\n");
		return -EPERM;
	}
	*fw_checksum = (buf[1] << 8) + buf[2];

	return 0;
}

static void btl_get_fw_bin_checksum_for_self_ctp(unsigned char *fw_data, unsigned short *fw_bin_checksum,
						 int fw_size, int specifyArgAddr)
{
	int i = 0;
	int temp_checksum = 0x0;

	for (i = 0; i < BTL_ARGUMENT_BASE_OFFSET; i++) {
		temp_checksum += fw_data[i];
	}
	for (i = specifyArgAddr; i < specifyArgAddr + VERTIFY_START_OFFSET; i++) {
		temp_checksum += fw_data[i];
	}
	for (i = specifyArgAddr + VERTIFY_START_OFFSET; i < specifyArgAddr + VERTIFY_START_OFFSET + 4; i++) {
		temp_checksum += fw_data[i];
	}
	for (i = BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET + 4; i < fw_size; i++) {
		temp_checksum += fw_data[i];
	}

	for (i = fw_size; i < MAX_FLASH_SIZE; i++) {
		temp_checksum += 0xff;
	}

	*fw_bin_checksum = temp_checksum & 0xffff;
}

static void btl_get_fw_bin_checksum_for_self_interactive_ctp(unsigned char *fw_data,
							     unsigned short *fw_bin_checksum, int fw_size,
							     int specifyArgAddr)
{
	int i = 0;
	int temp_checksum = 0x0;

	for (i = specifyArgAddr; i < (specifyArgAddr + fw_size); i++) {
		temp_checksum += fw_data[i];
	}

	for (i = fw_size; i < MAX_FLASH_SIZE; i++) {
		temp_checksum += 0xff;
	}
	*fw_bin_checksum = temp_checksum & 0xffff;
}

static void btl_get_fw_bin_checksum_for_compatible_ctp(unsigned char *fw_data,
						       unsigned short *fw_bin_checksum, int fw_size)
{
	int i = 0;
	int temp_checksum = 0x0;

	for (i = 0; i < fw_size; i++) {
		temp_checksum += fw_data[i];
	}
	for (i = fw_size; i < MAX_FLASH_SIZE; i++) {
		temp_checksum += 0xff;
	}

	*fw_bin_checksum = temp_checksum & 0xffff;
}

static int btl_erase_flash(void)
{
	unsigned char cmd[2];

	BTL_DEBUG("enter\n");

	cmd[0] = ERASE_ALL_MAIN_CMD;
	cmd[1] = ~cmd[0];

	return btl_i2c_transfer(BTL_FLASH_I2C_ADDR, cmd, 0x02, I2C_WRITE);
}

static int btl_write_flash(unsigned char cmd, int flash_start_addr, unsigned char *buf, int len)
{
	unsigned char cmd_buf[6 + FLASH_WSIZE];
	unsigned short flash_end_addr;
	int ret;

	BTL_DEBUG("enter\n");
	BTL_DEBUG("flash_start_addr = %x:%x %x len = %x\n", flash_start_addr, buf[0], buf[1], len);
	if (!len) {
		BTL_DEBUG("___write flash len is 0x00,return___\n");
		return -EPERM;
	}

	flash_end_addr = flash_start_addr + len - 1;

	if (flash_end_addr >= MAX_FLASH_SIZE) {
		BTL_DEBUG("___write flash end addr is overflow,return___\n");
		return -EPERM;
	}

	cmd_buf[0] = cmd;
	cmd_buf[1] = ~cmd;
	cmd_buf[2] = flash_start_addr >> 0x08;
	cmd_buf[3] = flash_start_addr & 0xff;
	cmd_buf[4] = flash_end_addr >> 0x08;
	cmd_buf[5] = flash_end_addr & 0xff;

	memcpy(&cmd_buf[6], buf, len);

	ret = btl_i2c_transfer(BTL_FLASH_I2C_ADDR, cmd_buf, len + 6, I2C_WRITE);
	if (ret < 0) {
		BTL_DEBUG("i2c transfer error___\n");
		return -EPERM;
	}

	return 0;
}

static int btl_read_flash(unsigned char cmd, int flash_start_addr, unsigned char *buf, int len)
{
	char ret = 0;
	unsigned char cmd_buf[6];
	unsigned short flash_end_addr;

	flash_end_addr = flash_start_addr + len - 1;
	cmd_buf[0] = cmd;
	cmd_buf[1] = ~cmd;
	cmd_buf[2] = flash_start_addr >> 0x08;
	cmd_buf[3] = flash_start_addr & 0xff;
	cmd_buf[4] = flash_end_addr >> 0x08;
	cmd_buf[5] = flash_end_addr & 0xff;
	ret = btl_i2c_transfer(BTL_FLASH_I2C_ADDR, cmd_buf, 6, I2C_WRITE);
	if (ret < 0) {
		BTL_DEBUG("i2c transfer write error\n");
		return -EPERM;
	}
	ret = btl_i2c_transfer(BTL_FLASH_I2C_ADDR, buf, len, I2C_READ);
	if (ret < 0) {
		BTL_DEBUG("i2c transfer read error\n");
		return -EPERM;
	}

	return 0;
}

static int btl_download_fw_for_self_ctp(unsigned char *pfwbin, int specificArgAddr, int fwsize)
{
	unsigned int i;
	unsigned int size, len;
	unsigned int addr;
	unsigned char verifyBuf[4] = { 0xff, 0xff, 0xff, 0xff };

	BTL_DEBUG("enter\n");

	verifyBuf[2] = pfwbin[BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET + 2];
	verifyBuf[3] = pfwbin[BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET + 3];
	BTL_DEBUG("verifyBuf = %x %x %x %x\n", verifyBuf[0], verifyBuf[1], verifyBuf[2],
		  verifyBuf[3]);
	if (btl_erase_flash()) {
		BTL_DEBUG("___erase flash fail___\n");
		return -EPERM;
	}

	MDELAY(50);

	/* Write data before BTL_ARGUMENT_BASE_OFFSET */
	for (i = 0; i < BTL_ARGUMENT_BASE_OFFSET;) {
		size = BTL_ARGUMENT_BASE_OFFSET - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash(WRITE_MAIN_CMD, addr, &pfwbin[i], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	/* Write the data from BTL_ARGUMENT_BASE_OFFSET to VERTIFY_START_OFFSET */
	for (i = BTL_ARGUMENT_BASE_OFFSET; i < (VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET);) {
		size = VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash
		    (WRITE_MAIN_CMD, addr, &pfwbin[i + specificArgAddr - BTL_ARGUMENT_BASE_OFFSET], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	/* Write the four bytes verifyBuf from VERTIFY_START_OFFSET */
	for (i = (VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET);
	     i < (VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET + sizeof(verifyBuf));) {
		size = VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET + sizeof(verifyBuf) - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash
		    (WRITE_MAIN_CMD, addr, &verifyBuf[i - VERTIFY_START_OFFSET - BTL_ARGUMENT_BASE_OFFSET], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	/* Write data after verityBuf from VERTIFY_START_OFFSET + 4 */
	for (i = (BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET + 4); i < fwsize;) {
		size = fwsize - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash(WRITE_MAIN_CMD, addr, &pfwbin[i], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	return 0;
}

static int btl_download_fw_for_self_interactive_ctp(unsigned char *pfwbin, int specificArgAddr, int fwsize)
{
	unsigned int i;
	unsigned int size, len;
	unsigned int addr;
	unsigned char verifyBuf[4] = { 0xff, 0xff, 0xff, 0xff };

	BTL_DEBUG("enter\n");

	verifyBuf[2] = pfwbin[specificArgAddr + BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET + 2];
	verifyBuf[3] = pfwbin[specificArgAddr + BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET + 3];
	BTL_DEBUG("verifyBuf = %x %x %x %x\n", verifyBuf[0], verifyBuf[1],
		  verifyBuf[2], verifyBuf[3]);
	if (btl_erase_flash()) {
		BTL_DEBUG("___erase flash fail___\n");
		return -EPERM;
	}

	MDELAY(50);

	/* Write data before BTL_ARGUMENT_BASE_OFFSET */
	for (i = 0; i < (VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET);) {
		size = BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash(WRITE_MAIN_CMD, addr, &pfwbin[i + specificArgAddr], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	/* Write the four bytes verifyBuf from VERTIFY_START_OFFSET */
	for (i = (VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET);
	     i < (VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET + sizeof(verifyBuf));) {
		size = VERTIFY_START_OFFSET + BTL_ARGUMENT_BASE_OFFSET + sizeof(verifyBuf) - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash
		    (WRITE_MAIN_CMD, addr, &verifyBuf[i - VERTIFY_START_OFFSET - BTL_ARGUMENT_BASE_OFFSET], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	/* Write data after verityBuf from VERTIFY_START_OFFSET + 4 */
	for (i = (BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET + 4); i < fwsize;) {
		size = fwsize - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash(WRITE_MAIN_CMD, addr, &pfwbin[i + specificArgAddr], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	return 0;
}

static int btl_download_fw_for_compatible_ctp(unsigned char *pfwbin, int fwsize)
{
	unsigned int i;
	unsigned int size, len;
	unsigned int addr;

	BTL_DEBUG("enter\n");

	if (btl_erase_flash()) {
		BTL_DEBUG("___erase flash fail___\n");
		return -EPERM;
	}

	MDELAY(50);

	for (i = 0; i < fwsize;) {
		size = fwsize - i;
		if (size > FLASH_WSIZE) {
			len = FLASH_WSIZE;
		} else {
			len = size;
		}

		addr = i;

		if (btl_write_flash(WRITE_MAIN_CMD, addr, &pfwbin[i], len)) {
			return -EPERM;
		}
		i += len;
		MDELAY(5);
	}

	return 0;
}

static int btl_read_flash_vertify(unsigned char *pfwbin)
{
	unsigned char cnt = 0;
	int ret = 0;
	unsigned char vertify[2] = { 0 };
	unsigned char vertify1[2] = { 0 };

	memcpy(vertify, &pfwbin[BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET], sizeof(vertify));
	BTL_DEBUG("vertify:%x %x\n", vertify[0], vertify[1]);

	SET_WAKEUP_LOW;
	while (cnt < 3) {
		cnt++;
		ret =
		    btl_read_flash(READ_MAIN_CMD, BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET, vertify1,
				   sizeof(vertify1));
		if (ret < 0) {
			BTL_DEBUG("read fail\n");
			continue;
		}

		if (memcmp(vertify, vertify1, sizeof(vertify)) == 0) {
			ret = 0;
			break;
		}
		ret = -1;
	}
	SET_WAKEUP_HIGH;
	return ret;
}

static int btl_write_flash_vertify(unsigned char *pfwbin)
{
	unsigned char cnt = 0;
	int ret = 0;
	unsigned char vertify[2] = { 0 };
	unsigned char vertify1[2] = { 0 };

	memcpy(vertify, &pfwbin[BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET], sizeof(vertify));
	BTL_DEBUG(" vertify:%x %x\n", vertify[0], vertify[1]);

	SET_WAKEUP_LOW;
	while (cnt < 3) {
		cnt++;
		ret =
		    btl_write_flash(WRITE_MAIN_CMD, BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET, vertify,
				    sizeof(vertify));
		if (ret < 0) {
			BTL_DEBUG("write fail\n");
			continue;
		}

		MDELAY(10);

		ret =
		    btl_read_flash(READ_MAIN_CMD, BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET, vertify1,
				   sizeof(vertify1));
		if (ret < 0) {
			BTL_DEBUG("read fail\n");
			continue;
		}

		if (memcmp(vertify, vertify1, sizeof(vertify)) == 0) {
			ret = 0;
			break;
		}
		ret = -1;
	}
	SET_WAKEUP_HIGH;
	return ret;
}

static int btl_update_flash_for_self_ctp(unsigned char update_type, unsigned char *pfwbin, int fwsize,
					 int specificArgAddr)
{
	int retry = 0;
	int ret = 0;
	unsigned short fw_checksum = 0x0;
	unsigned short fw_bin_checksum = 0x0;

	retry = 3;
	while (retry--) {
		SET_WAKEUP_LOW;

		ret = btl_download_fw_for_self_ctp(pfwbin, specificArgAddr, fwsize);

		if (ret < 0) {
			BTL_DEBUG("error retry=%d\n", retry);
			continue;
		}

		MDELAY(50);

		SET_WAKEUP_HIGH;

		btl_get_fw_bin_checksum_for_self_ctp(pfwbin, &fw_bin_checksum, fwsize, specificArgAddr);
		ret = btl_get_fw_checksum(&fw_checksum);
		fw_checksum -= 0xff;
		BTL_DEBUG("fw checksum = 0x%x,fw_bin_checksum =0x%x\n", fw_checksum, fw_bin_checksum);

		if ((ret < 0) || ((update_type == FW_ARG_UPDATE) && (fw_checksum != fw_bin_checksum))) {
			BTL_DEBUG("btl_get_fw_checksum error");
			continue;
		}

		if ((update_type == FW_ARG_UPDATE) && (fw_checksum == fw_bin_checksum)) {
			ret = btl_write_flash_vertify(pfwbin);
			if (ret < 0)
				continue;
		}
		break;
	}

	if (retry < 0) {
		BTL_DEBUG("error\n");
		return -EPERM;
	}

	BTL_DEBUG("success___\n");

	return 0;
}

static int btl_update_flash_for_self_interactive_ctp(unsigned char update_type, unsigned char *pfwbin,
						     int fwsize, int specificArgAddr)
{
	int retry = 0;
	int ret = 0;
	unsigned short fw_checksum = 0x0;
	unsigned short fw_bin_checksum = 0x0;

	retry = 3;
	while (retry--) {
		SET_WAKEUP_LOW;

		ret = btl_download_fw_for_self_interactive_ctp(pfwbin, specificArgAddr, fwsize);

		if (ret < 0) {
			BTL_DEBUG("btl_download_fw_for_self_interactive_ctp error retry=%d\n", retry);
			continue;
		}

		MDELAY(50);

		SET_WAKEUP_HIGH;

		btl_get_fw_bin_checksum_for_self_interactive_ctp(pfwbin, &fw_bin_checksum, fwsize,
								 specificArgAddr);
		ret = btl_get_fw_checksum(&fw_checksum);
		fw_checksum -= 0xff;
		BTL_DEBUG("fw checksum = 0x%x,fw_bin_checksum =0x%x\n", fw_checksum, fw_bin_checksum);

		if ((ret < 0) || ((update_type == FW_ARG_UPDATE) && (fw_checksum != fw_bin_checksum))) {
			BTL_DEBUG("btl_get_fw_checksum error");
			continue;
		}

		if ((update_type == FW_ARG_UPDATE) && (fw_checksum == fw_bin_checksum)) {
			ret = btl_write_flash_vertify(pfwbin);
			if (ret < 0)
				continue;
		}
		break;
	}

	if (retry < 0) {
		BTL_DEBUG("error\n");
		return -EPERM;
	}

	BTL_DEBUG("success___\n");

	return 0;
}

static int btl_update_flash_for_copatible_ctp(unsigned char update_type, unsigned char *pfwbin, int fwsize)
{
	int retry = 0;
	int ret = 0;
	unsigned short fw_checksum = 0x0;
	unsigned short fw_bin_checksum = 0x0;

	retry = 3;
	while (retry--) {
		SET_WAKEUP_LOW;

		ret = btl_download_fw_for_compatible_ctp(pfwbin, fwsize);

		if (ret < 0) {
			BTL_DEBUG("btl fw update start btl_download_fw error retry=%d\n", retry);
			continue;
		}

		MDELAY(50);

		SET_WAKEUP_HIGH;

		btl_get_fw_bin_checksum_for_compatible_ctp(pfwbin, &fw_bin_checksum, fwsize);
		ret = btl_get_fw_checksum(&fw_checksum);
		BTL_DEBUG("btl fw update end,fw checksum = 0x%x,fw_bin_checksum =0x%x\n", fw_checksum,
			  fw_bin_checksum);

		if ((ret < 0) || ((update_type == FW_ARG_UPDATE) && (fw_checksum != fw_bin_checksum))) {
			BTL_DEBUG("btl fw update start btl_download_fw btl_get_fw_checksum error");
			continue;
		}

		break;
	}

	if (retry < 0) {
		BTL_DEBUG("btl fw update error\n");
		return -EPERM;
	}

	BTL_DEBUG("btl fw update success___\n");

	return 0;
}

static unsigned char choose_update_type_for_self_ctp(unsigned char isBlank, unsigned char *fw_data,
						     unsigned char fwVer, unsigned char arguVer,
						     unsigned short fwChecksum, unsigned short fwBinChecksum,
						     int specifyArgAddr)
{
	unsigned char update_type = NONE_UPDATE;

	if (isBlank) {
		update_type = FW_ARG_UPDATE;
		BTL_DEBUG("Update case 0:FW_ARG_UPDATE\n");
	} else {
		if ((fwVer < fw_data[specifyArgAddr + BTL_FWVER_MAIN_OFFSET])
		    || (arguVer < fw_data[specifyArgAddr + BTL_FWVER_ARGU_OFFSET])) {
			update_type = FW_ARG_UPDATE;
			BTL_DEBUG("Update case 1:FW_ARG_UPDATE\n");
		} else {
			update_type = NONE_UPDATE;
			BTL_DEBUG("Update case 4:NONE_UPDATE\n");
		}
	}
	return update_type;
}

static unsigned char choose_update_type_for_self_interactive_ctp(unsigned char isBlank,
								 unsigned char *fw_data, unsigned char fwVer,
								 unsigned char arguVer,
								 unsigned short fwChecksum,
								 unsigned short fwBinChecksum,
								 int specifyArgAddr)
{
	unsigned char update_type = NONE_UPDATE;

	if (isBlank) {
		update_type = FW_ARG_UPDATE;
		BTL_DEBUG("Update case 0:FW_ARG_UPDATE\n");
	} else {
		if ((fwVer != fw_data[specifyArgAddr + BTL_ARGUMENT_BASE_OFFSET + BTL_FWVER_MAIN_OFFSET])
		    || (arguVer != fw_data[specifyArgAddr + BTL_ARGUMENT_BASE_OFFSET + BTL_FWVER_ARGU_OFFSET])
		    || (fwChecksum != fwBinChecksum)) {
			update_type = FW_ARG_UPDATE;
			BTL_DEBUG("Update case 1:FW_ARG_UPDATE\n");
		} else {
			update_type = NONE_UPDATE;
			BTL_DEBUG("Update case 4:NONE_UPDATE\n");
		}
	}
	return update_type;
}

static unsigned char choose_update_type_for_compatible_ctp(unsigned char isBlank, unsigned char *fw_data,
							   unsigned char prjID, unsigned short checksum)
{
	unsigned char update_type = NONE_UPDATE;

	if (isBlank) {
		update_type = FW_ARG_UPDATE;
		BTL_DEBUG("Update case 0:FW_ARG_UPDATE\n");
	} else {
		if ((prjID != fw_data[PJ_ID_OFFSET]) || (checksum == 0)) {
			update_type = FW_ARG_UPDATE;
			BTL_DEBUG("Update case 1:FW_ARG_UPDATE\n");
		} else {
			update_type = NONE_UPDATE;
			BTL_DEBUG("Update case 4:NONE_UPDATE\n");
		}
	}
	return update_type;
}

static int btl_update_fw_for_self_ctp(unsigned char fileType, unsigned char *fw_data, int fw_size)
{
	unsigned char fwArgPrjID[3];	/* firmware version/argument version/project identification */
	int ret = 0x00;
	unsigned char isBlank = 0x0;	/* Indicate the IC have any firmware */
	unsigned short fw_checksum = 0x0;	/* The checksum for firmware in IC */
	unsigned short fw_bin_checksum = 0x0;	/* The checksum for firmware in file */
	unsigned char update_type = NONE_UPDATE;
	unsigned int downSize = 0x0;	/* The available size of firmware data in file */
	unsigned char cobID[6] = { 0 };	/* The identification for COB project */
	/* The specific argument base address in firmware date with cobID */
	unsigned int specificArguAddr = BTL_ARGUMENT_BASE_OFFSET;
	unsigned char arguCount = 0x0;	/* The argument count for COB firmware */
	unsigned char IsCobPrj = 0;	/* Judge the project type depend firmware file */
	unsigned char projectFlag = 0;	/* protect flag */

	BTL_DEBUG("start\n");

/* check protect flag */
	projectFlag = btl_get_protect_flag();
	if (btl_get_protect_flag() && (fileType == HEADER_FILE_UPDATE)) {
		BTL_DEBUG("projectFlag = %x, fileType = %x", projectFlag,
			  fileType);
		return 0;
	}
/* Step 1:Obtain project type */
	IsCobPrj = btl_is_cob_project_for_self(fw_data, fw_size);
	BTL_DEBUG("IsCobPrj = %x", IsCobPrj);

/* Step 2:Obtain IC version number */
	MDELAY(5);
	ret = btl_get_fwArgPrj_id(fwArgPrjID);
	if ((ret < 0)
	    || (fileType == BIN_FILE_UPDATE)
	    || ((ret == 0) && (fwArgPrjID[0] == 0))
	    || ((ret == 0) && (fwArgPrjID[0] == 0xff))
	    || ((ret == 0) && (fwArgPrjID[0] == BTL_FWVER_PJ_ID_REG))
	    || (btl_read_flash_vertify(fw_data) < 0)) {
		isBlank = 1;
		BTL_DEBUG
		    ("This is blank IC ret = %x fwArgPrjID[0]=%x fwArgPrjID[1]=%x fwArgPrjID[2]=%x\n",
		     ret, fwArgPrjID[0], fwArgPrjID[1], fwArgPrjID[2]);
	} else {
		isBlank = 0;
		BTL_DEBUG("ret=%x fwID=%x argID=%x prjID=%x\n", ret, fwArgPrjID[0],
			  fwArgPrjID[1], fwArgPrjID[2]);
	}
	BTL_DEBUG("isBlank = %x\n", isBlank);

/* Step 3:Specify download size */
	if (IsCobPrj) {
		downSize = btl_get_cob_project_down_size_arguCnt_for_self_ctp(fw_data, fw_size, &arguCount);
		BTL_DEBUG("downSize = %x,arguCount = %x\n", downSize, arguCount);
	} else {
		downSize = fw_size;
		BTL_DEBUG("downSize = %x\n", downSize);
	}

UPDATE_SECOND_FOR_COB:
/* Step 4:Update the fwArgPrjID */
	if (!isBlank) {
		btl_get_fwArgPrj_id(fwArgPrjID);
	}
/* Step 5:Specify the argument data for cob project */
	if (IsCobPrj && !isBlank) {
		MDELAY(50);
		ret = btl_get_cob_id(cobID);
		if (ret < 0) {
			BTL_DEBUG("btl_get_cob_id error\n");
			ret = -1;
			goto UPDATE_ERROR;
		} else {
			BTL_DEBUG("cobID = %x %x %x %x %x %x\n", cobID[0],
				  cobID[1], cobID[2], cobID[3], cobID[4], cobID[5]);
		}
		ret =
		    btl_get_specific_argument_for_self_ctp(&specificArguAddr, cobID, fw_data, fw_size, arguCount);
		if (ret < 0) {
			BTL_DEBUG("Can't found argument for CTP module,use default argu:\n");
		}
		BTL_DEBUG("specificArguAddr = %x\n", specificArguAddr);
	}

	BTL_DEBUG("fw_data[] = %x  fw_data[] = %x", fw_data[BTL_ARGUMENT_BASE_OFFSET + VERTIFY_START_OFFSET],
		  fw_data[BTL_ARGUMENT_BASE_OFFSET + VERTIFY_END_OFFSET]);

/* Step 6:Specify whether switch the touch */
	BTL_DEBUG("isBlank = %d  ver1 = %d ver2 = %d binVer1 = %d binVer2 = %d specificAddr = %x", isBlank,
		  fwArgPrjID[0], fwArgPrjID[1], fw_data[specificArguAddr + BTL_FWVER_MAIN_OFFSET],
		  fw_data[specificArguAddr + BTL_FWVER_ARGU_OFFSET], specificArguAddr);
	if (!isBlank) {
		btl_get_fw_bin_checksum_for_self_ctp(fw_data, &fw_bin_checksum, downSize, specificArguAddr);
		ret = btl_get_fw_checksum(&fw_checksum);
		if ((ret < 0) || (fw_checksum != fw_bin_checksum)) {
			BTL_DEBUG("Read checksum fail fw_checksum = %x\n", fw_checksum);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			tpd_print_zlog("Read checksum fail fw_checksum = %x\n", fw_checksum);
			tpd_zlog_record_notify(TP_CRC_ERROR_NO);
#endif
			fw_checksum = 0x00;
		}
		BTL_DEBUG("fw_checksum = 0x%x,fw_bin_checksum = 0x%x___\n",
			  fw_checksum, fw_bin_checksum);
	}
/* Step 7:Select update fw+arg or only update arg */
	update_type =
	    choose_update_type_for_self_ctp(isBlank, fw_data, fwArgPrjID[0], fwArgPrjID[1], fw_checksum,
					    fw_bin_checksum, specificArguAddr);

/* Step 8:Start Update depend condition */
	if (update_type != NONE_UPDATE) {
		ret = btl_update_flash_for_self_ctp(update_type, fw_data, downSize, specificArguAddr);
		if (ret < 0) {
			BTL_DEBUG("btl_update_flash failed\n");
			goto UPDATE_ERROR;
		}
	}
/* Step 9:Execute second update flow when project firmware is cob and last update_type is FW_ARG_UPDATE */
	if ((ret == 0) && (IsCobPrj) && (isBlank)) {
		isBlank = 0;
		BTL_DEBUG
		    ("btl_update_flash for COB project need second update with blank IC:isBlank = %d\n", isBlank);
		goto UPDATE_SECOND_FOR_COB;
	}
	BTL_DEBUG("exit\n");

UPDATE_ERROR:
	return ret;
}

static int btl_update_fw_for_self_interactive_ctp(unsigned char fileType, unsigned char *fw_data, int fw_size)
{
	unsigned char fwArgPrjID[3];	/* firmware version/argument version/project identification */
	int ret = 0x00;
	unsigned char isBlank = 0x0;	/* Indicate the IC have any firmware */
	unsigned short fw_checksum = 0x0;	/* The checksum for firmware in IC */
	unsigned short fw_bin_checksum = 0x0;	/* The checksum for firmware in file */
	unsigned char update_type = NONE_UPDATE;
	unsigned int downSize = 0x0;	/* The available size of firmware data in file */
	unsigned char prjID = { 0 };	/* The identification for COB project */
	unsigned int specificArguAddr = 0;	/* The specific argument base address in firmware date with cobID */
	unsigned char arguCount = 0x0;	/* The argument count for COB firmware */
	unsigned char IsCobPrj = 0;	/* Judge the project type depend firmware file */
	unsigned char projectFlag = 0;	/* protect flag */

	BTL_DEBUG("start\n");

/* check protect flag */
	projectFlag = btl_get_protect_flag();
	if (projectFlag && (fileType == HEADER_FILE_UPDATE)) {
		BTL_DEBUG("projectFlag = %x, fileType = %x", projectFlag, fileType);
		return 0;
	}
/* Step 1:Obtain project type */
	IsCobPrj = btl_is_cob_project_for_self_interactive(fw_data, fw_size);
	BTL_DEBUG("IsCobPrj = %x", IsCobPrj);

/* Step 2:Obtain IC version number */
	MDELAY(5);
	ret = btl_get_fwArgPrj_id(fwArgPrjID);
	if ((ret < 0)
	    || (fileType == BIN_FILE_UPDATE)
	    || ((ret == 0) && (fwArgPrjID[0] == 0))
	    || ((ret == 0) && (fwArgPrjID[0] == 0xff))
	    || ((ret == 0) && (fwArgPrjID[0] == BTL_FWVER_PJ_ID_REG))
	    || (btl_read_flash_vertify(fw_data) < 0)) {
		isBlank = 1;
		BTL_DEBUG
		    ("This is blank IC ret = %x fwArgPrjID[0]=%x fwArgPrjID[1]=%x fwArgPrjID[2]=%x\n",
		     ret, fwArgPrjID[0], fwArgPrjID[1], fwArgPrjID[2]);
	} else {
		isBlank = 0;
		BTL_DEBUG("ret=%x fwID=%x argID=%x prjID=%x\n", ret,
			  fwArgPrjID[0], fwArgPrjID[1], fwArgPrjID[2]);
	}
	BTL_DEBUG("isBlank = %x\n", isBlank);

/* Step 3:Specify download size */
	if (IsCobPrj) {
		downSize =
		    btl_get_cob_project_down_size_arguCnt_for_interactive_ctp(fw_data, fw_size, &arguCount);
		BTL_DEBUG("downSize = %x,arguCount = %x\n", downSize, arguCount);
	} else {
		downSize = fw_size;
		BTL_DEBUG("downSize = %x\n", downSize);
	}

UPDATE_SECOND_FOR_COB:
/* Step 4:Update the fwArgPrjID */
	if (!isBlank) {
		btl_get_fwArgPrj_id(fwArgPrjID);
	}
/* Step 5:Specify the argument data for cob project */
	if (IsCobPrj && !isBlank) {
		MDELAY(50);
		ret = btl_get_prj_id(&prjID);
		if (ret < 0) {
			BTL_DEBUG("btl_get_prj_id error\n");
			ret = -1;
			goto UPDATE_ERROR;
		} else {
			BTL_DEBUG("prjID = %x\n", prjID);
		}
		ret =
		    btl_get_specific_argument_for_self_interactive_ctp(&specificArguAddr, prjID, fw_data,
								       fw_size, arguCount);
		if (ret < 0) {
			BTL_DEBUG("Can't found argument for CTP module,use default argu:\n");
		}
		BTL_DEBUG("specificArguAddr = %x\n", specificArguAddr);
	}
/* Step 6:Specify whether switch the touch */
	BTL_DEBUG("isBlank = %d  ver1 = %d ver2 = %d binVer1 = %d binVer2 = %d specificAddr = %x", isBlank,
		  fwArgPrjID[0], fwArgPrjID[1],
		  fw_data[specificArguAddr + BTL_ARGUMENT_BASE_OFFSET + BTL_FWVER_MAIN_OFFSET],
		  fw_data[specificArguAddr + BTL_ARGUMENT_BASE_OFFSET + BTL_FWVER_ARGU_OFFSET],
		  specificArguAddr);
	if (!isBlank) {
		btl_get_fw_bin_checksum_for_self_interactive_ctp(fw_data, &fw_bin_checksum, downSize,
								 specificArguAddr);
		ret = btl_get_fw_checksum(&fw_checksum);
		if ((ret < 0) || (fw_checksum != fw_bin_checksum)) {
			BTL_DEBUG
			    ("Read checksum fail fw_checksum = %x\n", fw_checksum);
			fw_checksum = 0x00;
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			tpd_print_zlog("Read checksum fail fw_checksum = %x\n", fw_checksum);
			tpd_zlog_record_notify(TP_CRC_ERROR_NO);
#endif
		}
		BTL_DEBUG
		    ("fw_checksum = 0x%x,fw_bin_checksum = 0x%x___\n", fw_checksum, fw_bin_checksum);
	}
/* Step 7:Select update fw+arg or only update arg */
	update_type =
	    choose_update_type_for_self_interactive_ctp(isBlank, fw_data, fwArgPrjID[0], fwArgPrjID[1],
							fw_checksum, fw_bin_checksum, specificArguAddr);

/* Step 8:Start Update depend condition */
	if (update_type != NONE_UPDATE) {
		ret =
		    btl_update_flash_for_self_interactive_ctp(update_type, fw_data, downSize,
							      specificArguAddr);
		if (ret < 0) {
			BTL_DEBUG("btl_update_flash failed\n");
			goto UPDATE_ERROR;
		}
	}
/* Step 9:Execute second update flow when project firmware is cob and last update_type is FW_ARG_UPDATE */
	if ((ret == 0) && (IsCobPrj) && (isBlank)) {
		isBlank = 0;
		BTL_DEBUG
		    ("btl_update_flash for COB project need second update with blank IC:isBlank = %d\n", isBlank);
		goto UPDATE_SECOND_FOR_COB;
	}
	BTL_DEBUG("exit\n");

UPDATE_ERROR:
	return ret;
}

static int btl_update_fw_for_compatible_ctp(unsigned char fileType, unsigned char *fw_data, int fw_size)
{
	unsigned char fwArgPrjID;	/* firmware version */
	int ret = 0x00;
	unsigned short fw_bin_checksum = 0x0;
	unsigned short fw_checksum = 0x0;
	unsigned char isBlank = 0x0;	/* Indicate the IC have any firmware */
	unsigned char update_type = NONE_UPDATE;

	BTL_DEBUG("start\n");

/* Step 1:Obtain IC version number */
	MDELAY(5);
	ret = btl_get_prj_id(&fwArgPrjID);
	if ((ret < 0)
	    || (fileType == BIN_FILE_UPDATE)
	    || ((ret == 0) && (fwArgPrjID == 0))
	    || ((ret == 0) && (fwArgPrjID == 0xff))
	    || ((ret == 0) && (fwArgPrjID == BTL_PRJ_ID_REG))) {
		isBlank = 1;
		BTL_DEBUG("This is blank IC ret = %x fwArgPrjID=%x\n", ret, fwArgPrjID);
	} else {
		isBlank = 0;
		BTL_DEBUG("ret=%x fwID=%x\n", ret, fwArgPrjID);
	}
	BTL_DEBUG("sBlank = %d  fwID = %d binFwID = %d", isBlank, fwArgPrjID, fw_data[BTL_PRJ_ID_REG]);
/* step 2:check checksum */

	BTL_DEBUG("isBlank = %d  fwArgPrjID = %d  binFwArgPrjID = %d\n", isBlank, fwArgPrjID,
		  fw_data[PJ_ID_OFFSET]);
	if (!isBlank) {
		btl_get_fw_bin_checksum_for_compatible_ctp(fw_data, &fw_bin_checksum, fw_size);
		ret = btl_get_fw_checksum(&fw_checksum);
		if ((ret < 0) || (fw_checksum != fw_bin_checksum)) {
			BTL_DEBUG("Read checksum fail fw_checksum = %x\n", fw_checksum);
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
			tpd_print_zlog("Read checksum fail fw_checksum = %x\n", fw_checksum);
			tpd_zlog_record_notify(TP_CRC_ERROR_NO);
#endif
			fw_checksum = 0x00;
		}
		BTL_DEBUG("fw_checksum = 0x%x,fw_bin_checksum = 0x%x___\n", fw_checksum, fw_bin_checksum);
	}
/* Step 2:Select update fw+arg or only update arg */
	update_type = choose_update_type_for_compatible_ctp(isBlank, fw_data, fwArgPrjID, fw_checksum);

/* Step 3:Start Update depend condition */
	if (update_type != NONE_UPDATE) {
		ret = btl_update_flash_for_copatible_ctp(update_type, fw_data, fw_size);
		if (ret < 0) {
			BTL_DEBUG("btl_update_flash failed\n");
			goto UPDATE_ERROR;
		}
	}

UPDATE_ERROR:
	BTL_DEBUG("exit\n");
	return ret;
}

int btl_update_fw(unsigned char fileType, unsigned char ctpType, unsigned char *pFwData,
			 unsigned int fwLen)
{
	int ret = 0;

	BTL_DEBUG("ctpType = %x\n", ctpType);
	switch (ctpType) {
	case SELF_CTP:
		ret = btl_update_fw_for_self_ctp(fileType, pFwData, fwLen);
		break;
	case SELF_INTERACTIVE_CTP:
		ret = btl_update_fw_for_self_interactive_ctp(fileType, pFwData, fwLen);
		break;

	case COMPATIBLE_CTP:
		ret = btl_update_fw_for_compatible_ctp(fileType, pFwData, fwLen);
		break;
	}
	return ret;
}

#ifdef BTL_AUTO_UPDATE_FARMWARE
int btl_auto_update_fw(void)
{
	int ret = 0;
	unsigned int fwLen = sizeof(fwbin);

	BTL_DEBUG("fwLen = %x\n", fwLen);
	g_btl_ts->enter_update = 1;
	ret = btl_update_fw(HEADER_FILE_UPDATE, CTP_TYPE, (unsigned char *)fwbin, fwLen);

	if (ret < 0) {
		BTL_DEBUG("btl_update_fw fail\n");
	} else {
		BTL_DEBUG("btl_update_fw success\n");
	}
	g_btl_ts->enter_update = 0;
	return ret;
}
#endif

#ifdef BTL_UPDATE_FARMWARE_WITH_BIN
static int btl_GetFirmwareSize(char *firmware_path)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize = 0;

	if (pfile == NULL)
		pfile = filp_open(firmware_path, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", firmware_path);
		return -EIO;
	}

	inode = file_inode(pfile);
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	filp_close(pfile, NULL);

	return fsize;
}

static int btl_ReadFirmware(char *firmware_path, unsigned char *firmware_buf)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize;
	loff_t pos;
	mm_segment_t old_fs;

	if (pfile == NULL)
		pfile = filp_open(firmware_path, O_RDONLY, 0);

	if (IS_ERR(pfile)) {
		BTL_DEBUG("error occured while opening file %s. %d\n", firmware_path, PTR_ERR(pfile));
		return -EIO;
	}

	inode = file_inode(pfile);
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	pos = 0;
	vfs_read(pfile, firmware_buf, fsize, &pos);
	filp_close(pfile, NULL);
	set_fs(old_fs);

	return 0;
}

int btl_fw_upgrade_with_bin_file(unsigned char *firmware_path)
{
	unsigned char *pbt_buf = NULL;
	int ret = 0;
	int fwsize = 0;

	/* Obtain the size of firmware bin */
	fwsize = btl_GetFirmwareSize(firmware_path);
	if (fwsize <= 0) {
		BTL_DEBUG("Get firmware size error:%d\n", fwsize);
		return -EPERM;
	}
	BTL_DEBUG("%s:Get firmware size %x\n", __func__, fwsize);

	pbt_buf = kmalloc(fwsize + 1, GFP_KERNEL);
	if (pbt_buf == NULL) {
		BTL_DEBUG("%s ERROR:Get buf failed\n", __func__);
		return -EPERM;
	}

	ret = btl_ReadFirmware(firmware_path, pbt_buf);
	if (ret < 0) {
		BTL_DEBUG("%s ERROR:Read firmware data failed\n", __func__);
		kfree(pbt_buf);
		return -EIO;
	}
	/* Call the upgrade procedure */
	ret = btl_update_fw(BIN_FILE_UPDATE, CTP_TYPE, pbt_buf, fwsize);
	kfree(pbt_buf);
	return ret;
}
#endif

#ifdef BTL_UPDATE_FIRMWARE_WITH_REQUEST_FIRMWARE
int btl_update_firmware_via_request_firmware(void)
{
	int ret = 0;
	const struct firmware *fw = NULL;
	unsigned char *fwData = NULL;
	struct i2c_client* client = g_btl_ts->client;
	struct device *dev = &client->dev;

	BTL_DEBUG_FUNC();
	ret = request_firmware(&fw, btl_firmware_name, dev);
	if(ret == 0) {
		BTL_DEBUG("request firmware %s success", btl_firmware_name);
		fwData = vmalloc(fw->size);
		if (fwData == NULL) {
			BTL_DEBUG("fwData buffer vmalloc fail");
			ret = -ENOMEM;
		} else {
			memcpy(fwData, fw->data, fw->size);
			ret = btl_update_fw(FIRMWARE_UPDATE, CTP_TYPE, fwData, fw->size);
			if(ret < 0) {
				BTL_DEBUG("update firmware fail");
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
				tpd_zlog_record_notify(TP_FW_UPGRADE_ERROR_NO);
#endif
			} else {
				BTL_DEBUG("update firmware success");
			}
		}
	} else {
#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
		tpd_zlog_record_notify(TP_REQUEST_FIRMWARE_ERROR_NO);
#endif
	}

	if (fwData != NULL) {
		vfree(fwData);
		fwData = NULL;
	}

	if (fw != NULL) {
		release_firmware(fw);
		fw = NULL;
	}
	return ret;
}
#endif
#endif
