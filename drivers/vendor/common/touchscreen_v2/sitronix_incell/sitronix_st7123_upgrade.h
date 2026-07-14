
//#define ST_DO_WRITE_DISPLAY_AREA
//#define ST_REPLACE_DUMP_BY_DISPLAY_ID


#ifdef ST_DO_WRITE_DISPLAY_AREA
#define ST_DUMP_MAX_LEN	0x20000
#else
#define ST_DUMP_MAX_LEN	0x11000
#endif


#ifdef ST_REPLACE_DUMP_BY_DISPLAY_ID
unsigned char dump_id_1[3] = {0x80,0xA0,0xFB};
#endif /* ST_REPLACE_DUMP_BY_DISPLAY_ID */

#ifdef ST_UPGRADE_USE_REQUESTFW_BUF
unsigned char *dump_buf;
#else
unsigned char dump_buf[] = {0};
#endif