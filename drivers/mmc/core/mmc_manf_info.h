/* zte wangxiaomei add */
#ifndef _MMC_MANF_INFO_H
#define _MMC_MANF_INFO_H

typedef struct _mmc_manf_info {
	int id;
	char *name;
} mmc_manf_info;

mmc_manf_info man_list[] = {
	{0x02, "Sandisk"},
	{0x11, "Toshiba"},
	{0x13, "Micro"},
	{0x15, "Sumsung"},
	{0x45, "Sandisk"},
	{0x70, "Kingston"},
	{0x90, "Hynix"},
	{0xF4, "Biwin"},
	{0x88, "Longsys"},
	{0xD6, "Longsys"},
};


#endif
