#ifndef __V2R_VIDEO_H
#define __V2R_VIDEO_H

#if defined(CONFIG_V2R_VIDEO_OV2643_SD)
#define V2R_VIDEO_W		640
#define V2R_VIDEO_H		480
#elif defined(CONFIG_V2R_VIDEO_OV2643_HD)
#define V2R_VIDEO_W		1280
#define V2R_VIDEO_H		720
#elif defined(CONFIG_V2R_VIDEO_TVP5150)
#define V2R_VIDEO_W		720
#define V2R_VIDEO_H		576
#elif defined(CONFIG_V2R_VIDEO_ADV7611)
#define V2R_VIDEO_W		1280
#define V2R_VIDEO_H		720
#endif

#endif /* __V2R_VIDEO_H */
