/************************************************************************
* Copyright (c) 2012-2020, Focaltech Systems (R)£¬All Rights Reserved.
*
* File Name: bl_test_ini.h
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-01
*
* Abstract: parsing function of INI file
*
************************************************************************/
#ifndef _BL_TEST_INI_H
#define _BL_TEST_INI_H
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define MAX_KEYWORD_NUM							(1000)
#define MAX_KEYWORD_NAME_LEN					(50)
#define MAX_KEYWORD_VALUE_LEN					(512)
#define MAX_KEYWORD_VALUE_ONE_LEN				(16)
#define MAX_INI_LINE_LEN		(MAX_KEYWORD_NAME_LEN + MAX_KEYWORD_VALUE_LEN)
#define MAX_INI_SECTION_NUM						(20)
#define MAX_IC_NAME_LEN							(32)
#define MAX_TEST_ITEM							(20)
#define IC_CODE_OFFSET							(16)

/*****************************************************************************
* enumerations, structures and unions
*****************************************************************************/
struct ini_ic_type {
	char ic_name[MAX_IC_NAME_LEN];
	u32 ic_type;
};

enum line_type {
	LINE_SECTION = 1,
	LINE_KEYWORD = 2,
	LINE_OTHER = 3,
};

struct ini_keyword {
	char name[MAX_KEYWORD_NAME_LEN];
	char value[MAX_KEYWORD_VALUE_LEN];
};

struct ini_section {
	char name[MAX_KEYWORD_NAME_LEN];
	int keyword_num;
	/* point to ini.tmp, don't need free */
	struct ini_keyword *keyword;
};

struct ini_data {
	char *data;
	int length;
	int keyword_num_total;
	int section_num;
	struct ini_section section[MAX_INI_SECTION_NUM];
	struct ini_keyword *tmp;
	char ic_name[MAX_IC_NAME_LEN];
	u32 ic_code;
};

#define TEST_ITEM_INCELL			{ \
	"SHORT_CIRCUIT_TEST", \
	"OPEN_TEST", \
	"CB_TEST", \
	"RAWDATA_TEST", \
	"LCD_NOISE_TEST", \
	"KEY_SHORT_TEST", \
	"MUX_OPEN_TEST", \
}

#define BASIC_THRESHOLD_INCELL		{ \
	"ShortCircuit_ResMin", "ShortCircuit_VkResMin", \
	"OpenTest_CBMin", "OpenTest_Check_K1", "OpenTest_K1Threshold", "OpenTest_Check_K2", "OpenTest_K2Threshold", \
	"CBTest_Min", "CBTest_Max", \
	"CBTest_VKey_Check", "CBTest_Min_Vkey", "CBTest_Max_Vkey", \
	"RawDataTest_Min", "RawDataTest_Max", \
	"RawDataTest_VKey_Check", "RawDataTest_Min_VKey", "RawDataTest_Max_VKey", \
	"LCD_NoiseTest_Frame", "LCD_NoiseTest_Coefficient", "LCD_NoiseTest_Coefficient_key", \
	"OpenTest_DifferMin", \
}

#define TEST_ITEM_MC_SC				{ \
	"RAWDATA_TEST", \
	"UNIFORMITY_TEST", \
	"SCAP_CB_TEST", \
	"SCAP_RAWDATA_TEST", \
	"WEAK_SHORT_CIRCUIT_TEST", \
	"PANEL_DIFFER_TEST", \
	"RELATIVE_RAWDATA_TEST", \
	"DRAM_TEST", \
}

#define BASIC_THRESHOLD_MC_SC		{ \
	"RawDataTest_High_Min", "RawDataTest_High_Max", "RawDataTest_HighFreq", \
	"RawDataTest_Low_Min", "RawDataTest_Low_Max", "RawDataTest_LowFreq", \
	"UniformityTest_Check_Tx", "UniformityTest_Check_Rx","UniformityTest_Check_MinMax", \
	"UniformityTest_Tx_Hole", "UniformityTest_Rx_Hole", "UniformityTest_MinMax_Hole", \
	"SCapCbTest_OFF_Min", "SCapCbTest_OFF_Max", "ScapCBTest_SetWaterproof_OFF", \
	"SCapCbTest_ON_Min", "SCapCbTest_ON_Max", "ScapCBTest_SetWaterproof_ON", \
	"SCapRawDataTest_OFF_Min", "SCapRawDataTest_OFF_Max", "SCapRawDataTest_SetWaterproof_OFF", \
	"SCapRawDataTest_ON_Min", "SCapRawDataTest_ON_Max", "SCapRawDataTest_SetWaterproof_ON", \
	"WeakShortTest_Min", "WeakShortTest_Max", \
	"PanelDifferTest_Min", "PanelDifferTest_Max", \
	"SCapCbTest_High_Min", "SCapCbTest_High_Max", "ScapCBTest_SetHighSensitivity", \
	"SCapRawDataTest_High_Min", "SCapRawDataTest_High_Max", "SCapRawDataTest_SetHighSensitivity", \
	"SCapCbTest_Hov_Min", "SCapCbTest_Hov_Max", "ScapCBTest_SetHov", \
	"SCapRawDataTest_Hov_Min", "SCapRawDataTest_Hov_Max", "SCapRawDataTest_SetHov", \
	"PanelDifferTest_Vol", "PanelDifferTest_High_Freq", "PanelDifferTest_Low_Freq", \
	"Rel_RawDataTest_Min", "Rel_RawDataTest_Max", \
}

#define TEST_ITEM_SC				{ \
	"RAWDATA_TEST", \
	"CB_TEST", \
	"DELTA_CB_TEST", \
	"RAWDATA_CB_TEST_MODE1", \
	"RAWDATA_CB_TEST_MODE2", \
	"RAWDATA_CB_TEST_MODE3", \
	"RAWDATA_CB_TEST_MODE4", \
	"RAWDATA_CB_TEST_MODE5", \
	"RAWDATA_CB_TEST_MODE6", \
	"WEAK_SHORT_TEST", \
}

#define BASIC_THRESHOLD_SC			{ \
	"RawDataTest_Min", "RawDataTest_Max", \
	"CbTest_Min", "CbTest_Max", \
	"DeltaCbTest_Base", "DeltaCbTest_Differ_Max", \
	"DeltaCbTest_Include_Key_Test", "DeltaCbTest_Key_Differ_Max", \
	"DeltaCbTest_Deviation_S1", "DeltaCbTest_Deviation_S2", "DeltaCbTest_Deviation_S3", \
	"DeltaCbTest_Deviation_S4", "DeltaCbTest_Deviation_S5", "DeltaCbTest_Deviation_S6", \
	"DeltaCbTest_Set_Critical", "DeltaCbTest_Critical_S1", "DeltaCbTest_Critical_S2", \
	"DeltaCbTest_Critical_S3", "DeltaCbTest_Critical_S4", \
	"DeltaCbTest_Critical_S5", "DeltaCbTest_Critical_S6", \
	"Test_Mode1_Freq", "Test_Mode2_Freq", \
	"Test_Mode3_Freq", "Test_Mode4_Freq", \
	"Test_Mode5_Freq", "Test_Mode6_Freq", \
	"Test_Mode1_Vdd", "Test_Mode2_Vdd", \
	"Test_Mode3_Vdd", "Test_Mode4_Vdd", \
	"Test_Mode5_Vdd", "Test_Mode6_Vdd", \
	"Test_Mode1_Cf", "Test_Mode2_Cf", \
	"Test_Mode3_Cf", "Test_Mode4_Cf", \
	"Test_Mode5_Cf", "Test_Mode6_Cf", \
	"Test_Mode1_Tvalue", "Test_Mode2_Tvalue", \
	"Test_Mode3_Tvalue", "Test_Mode4_Tvalue", \
	"Test_Mode5_Tvalue", "Test_Mode6_Tvalue", \
	"RawDataTest_Mode1_Min", "RawDataTest_Mode1_Max", \
	"RawDataTest_Mode2_Min", "RawDataTest_Mode2_Max", \
	"RawDataTest_Mode3_Min", "RawDataTest_Mode3_Max", \
	"RawDataTest_Mode4_Min", "RawDataTest_Mode4_Max", \
	"RawDataTest_Mode5_Min", "RawDataTest_Mode5_Max", \
	"RawDataTest_Mode6_Min", "RawDataTest_Mode6_Max", \
	"CbTest_Mode1_Min", "CbTest_Mode1_Max", \
	"CbTest_Mode2_Min", "CbTest_Mode2_Max", \
	"CbTest_Mode3_Min", "CbTest_Mode3_Max", \
	"CbTest_Mode4_Min", "CbTest_Mode4_Max", \
	"CbTest_Mode5_Min", "CbTest_Mode5_Max", \
	"CbTest_Mode6_Min", "CbTest_Mode6_Max", \
	"ShortTest_Min", "ShortTest_Max", \
}

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
int btl_test_get_testparam_from_ini(char *config_name);
int btl_get_keyword_value(char *section, char *name, int *value);

#define get_value_interface(name, value) \
	btl_get_keyword_value("Interface", name, value)
#define get_value_basic(name, value) \
	btl_get_keyword_value("Basic_Threshold", name, value)
#define get_value_detail(name, value) \
	btl_get_keyword_value("SpecialSet", name, value)
#define get_value_testitem(name, value) \
	btl_get_keyword_value("TestItem", name, value)
#endif /* _INI_H */
