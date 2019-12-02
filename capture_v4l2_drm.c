#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <getopt.h>

#include <sys/epoll.h>
#include <time.h>
/* drm headers */
#include <omap_drm.h>
#include <omap_drmif.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <xf86drmMode.h>

/***********************************
VIDIOC_QUERYCAP	查询设备的属性
VIDIOC_ENUM_FMT 帧格式
VIDIOC_S_FMT 设置视频帧格式，对应struct v4l2_format
VIDIOC_G_FMT 获取视频帧格式等
VIDIOC_REQBUFS 请求/申请若干个帧缓冲区，一般为不少于3个
VIDIOC_QUERYBUF 查询帧缓冲区在内核空间的长度和偏移量
VIDIOC_QBUF 将申请到的帧缓冲区全部放入视频采集输出队列
VIDIOC_STREAMON 开始视频流数据的采集
VIDIOC_DQBUF 应用程序从视频采集输出队列中取出已含有采集数据的帧缓冲区
VIDIOC_STREAMOFF 应用程序将该帧缓冲区重新挂入输入队列
***********************************/

#define CAPTURE_VIDEO_DEV	"/dev/video1"
#define CAPTURE_BUFF_NUM	(4)
#define CAPTURE_WIDTH		(640)
#define CAPTURE_HEIGHT		(480)

#define DRM_DEFAULT_DEV		"/dev/dri/card0"
#define DU_NUM  		  (4)
#define FRAME_BUFFER_NUM  (2)
#define SUPPORT_DRM_PLANE (1)
#define PLANE_NUM		  (4)

typedef unsigned char  uInt8;
typedef unsigned short uInt16;
typedef unsigned int uInt32;
typedef char Int8;
typedef short Int16;
typedef int Int32;

typedef enum
{
	EN_STD_NTSC,
	EN_STD_PAL,
	EN_STD_INVALID,
}enStdType;

typedef enum
{
	EN_CTRL_START,
	EN_CTRL_STOP,
	EN_CTRL_INVALID,
}enCtrlType;

typedef enum
{
	FALSE = 0,
	TRUE = 1
}enBOOL;

typedef struct
{
	void *pBuffer;
	uInt32 buff_size;
}stUserBuff;

typedef struct
{
	uInt32 x;
	uInt32 y;
	uInt32 width;
	uInt32 height;
}stRect;

typedef struct
{
	uInt32 virtualWidth;
	uInt32 virtualHeight;
	uInt32 stride;
	uInt32 size;
	uInt32 handle;
	uInt8 *pMapAddr;
	uInt32 fbId;
}stFrameBufferInfo;

typedef struct
{
	uInt32 width;
	uInt32 height;
	Int32 crtcId;
	Int32 fourcc;
	uInt32 curFbIdx;
	enBOOL duValid;
	stFrameBufferInfo frameBuffers[FRAME_BUFFER_NUM];
	drmModeModeInfoPtr pDrmMode;
	drmModeConnector *pDrmConnector;
}stDisplayUnit;

typedef struct
{
	enBOOL init;
	Int32 drmFd;
	drmModeResPtr pDrmResources;
	stDisplayUnit dispUnits[DU_NUM];
	void* usrData;
	
	#if SUPPORT_DRM_PLANE
	Int32 planeIds[PLANE_NUM];
	#endif
}stDrmInfo;

#define CHECK_POINTER_VALID(P, V) \
	do{ \
		if(NULL == P){ \
			DRM_LOG_ERR("Pointer: %s is NULL!", #P); \
			return V; \
		} \
	}while(0)


/* log color */
#define LOG_COL_NONE
#define LOG_COL_END         "\033[0m"
#define LOG_COL_BLK  	"\033[0;30m" /* black */
#define LOG_COL_RED  	"\033[0;31m" /* red */
#define LOG_COL_GRN  	"\033[0;32m" /* green */
#define LOG_COL_YLW  	"\033[0;33m" /* yellow */
#define LOG_COL_BLU  	"\033[0;34m" /* blue */
#define LOG_COL_PUR  	"\033[0;35m" /* purple */
#define LOG_COL_CYN  	"\033[0;36m" /* cyan */
#define LOG_COL_WHI  	"\033[0;37m"
#define LOG_COL_RED_BLK "\033[0;31;40m"
#define LOG_COL_RED_YLW "\033[0;31;43m"
#define LOG_COL_RED_WHI "\033[0;31;47m"
#define LOG_COL_GRN_BLK "\033[0;32;40m"
#define LOG_COL_YLW_BLK "\033[0;33;40m"
#define LOG_COL_YLW_GRN "\033[1;33;42m"
#define LOG_COL_YLW_PUR "\033[0;33;45m"
#define LOG_COL_WHI_GRN "\033[0;37;42m"
		
/* log level */
typedef enum{
	LOG_LEVEL_ERR,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_NOTIFY,
	LOG_LEVEL_NONE
}enLogLevel;

void _LogOut_(int Level, const char Format[], ...) __attribute__((format(printf,2,3)));
		
#define LOG_OUTPUT(_level_, _fmt_, ...) _LogOut_(_level_, _fmt_, ##__VA_ARGS__)
		
#define LOG_ERR(_fmt_, ...)		LOG_OUTPUT(LOG_LEVEL_ERR, _fmt_, ##__VA_ARGS__)
#define LOG_WARNING(_fmt_, ...)	LOG_OUTPUT(LOG_LEVEL_WARNING, _fmt_, ##__VA_ARGS__)
#define LOG_NOTIFY(_fmt_, ...)	LOG_OUTPUT(LOG_LEVEL_NOTIFY, _fmt_, ##__VA_ARGS__)
		
#if 1
#define DRM_LOG_ERR(msg, ...)	    LOG_ERR(LOG_COL_RED_YLW "[DRM_ERR]"msg LOG_COL_END"\n", ##__VA_ARGS__)
#define DRM_LOG_WARNING(msg, ...)	LOG_WARNING(LOG_COL_RED_WHI "[DRM_WARNING]"msg LOG_COL_END"\n", ##__VA_ARGS__)
#define DRM_LOG_NOTIFY(msg, ...)	LOG_NOTIFY(LOG_COL_NONE "[DRM_NOTIFY]"msg LOG_COL_NONE"\n", ##__VA_ARGS__)
#else
#define DRM_LOG_ERR
#define DRM_LOG_WARNING
#define DRM_LOG_NOTIFY
#endif
		
/* log out put */
void _LogOut_(int Level, const char Format[], ...)
{
	if(Level >= LOG_LEVEL_NONE) return;
	char *pBuffer = NULL;
	char buffer[256] = {'\0'};
	pBuffer = &buffer[0];
	
	va_list  ArgPtr;
	va_start(ArgPtr, Format);
	vsnprintf(pBuffer, 255, Format, ArgPtr);
	va_end(ArgPtr);

	printf(buffer);
	return;
}


static stUserBuff capture_usr_buffs[CAPTURE_BUFF_NUM];
static Int32      capture_dev_fd = -1;
static uInt8 	  capture_tmp_buff[CAPTURE_WIDTH * CAPTURE_HEIGHT * 4];

static Int32 	  drm_current_du = 0;
static Int32 	  drm_current_fmt = DRM_FORMAT_XRGB8888;
static char*	  drm_current_card = DRM_DEFAULT_DEV;
static Int32	  drm_overlay_plane_id = -1;

//yuyv转rgb32的算法实现  
/*
YUV到RGB的转换有如下公式： 
R = 1.164*(Y-16) + 1.159*(V-128); 
G = 1.164*(Y-16) - 0.380*(U-128)+ 0.813*(V-128); 
B = 1.164*(Y-16) + 2.018*(U-128)); 
*/
Int32 yuvtorgb(uInt8 y, uInt8 u, uInt8 v, uInt8 calc)
{
     unsigned int pixel32 = 0;
     unsigned char *pixel = (unsigned char *)&pixel32;
     int r, g, b;
     static long int ruv, guv, buv;

     if(1 == calc) {
         ruv = 1159*(v-128);
         guv = -380*(u-128) + 813*(v-128);
         buv = 2018*(u-128);
     }

     r = (1164*(y-16) + ruv) / 1000;
     g = (1164*(y-16) - guv) / 1000;
     b = (1164*(y-16) + buv) / 1000;

     if(r > 255) r = 255;
     if(g > 255) g = 255;
     if(b > 255) b = 255;
     if(r < 0) r = 0;
     if(g < 0) g = 0;
     if(b < 0) b = 0;

     pixel[0] = r;
     pixel[1] = g;
     pixel[2] = b;

     return pixel32;
}

Int32 yuyv2rgb32(uInt8 *yuv, uInt8 *rgb, uInt32 width, uInt32 height)
{
     uInt32 in, out;
     uInt8 y0, u, y1, v;
     uInt32 pixel32;
     uInt8 *pixel = (unsigned char *)&pixel32;
     //分辨率描述像素点个数，而yuv2个像素点占有4个字符，所以这里计算总的字符个数，需要乘2
     unsigned int size = width*height*2; 

     for(in = 0, out = 0; in < size; in += 4, out += 8) {
          y0 = yuv[in+0];
          u  = yuv[in+1];
          y1 = yuv[in+2];
          v  = yuv[in+3];

          pixel32 = yuvtorgb(y0, u, v, 1);
          rgb[out+0] = pixel[2]; // b  
          rgb[out+1] = pixel[1]; // g
          rgb[out+2] = pixel[0]; // r
          rgb[out+3] = 0;  //32位rgb多了一个保留位

          pixel32 = yuvtorgb(y1, u, v, 0);
          rgb[out+4] = pixel[2];
          rgb[out+5] = pixel[1];
          rgb[out+6] = pixel[0];
          rgb[out+7] = 0;

     }
     return 0;
}

Int32 capture_open_dev(const char* dev)
{
	return open(dev, O_RDWR);
}

Int32 capture_query_cap(Int32 fd)
{
	struct v4l2_capability cap;
	ioctl(fd, VIDIOC_QUERYCAP, &cap);
	printf("DriverName:%s\nCard Name:%s\nBus info:%s\nDriverVersion:%u.%u.%u\n",
		cap.driver, cap.card, cap.bus_info, 
		(cap.version >> 16) & 0XFF, (cap.version >> 8) & 0XFF, cap.version & 0xFF);

	
	// 是否支持视频捕获或者支持输入输出流控制
 	if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == V4L2_CAP_VIDEO_CAPTURE) {
        printf("Device %s: supports capture.\n", CAPTURE_VIDEO_DEV);
	}
	else if ((cap.capabilities & V4L2_CAP_STREAMING) == V4L2_CAP_STREAMING) {
        printf("Device %s: supports streaming.\n", CAPTURE_VIDEO_DEV);
	}
	else {
		printf("Device %s: not supports capture or streaming.\n", CAPTURE_VIDEO_DEV);
		return -1;
	}
	return 0;
}

void capture_show_fmts(Int32 fd)
{
	struct v4l2_fmtdesc fmtdesc;
	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	printf("Supportformat:\n");
	while(ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != -1){
		printf("/t[%d] format: 0x%x <%s>\n", fmtdesc.index+1, fmtdesc.pixelformat, fmtdesc.description);
		fmtdesc.index++;
	}
}

void capture_current_fmt(Int32 fd)
{
	struct v4l2_format fmt;	
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	Int32 ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		printf( "capture: G_FMT after set format failed: %s\n", strerror(errno));

	printf("capture: G_FMT(start): width = %u, height = %u, 4cc = %.4s\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height,
			(char*)&fmt.fmt.pix.pixelformat);
}

void capture_check_fmt(Int32 fd, Int32 pixel_fmt)
{
	struct v4l2_format fmt;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = pixel_fmt;
	if(ioctl(fd, VIDIOC_TRY_FMT, &fmt) == -1) {
		printf("capture not support fmt: 0x%x\n", pixel_fmt);
	}
}

enStdType capture_get_standard(Int32 fd)
{
	v4l2_std_id std;
	enStdType std_type = EN_STD_INVALID;
	if(ioctl(fd, VIDIOC_G_STD, &std) < 0) {
		printf("get standard fail!\n");
		return EN_STD_INVALID;
	}

	switch(std) {
		case V4L2_STD_NTSC:
		{
			std_type = EN_STD_NTSC;
			printf("support NTSC standard.\n");
		}break;
		case V4L2_STD_PAL:
		{
			std_type = EN_STD_PAL;
			printf("support PAL standard.\n");
		}break;
		default:
			break;
	}
	return std_type;
}

Int32 capture_set_format(Int32 fd, Int32 width, Int32 height, Int32 pixel_fmt)
{
	Int32 ret;
	struct v4l2_format fmt;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = pixel_fmt;
	//fmt.fmt.pix.field = V4L2_FIELD_ALTERNATE;

	ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0)
		printf( "capture: S_FMT failed: %s\n", strerror(errno));

	capture_current_fmt(fd);
	return 0;
}

Int32 capture_set_crop(Int32 fd, enStdType std)
{
	struct v4l2_crop crop;
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	switch(std) 
	{
		case EN_STD_NTSC:
		{
			crop.c.left = 0;
			crop.c.top = 13;
			crop.c.width = 720;
			crop.c.height = 480;
		}break;
		case EN_STD_PAL:
		{
			crop.c.left = 0;
			crop.c.top = 0;
			crop.c.width = 720;
			crop.c.height = 576;
		}break;
		default:
			break;
	}

	if(std != EN_STD_INVALID) {
		if (ioctl(fd, VIDIOC_S_CROP, &crop) < 0) {
			printf("fail to set crop\n");
			return -1;
		}
	}
	return 0;
}

Int32 capture_request_buffers(Int32 fd, Int32 mem_num)
{
	struct v4l2_requestbuffers req;
	req.count = mem_num;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	int ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if(ret < 0) {
		printf("request %d buffers fail!\n", mem_num);
		return -1;
	}

	return 0;
}

Int32 capture_query_buffers(Int32 fd, Int32 mem_num, stUserBuff *pUserBuff)
{
	Int32 i = 0;
	struct v4l2_buffer buf;
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	for (i = 0; i < mem_num; ++i) {
		buf.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
			printf("fail to query buffer\n");
			return -1;
		}

		pUserBuff[i].pBuffer = mmap(0, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
		pUserBuff[i].buff_size = buf.length;
		printf("Query Buff: 0x%x, size = %d\n", pUserBuff[i].pBuffer, buf.length);
		
		if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			printf("fail to put buffer into queue\n");
			return -1;
		}
	}
	return 0;
}

Int32 capture_control(Int32 fd, enCtrlType ctrl)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ctrl == EN_CTRL_START) {
		if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
			printf("fail to start video input\n");
			return -1;
		}
	}
	else {
		if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
			printf("fail to stop video input\n");
			return -1;
		}
	}
	return 0;
}

Int32 capture_dump_data(Int32 fd, Int32 cnt)
{
	Int32 ret = -1;
	struct v4l2_buffer dequeue_buf, queue_buf;
	dequeue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dequeue_buf.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_DQBUF, &dequeue_buf);
	if(ret < 0) {
		printf("dqueue buffer fail!\n");
		return -1;
	}

	char file[128] = {'\0'};
	sprintf(file, "./capture%02d.uyvy", cnt);
	FILE *pf = fopen(file, "wb");
	fwrite(capture_usr_buffs[dequeue_buf.index].pBuffer, dequeue_buf.length, 1, pf);
	fclose(pf);
	printf("Dump: %s, buff index = %d, len = %d\n", file, dequeue_buf.index, dequeue_buf.length);

	queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue_buf.memory = V4L2_MEMORY_MMAP;
	queue_buf.index = dequeue_buf.index;
	ret = ioctl(fd, VIDIOC_QBUF, &queue_buf);
	if(ret < 0) {
		printf("queue buffer fail!\n");
		return -1;
	}

	return 0;
}

Int32 capture_get_buff(Int32 fd)
{
	Int32 ret = -1;
	struct v4l2_buffer dequeue_buf;
	dequeue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dequeue_buf.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_DQBUF, &dequeue_buf);
	if(ret < 0) {
		printf("dqueue buffer fail!\n");
		return -1;
	}
	capture_usr_buffs[dequeue_buf.index].buff_size = dequeue_buf.length;
	return dequeue_buf.index;
}

Int32 capture_put_buff(Int32 fd, Int32 buff_idx)
{
	Int32 ret = -1;
	struct v4l2_buffer queue_buf;
	queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue_buf.memory = V4L2_MEMORY_MMAP;
	queue_buf.index = buff_idx;
	ret = ioctl(fd, VIDIOC_QBUF, &queue_buf);
	if(ret < 0) {
		printf("queue buffer fail!\n");
		return -1;
	}
	return 0;
}

static void epollAddEvent(Int32 epollFd, Int32 addFd, Int32 state)
{
	struct epoll_event ev;
	ev.events = state;
	ev.data.fd = addFd;
	epoll_ctl(epollFd, EPOLL_CTL_ADD, addFd, &ev);
	return;
}

// 将src内容拷贝到dst的指定区域
static void fillDataToRect(uInt8 *pDstAddr, uInt32 dstWidth, uInt8 *pSrcAddr, stRect *pRect, uInt8 pixBytes)
{
	uInt8 *pDstStartPos = NULL, *pSrcStartPos = NULL;
	uInt32 i = 0;
	uInt32 dst_stride = dstWidth * pixBytes;
	uInt32 src_stride = pRect->width * pixBytes;
	pDstStartPos = pDstAddr+ pRect->y * dst_stride + pRect->x * pixBytes;
	pSrcStartPos = pSrcAddr;
	for (i = 0; i < pRect->height; ++i) {
	    memcpy(pDstStartPos, pSrcStartPos, src_stride);
	    pDstStartPos += dst_stride;
		pSrcStartPos += src_stride;
	}
}

/* return ms */
Int32 calcProcessTime(Int32 *pStartMs)
{
	struct timespec endTime;
	clock_gettime(CLOCK_REALTIME, &endTime);
	if(pStartMs){
		return endTime.tv_sec * 1000 + endTime.tv_nsec / 1000000 - *pStartMs;
	}
	return endTime.tv_sec * 1000 + endTime.tv_nsec / 1000000;
}

/* Show frames per second */
static void showFps(void)
{
    static Int32 framecount = 0;
    static Int32 lastframecount = 0;
    static Int32 lastfpstime = 0;
    static float fps = 0;

	struct timespec endTime;
	clock_gettime(CLOCK_REALTIME, &endTime);

    framecount++;
    if (!(framecount & 0x7)) {
        Int32 now = endTime.tv_sec * 1000000000 + endTime.tv_nsec;
        Int32 diff = now - lastfpstime;
        fps = ((framecount - lastframecount) * (float)(1000000000)) / diff;
        lastfpstime = now;
        lastframecount = framecount;
        DRM_LOG_WARNING("%d Frames, %f FPS", framecount, fps);
    }
	return;
}

static void drmInfoClear(stDrmInfo *pDrmInfo)
{
	//CHECK_POINTER_VALID(pDrmInfo, 0);
	pDrmInfo->drmFd = -1;
	pDrmInfo->pDrmResources = NULL;
	uInt32 i = 0;
	for(i = 0; i < DU_NUM; i++){
		pDrmInfo->dispUnits[i].width = 0;
		pDrmInfo->dispUnits[i].height = 0;
		pDrmInfo->dispUnits[i].crtcId = -1;
		pDrmInfo->dispUnits[i].fourcc = drm_current_fmt;
		pDrmInfo->dispUnits[i].curFbIdx = 0;
		pDrmInfo->dispUnits[i].duValid = FALSE;
		pDrmInfo->dispUnits[i].pDrmConnector = NULL;
		pDrmInfo->dispUnits[i].pDrmMode = NULL;
		memset(&pDrmInfo->dispUnits[i].frameBuffers, 0, sizeof(stFrameBufferInfo));
	}
	return;
}


static Int32 drmFormat2Bpp(Int32 fourcc)
{
    Int32 bpp;
    switch (fourcc) {
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
    case DRM_FORMAT_NV16:
    case DRM_FORMAT_NV61:
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
        bpp = 8;
        break;

    case DRM_FORMAT_ARGB4444:
    case DRM_FORMAT_XRGB4444:
    case DRM_FORMAT_ABGR4444:
    case DRM_FORMAT_XBGR4444:
    case DRM_FORMAT_RGBA4444:
    case DRM_FORMAT_RGBX4444:
    case DRM_FORMAT_BGRA4444:
    case DRM_FORMAT_BGRX4444:
    case DRM_FORMAT_ARGB1555:
    case DRM_FORMAT_XRGB1555:
    case DRM_FORMAT_ABGR1555:
    case DRM_FORMAT_XBGR1555:
    case DRM_FORMAT_RGBA5551:
    case DRM_FORMAT_RGBX5551:
    case DRM_FORMAT_BGRA5551:
    case DRM_FORMAT_BGRX5551:
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_BGR565:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_VYUY:
    case DRM_FORMAT_YUYV:
    case DRM_FORMAT_YVYU:
        bpp = 16;
        break;

    case DRM_FORMAT_BGR888:
    case DRM_FORMAT_RGB888:
        bpp = 24;
        break;

    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_RGBX1010102:
    case DRM_FORMAT_BGRA1010102:
    case DRM_FORMAT_BGRX1010102:
        bpp = 32;
        break;

    default:
        DRM_LOG_WARNING( "unsupported format 0x%08x", fourcc);
        return -1;
    }
    return bpp;
}

static uInt32 drmFormat2height(Int32 fourcc, uInt32 height)
{
    uInt32 virtual_height;
    switch (fourcc) {
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
        virtual_height = height * 3 / 2;
        break;

    case DRM_FORMAT_NV16:
    case DRM_FORMAT_NV61:
        virtual_height = height * 2;
        break;

    default:
        virtual_height = height;
        break;
    }
    return virtual_height;
}

static Int32 drmFormat2Handle(
	Int32 fourcc,
	uint32_t *handles,
	uint32_t *pitches,
	uint32_t *offsets,
	uint32_t handle,
	uint32_t pitch,
	uint32_t height,
	uint32_t offset_base)
{
    switch (fourcc) {
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_VYUY:
    case DRM_FORMAT_YUYV:
    case DRM_FORMAT_YVYU:
        offsets[0] = offset_base;
        handles[0] = handle;
        pitches[0] = pitch;
        break;

    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
    case DRM_FORMAT_NV16:
    case DRM_FORMAT_NV61:
        offsets[0] = offset_base;
        handles[0] = handle;
        pitches[0] = pitch;
        pitches[1] = pitches[0];
        offsets[1] = offsets[0] + pitches[0] * height;
        handles[1] = handle;
        break;

    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
        offsets[0] = offset_base;
        handles[0] = handle;
        pitches[0] = pitch;
        pitches[1] = pitches[0] / 2;
        offsets[1] = offsets[0] + pitches[0] * height;
        handles[1] = handle;
        pitches[2] = pitches[1];
        offsets[2] = offsets[1] + pitches[1] * height / 2;
        handles[2] = handle;
        break;

    case DRM_FORMAT_ARGB4444:
    case DRM_FORMAT_XRGB4444:
    case DRM_FORMAT_ABGR4444:
    case DRM_FORMAT_XBGR4444:
    case DRM_FORMAT_RGBA4444:
    case DRM_FORMAT_RGBX4444:
    case DRM_FORMAT_BGRA4444:
    case DRM_FORMAT_BGRX4444:
    case DRM_FORMAT_ARGB1555:
    case DRM_FORMAT_XRGB1555:
    case DRM_FORMAT_ABGR1555:
    case DRM_FORMAT_XBGR1555:
    case DRM_FORMAT_RGBA5551:
    case DRM_FORMAT_RGBX5551:
    case DRM_FORMAT_BGRA5551:
    case DRM_FORMAT_BGRX5551:
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_BGR565:
    case DRM_FORMAT_BGR888:
    case DRM_FORMAT_RGB888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_RGBX1010102:
    case DRM_FORMAT_BGRA1010102:
    case DRM_FORMAT_BGRX1010102:
        offsets[0] = offset_base;
        handles[0] = handle;
        pitches[0] = pitch;
        break;
	default:
		DRM_LOG_WARNING("Invalid format!!!!");
		break;
    }
    return 0;
}

static Int32 drmCreateOneFrameBuffer(
	Int32 drmFd,
	Int32 fourcc,
	uInt32 width,
	uInt32 height,
	stFrameBufferInfo *pFBInfo)
{
	CHECK_POINTER_VALID(pFBInfo, -1);
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	/*create dumb buffer */
	memset(&creq, 0, sizeof(creq));
	pFBInfo->virtualWidth = creq.width = width;
	pFBInfo->virtualHeight = creq.height = drmFormat2height(fourcc, height);
	creq.bpp = drmFormat2Bpp(fourcc);
	DRM_LOG_NOTIFY("FBInfo: fourcc: %d; virtual w , h = %d, %d; bpp = %d", fourcc, pFBInfo->virtualWidth, pFBInfo->virtualHeight, creq.bpp);
	if(drmIoctl(drmFd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0){
		DRM_LOG_ERR("%s()->drmIoctl() Fail!!, line: %d, errorNo = %d", __FUNCTION__, __LINE__, errno);
		return -1;
	}
	pFBInfo->stride = creq.pitch;
	pFBInfo->size = creq.size;
	pFBInfo->handle = creq.handle;

	/* create framebuffer object for the dumb-buffer */
	drmFormat2Handle(fourcc, handles, pitches, offsets, pFBInfo->handle, pFBInfo->stride, height, 0);
	if(drmModeAddFB2(drmFd, width, height, fourcc, handles, pitches, offsets, &pFBInfo->fbId, 0) < 0){
		DRM_LOG_ERR("%s()->drmIoctl() Fail!!, line: %d, errorNo = %d", __FUNCTION__, __LINE__, errno);
		goto DESTORY_CREATE_DUMB;
	}

	/* create buffer for memory mapping */
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = pFBInfo->handle;
	if(drmIoctl(drmFd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0){
		DRM_LOG_ERR("%s()->drmIoctl() Fail!!, line: %d, errorNo = %d", __FUNCTION__, __LINE__, errno);
		goto REMOVE_FB;
	}

	pFBInfo->pMapAddr = NULL;
	pFBInfo->pMapAddr = (uInt8*)mmap(NULL, pFBInfo->size, PROT_READ | PROT_WRITE, MAP_SHARED, drmFd, mreq.offset);
	if(pFBInfo->pMapAddr == MAP_FAILED){
		DRM_LOG_ERR("%s()->mmap() Fail!!, line: %d, errorNo = %d", __FUNCTION__, __LINE__, errno);
		goto REMOVE_FB;
	}

	/* mreq.offset 是64位 */
	DRM_LOG_WARNING("Offset = 0x%llx, mapAddr = %p, stride = %d, size = %d", mreq.offset, (void*)pFBInfo->pMapAddr, pFBInfo->stride, pFBInfo->size);
	/* clear map buffer */
	memset(pFBInfo->pMapAddr, 0, pFBInfo->size);
	
	return 0;
	
REMOVE_FB:
	drmModeRmFB(drmFd, pFBInfo->fbId);
DESTORY_CREATE_DUMB:
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = pFBInfo->handle;
	drmIoctl(drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	return -1;
}

static void drmDestoryOneFrameBuffer(Int32 drmFd, stFrameBufferInfo *pFBInfo)
{
	//CHECK_POINTER_VALID(pFBInfo, 0);
	struct drm_mode_destroy_dumb dreq;
	/* unmap buffer */
	munmap(pFBInfo->pMapAddr, pFBInfo->size);

	/* delete frame buffer */
	drmModeRmFB(drmFd, pFBInfo->fbId);

	/* delete dump buffer */
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = pFBInfo->handle;
	drmIoctl(drmFd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	return;
}

static enBOOL drmCheckCrtcHasUsed(stDrmInfo *pDrmInfo, uInt32 curDuIdx, Int32 crtcId)
{
	uInt32 i = 0;
	for(i = 0; i < curDuIdx; i++){
		if(pDrmInfo->dispUnits[i].crtcId == crtcId){
			return TRUE;
		}
	}
	return FALSE;
}

static Int32 drmFindCrtc(stDrmInfo *pDrmInfo, uInt32 curDuIdx)
{
	CHECK_POINTER_VALID(pDrmInfo, -1);
	stDisplayUnit *pCurDu = &pDrmInfo->dispUnits[curDuIdx];
	drmModeEncoder *pDrmEnc = NULL;
	uInt32 i = 0, j = 0;
	/* First try the currently connector */
	if(pCurDu->pDrmConnector->encoder_id){
		pDrmEnc = drmModeGetEncoder(pDrmInfo->drmFd, pCurDu->pDrmConnector->encoder_id);
	}
	else{
		pDrmEnc = NULL;
	}

	if(pDrmEnc) {
		if(pDrmEnc->crtc_id){
			/*Current crtc id is not used by other display unit*/
			if(!drmCheckCrtcHasUsed(pDrmInfo, curDuIdx, pDrmEnc->crtc_id)){
				pCurDu->crtcId = pDrmEnc->crtc_id;
				DRM_LOG_NOTIFY("(1) Find Crtc success, crtc id = %d", pDrmEnc->crtc_id);
				drmModeFreeEncoder(pDrmEnc);
				pDrmEnc = NULL;
				return 0;
			}
		}
		/* current encoder has no valid crtc or crtc has been used by other display unit */
		drmModeFreeEncoder(pDrmEnc);
		pDrmEnc = NULL;
	}
	
	/* iterate all encoders of a connector */
	for(i = 0; i < pCurDu->pDrmConnector->count_encoders; i++){
		pDrmEnc = drmModeGetEncoder(pDrmInfo->drmFd, pCurDu->pDrmConnector->encoders[i]);
		if(!pDrmEnc){
			DRM_LOG_WARNING("[%d] cannot get encoder id = %d", pCurDu->pDrmConnector->encoders[i]);
			continue;
		}
		
		/* iterate all crtcs of an encoder */
		for(j = 0; j < pDrmInfo->pDrmResources->count_crtcs; j++){
			if(pDrmEnc->possible_crtcs & (1 << j)){
				Int32 crtcId = pDrmInfo->pDrmResources->crtcs[j];
				if(!drmCheckCrtcHasUsed(pDrmInfo, curDuIdx, crtcId)){
					pCurDu->crtcId = pDrmEnc->crtc_id;
					DRM_LOG_NOTIFY("(2) Find Crtc success, crtc id = %d", pDrmEnc->crtc_id);
					drmModeFreeEncoder(pDrmEnc);
					pDrmEnc = NULL;
					return 0;
				}
			}
		}
		drmModeFreeEncoder(pDrmEnc);
		pDrmEnc = NULL;
	}
	DRM_LOG_WARNING("Cannot find suitable crtc for connector!!!");
	return -1;
}

static Int32 drmSetCrtc(Int32 drmFd, stDisplayUnit *pDispUnit)
{
	CHECK_POINTER_VALID(pDispUnit, -1);
	uInt32 i = 0;
	Int32 ret = -1;
	ret = drmModeSetCrtc(drmFd,
					  pDispUnit->crtcId, 
					  pDispUnit->frameBuffers[0].fbId,
					  0,
					  0,
					  &pDispUnit->pDrmConnector->connector_id,
					  1,
					  pDispUnit->pDrmMode
					  );
	printf("SetCtrc； drmfd = %d, crtc = %d, fb = %d, connId= %d\n",
			drmFd, pDispUnit->crtcId, pDispUnit->frameBuffers[0].fbId, pDispUnit->pDrmConnector->connector_id);
	if(ret){
		DRM_LOG_ERR("Cannot set crtc for connector: %d, crtcID = %d, ret = %d!", pDispUnit->pDrmConnector->connector_id, pDispUnit->crtcId, ret);
		return -1;
	}
	return 0;
}

static void drawCaptureToDu(Int32 drmFd, stDisplayUnit *pDu)
{
	Int32 ret = -1;
	stRect rect = {640, 300, CAPTURE_WIDTH, CAPTURE_HEIGHT};
	stFrameBufferInfo *pfb = &pDu->frameBuffers[pDu->curFbIdx];
	uInt32 bitPixel = pfb->stride / pfb->virtualWidth;
	
	Int32 buff_idx = capture_get_buff(capture_dev_fd);
	if(buff_idx >= 0) {
		if(drm_current_fmt == DRM_FORMAT_XRGB8888) {
			yuyv2rgb32(capture_usr_buffs[buff_idx].pBuffer, capture_tmp_buff, CAPTURE_WIDTH, CAPTURE_HEIGHT);
		}
		
		fillDataToRect(pfb->pMapAddr, pDu->width, capture_tmp_buff, &rect, bitPixel);
		capture_put_buff(capture_dev_fd, buff_idx);
	}
	else {
		return;
	}

	ret = drmModePageFlip(drmFd, pDu->crtcId, pfb->fbId, DRM_MODE_PAGE_FLIP_EVENT, pDu);
	if (ret) {
		DRM_LOG_ERR("cannot flip CRTC for connector %u : %d", pDu->pDrmConnector->connector_id, errno);
	}
	else{
		pDu->curFbIdx ^= 1;
	}
	//DRM_LOG_NOTIFY("Flip crtc id = %d, curFbIdx = %d", pDu->crtcId, pDu->curFbIdx);
	showFps();


	#if SUPPORT_DRM_PLANE
	stRect src_rect = {640, 300, CAPTURE_WIDTH, CAPTURE_HEIGHT};
	stRect dst_rect = {100, 100, CAPTURE_WIDTH, CAPTURE_HEIGHT};
	drmModeSetPlane(drmFd, drm_overlay_plane_id, 
					pDu->crtcId,
					pfb->fbId, 0,
					dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height,
					src_rect.x << 16, src_rect.y << 16, src_rect.width << 16, src_rect.height << 16);
	
	#endif
	
	return;
}

static void pageFlipEvent(
	Int32 drmFd, 
	uInt32 frame,
	uInt32 sec,
	uInt32 usec,
	void *data)
{
	stDisplayUnit *pDu = (stDisplayUnit*)data;
	drawCaptureToDu(drmFd, pDu);
	return;
}

Int32 drm_init(stDrmInfo *pDrmInfo, const char* dev)
{
	CHECK_POINTER_VALID(pDrmInfo, -1);
	uInt32 i = 0, j = 0;
	uint64_t hasDumb;
	drmModeConnectorPtr drmConn = NULL;
	if(pDrmInfo->init) return 0;
	drmInfoClear(pDrmInfo);
	/* open drm device */
	pDrmInfo->drmFd = open(dev, O_RDWR | O_CLOEXEC);
	
	if (pDrmInfo->drmFd < 0) {
    	DRM_LOG_ERR("Failed to open omapdrm device");
    	return -1;
	}

	if (drmGetCap(pDrmInfo->drmFd, DRM_CAP_DUMB_BUFFER, &hasDumb) < 0 ||!hasDumb) {
		DRM_LOG_ERR("drm device '%s' does not support dumb buffers", dev);
		drmClose(pDrmInfo->drmFd);
		pDrmInfo->drmFd = -1;
		return -1;
	}

	DRM_LOG_NOTIFY("Open Drm %s success; fd: %d", dev, pDrmInfo->drmFd);

	/* get drm resources */
	pDrmInfo->pDrmResources = drmModeGetResources(pDrmInfo->drmFd);
	if (!pDrmInfo->pDrmResources) {
		DRM_LOG_ERR("Failed to get drm resources: %s", strerror(errno));
		goto FAIL;
	}

	#if SUPPORT_DRM_PLANE
	drmModePlaneRes *plane_res;
	drmSetClientCap(pDrmInfo->drmFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	plane_res = drmModeGetPlaneResources(pDrmInfo->drmFd);
	DRM_LOG_NOTIFY("plane_res->count_planes = %d", plane_res->count_planes);
	for(i = 0; i < plane_res->count_planes; i++) {
		pDrmInfo->planeIds[i] = plane_res->planes[i];
		DRM_LOG_NOTIFY("[%d] plane id: %d", i, plane_res->planes[i]);
	}
	#endif

	/* get connectors */
	DRM_LOG_NOTIFY("pDrmInfo->pDrmResources->count_connectors = %d", pDrmInfo->pDrmResources->count_connectors);
	for (i = 0; i < pDrmInfo->pDrmResources->count_connectors; i++) {
        drmConn = drmModeGetConnector(pDrmInfo->drmFd, pDrmInfo->pDrmResources->connectors[i]);
		if(!drmConn) continue;
		/* check if a monitor is connected Or if there is at least one valid mode*/
		if((drmConn->connection != DRM_MODE_CONNECTED) || (drmConn->count_modes == 0)){
			DRM_LOG_WARNING("[%d]Connector ID: %d is unused connector[%d] or has no valid mode[%d].",
				drmConn->connector_id, drmConn->connection, drmConn->count_modes);
			drmModeFreeConnector(drmConn);
			drmConn = NULL;
			continue;
		}

		pDrmInfo->dispUnits[i].width = drmConn->modes[0].hdisplay;
		pDrmInfo->dispUnits[i].height = drmConn->modes[0].vdisplay;
		pDrmInfo->dispUnits[i].pDrmConnector = drmConn;
		pDrmInfo->dispUnits[i].pDrmMode = &drmConn->modes[0];
        
		DRM_LOG_NOTIFY("[%d]Connector ID: %d, ConnectType = %d, [w, h] = [%d, %d]",
			i, drmConn->connector_id, drmConn->connector_type, 
			drmConn->modes[0].hdisplay, drmConn->modes[0].vdisplay);

		pDrmInfo->dispUnits[i].crtcId = -1;
		if(drmFindCrtc(pDrmInfo, i) < 0){
			DRM_LOG_WARNING("drmFindCrtc Fail!!");
			drmModeFreeConnector(pDrmInfo->dispUnits[i].pDrmConnector);
			pDrmInfo->dispUnits[i].pDrmConnector = NULL;
			pDrmInfo->dispUnits[i].pDrmMode = NULL;
			continue;
		}
		DRM_LOG_NOTIFY("drmFindCrtc() success!!");

		for(j = 0; j < FRAME_BUFFER_NUM; j++){
			if(drmCreateOneFrameBuffer(pDrmInfo->drmFd, 
									  pDrmInfo->dispUnits[i].fourcc, 
									  pDrmInfo->dispUnits[i].width,
									  pDrmInfo->dispUnits[i].height,
									  &pDrmInfo->dispUnits[i].frameBuffers[j]) < 0){
				DRM_LOG_ERR("Drm create frame buffer %d fail!!!", j);
				for(; j >= 0; j--){
					drmDestoryOneFrameBuffer(pDrmInfo->drmFd, &pDrmInfo->dispUnits[i].frameBuffers[j]);
				}
				goto FAIL;
			}
		}

		DRM_LOG_NOTIFY("drm create frame buffers success!!");

		if(drmSetCrtc(pDrmInfo->drmFd, &pDrmInfo->dispUnits[i]) < 0){
			DRM_LOG_WARNING("drmSetCrtc Fail!!");
			for(j = 0; j < FRAME_BUFFER_NUM; j++){
				drmDestoryOneFrameBuffer(pDrmInfo->drmFd, &pDrmInfo->dispUnits[i].frameBuffers[j]);
			}
			goto FAIL;
		}

		DRM_LOG_WARNING("DU: %d drmSetCrtc Success!!", i);
		pDrmInfo->dispUnits[i].duValid = TRUE;
		drm_current_du = i;
		drmConn = NULL;
    }
	pDrmInfo->init = TRUE;
	return 0;
FAIL:

	for (i = 0; i < pDrmInfo->pDrmResources->count_connectors; i++) {
		if(pDrmInfo->dispUnits[i].pDrmConnector){
			drmModeFreeConnector(pDrmInfo->dispUnits[i].pDrmConnector);
			pDrmInfo->dispUnits[i].pDrmConnector = NULL;
		}
	}

	if(pDrmInfo->pDrmResources){
		drmModeFreeResources(pDrmInfo->pDrmResources);
		pDrmInfo->pDrmResources = NULL;
	}

	if(pDrmInfo->drmFd){
		drmClose(pDrmInfo->drmFd);
		pDrmInfo->drmFd = -1;
	}
	
	return -1;
}

void drm_deinit(stDrmInfo *pDrmInfo)
{
	uInt32 i = 0, j = 0;
	for(i = 0; i < pDrmInfo->pDrmResources->count_connectors; i++){
		for(j = 0; j < FRAME_BUFFER_NUM; j++){
			drmDestoryOneFrameBuffer(pDrmInfo->drmFd, &pDrmInfo->dispUnits[i].frameBuffers[j]);
		}
		drmModeFreeConnector(pDrmInfo->dispUnits[i].pDrmConnector);
		pDrmInfo->dispUnits[i].pDrmConnector = NULL;
	}
	if(pDrmInfo->pDrmResources){
		drmModeFreeResources(pDrmInfo->pDrmResources);
		pDrmInfo->pDrmResources = NULL;
	}

	if(pDrmInfo->drmFd){
		drmClose(pDrmInfo->drmFd);
		pDrmInfo->drmFd = -1;
	}
	pDrmInfo->init = FALSE;
	return;
}

void drm_loop_run(stDrmInfo *pDrmInfo)
{
	Int32 epollId = epoll_create(1);
	epollAddEvent(epollId, pDrmInfo->drmFd, EPOLLIN);
	struct epoll_event events[1] = {0};
	Int32 i = 0, cnt = 0, ret = -1;

	drmEventContext ev;
	/* Set this to only the latest version you support. Version 2
	 * introduced the page_flip_handler, so we use that. */
	ev.version = 2;
	ev.page_flip_handler = pageFlipEvent;

	drm_overlay_plane_id = pDrmInfo->planeIds[1];

	drawCaptureToDu(pDrmInfo->drmFd, &pDrmInfo->dispUnits[drm_current_du]); 

	while(1)
	{
		Int32 ms = 0;
		//ms = calcProcessTime(NULL);
		Int32 fireEvents = epoll_wait(epollId, events, 1, -1);
		//ms = calcProcessTime(&ms);
		if(fireEvents > 0){
			//DRM_LOG_WARNING("VSync %d: %d ms", cnt++, ms);
			drmHandleEvent(pDrmInfo->drmFd, &ev);
		}
	}

	return;
}

void drm_get_format(int idx)
{
	switch(idx) {
		case 0:
			drm_current_fmt = DRM_FORMAT_XRGB8888;
			printf("drm current format is XRGB8888\n");
			break;
		case 1:
			drm_current_fmt = DRM_FORMAT_YUYV;
			printf("drm current format is YUYV\n");
			break;
		default:
			drm_current_fmt = DRM_FORMAT_XRGB8888;
			printf("drm current format is XRGB8888\n");
			break;
	}
}

static void showUsage()
{
	printf("Usage: ./test_main  [params]\n" );
	printf("	(1) -c [--card] dri card (0 ~ 3).\n");
	printf("	(2) -f [--format] surport format: \n");
	printf("				[0] DRM_FORMAT_XRGB8888\n");
	printf("				[1] DRM_FORMAT_YUYV\n");
	printf("	(3) -h [--help] show the usage of this test.\n");
	printf("	Example: ./capture_v4l2 -c 1 -f 0\n");
	return;
}

static enBOOL parseArgs(Int32 argc, char* argv[])
{
	if(argc != 1 && argc != 2 &&
		argc != 3 && argc != 5){
		showUsage();
		return FALSE;
	}
	
	Int8* short_options = "c:f:h"; 
    struct option long_options[] = {  
       //{"reqarg", required_argument, NULL, 'r'},  
       //{"noarg",  no_argument,       NULL, 'n'},  
       //{"optarg", optional_argument, NULL, 'o'}, 
       {"card", required_argument, NULL, 'c'+'l'},
	   {"format", required_argument, NULL, 'f' + 'l'},
	   {"help", no_argument, NULL, 'h' + 'l'},
       {0, 0, 0, 0}};  

    Int32 opt = 0;
    while ( (opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1){
        switch(opt){
            case 'c':
            case 'c'+'l':
            {
                if(optarg){
                    char* card = optarg;
                    int card_idx = atoi(card);
					switch(card_idx) {
						case 0:
							drm_current_card = "/dev/dri/card0";
							break;
						case 1:
							drm_current_card = "/dev/dri/card1";
							break;
						case 2:
							drm_current_card = "/dev/dri/card2";
							break;
						case 3:
							drm_current_card = "/dev/dri/card3";
							break;
						default:
							drm_current_card = DRM_DEFAULT_DEV;
							break;
					}
                    printf("drm crad idx = %d, card = %s\n", card_idx, drm_current_card);
                }
                else{
                    printf("card optarg is null\n");
                }
                break;
            }
			case 'f':
            case 'f'+'l':
            {
                if(optarg){
                    char* format = optarg;
                    int fmt_idx = atoi(format);
                    drm_get_format(fmt_idx);
                }
                else{
                    printf("format optarg is null\n");
                }
                break;
            }
			case 'h':
            case 'h'+'l':
			{
				showUsage();
				return FALSE;
            }
            default:
                break;
        }
    }
	return TRUE;
}


int main(int argc, char* argv[])
{
	if(!parseArgs(argc, argv)){
		return -1;
	}
	
	// (1) 打开视频设备
	Int32 fd = -1, i = 0;
	fd = capture_open_dev(CAPTURE_VIDEO_DEV);
	if(fd < 0) {
		printf("open %s fail!\n", CAPTURE_VIDEO_DEV);
		return -1;
	}

	capture_dev_fd = fd;

	// (2) 查询视频设备的能力
	if(capture_query_cap(fd) < 0) {
		printf("capture_query_cap fail!\n");
		goto FAIL;
	}
	
	// (3) 设置视频采集的参数
	capture_show_fmts(fd);
	capture_current_fmt(fd);

	enStdType std = capture_get_standard(fd);
	if(capture_set_crop(fd, std) < 0) {
		printf("capture_set_crop fail!\n");
		goto FAIL;
	}

	if(capture_set_format(fd, CAPTURE_WIDTH, CAPTURE_HEIGHT, V4L2_PIX_FMT_YUYV) < 0) {
		printf("capture_set_format fail!\n");
		goto FAIL;
	}

	// (4) 向驱动申请视频流数据的帧缓冲区, 并
	if(capture_request_buffers(fd, CAPTURE_BUFF_NUM) < 0) {
		printf("capture_request_buffers fail!\n");
		goto FAIL;
	}

	// (5) 通过内存映射将帧缓冲区的地址映射到用户空间
	if(capture_query_buffers(fd, CAPTURE_BUFF_NUM, capture_usr_buffs) < 0) {
		printf("capture_query_buffers fail!\n");
		goto FAIL;
	}

	// (6) 开启视频输入
	if(capture_control(fd, EN_CTRL_START) < 0) {
		printf("capture_start fail!\n");
		goto FAIL;
	}

	stDrmInfo drmInfo = {0};
	Int32 ret = -1;
	ret = drm_init(&drmInfo, drm_current_card);
	if(ret < 0){
		DRM_LOG_ERR("DRM Init Fail!!!");
		goto FREE_MAP_BUFF;
	}

	// (7) 抓取数据
	Int32 cnt = 0;
	while(cnt < 5) {
		capture_dump_data(fd, cnt);
		cnt++;
	}

	drm_loop_run(&drmInfo);

	if(capture_control(fd, EN_CTRL_STOP) < 0) {
		printf("capture_stop fail!\n");
		goto FREE_MAP_BUFF;
	}

	drm_deinit(&drmInfo);
FREE_MAP_BUFF:
	for(i = 0; i < CAPTURE_BUFF_NUM; i++) {
		munmap(capture_usr_buffs[i].pBuffer, capture_usr_buffs[i].buff_size);
		capture_usr_buffs[i].pBuffer = NULL;
	}

FAIL:	
	close(fd);
	return 0;
}
