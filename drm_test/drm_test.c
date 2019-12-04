/*******************************************************
*  Author: Fanchenxin
*  Date :   2018/05/23
*  Email:  531266381@qq.com
*******************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <linux/sched.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/epoll.h>

/* drm headers */
#include <omap_drm.h>
#include <omap_drmif.h>
#include <xf86drm.h>
#include <drm_fourcc.h>
#include <xf86drmMode.h>

#define VIRTUAL_DRM (0)

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

#define COLOR_ARGB8(A, R, G, B) \
    ((((A)&0xFF) << 24) | (((R)&0xFF) << 16) | (((G)&0xFF) << 8) | ((B)&0xFF))

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
/* log definition end */

typedef unsigned char  uInt8;
typedef unsigned short uInt16;
typedef unsigned int uInt32;
typedef char Int8;
typedef short Int16;
typedef int Int32;

typedef enum
{
	FALSE = 0,
	TRUE = 1
}enBOOL;

/* Alignment with a power of two value. */
#define DRM_ALIGN(n, align) (((n) + (align) - 1L) & ~((align) - 1L))

/********************************* BMP header ******************************/
#define BF_TYPE 0x4D42             /* "MB" */

#pragma pack(1)
/**** BMP file header structure ****/
typedef struct {
    uInt16 bfType;           /* Magic number for file */
    uInt32 bfSize;           /* Size of file */
    uInt16 bfReserved1;      /* Reserved */
    uInt16 bfReserved2;      /* ... */
    uInt32 bfOffBits;        /* Offset to bitmap data */
} stBmpFileHeader;

/**** BMP file info structure ****/
typedef struct
{
    uInt32  biSize;           /* Size of info header */
    Int32   biWidth;          /* Width of image */
    Int32   biHeight;         /* Height of image */
    uInt16  biPlanes;         /* Number of color planes */
    uInt16  biBitCount;       /* Number of bits per pixel */
    uInt32  biCompression;    /* Type of compression to use */
    uInt32  biSizeImage;      /* Size of image data */
    Int32   biXPelsPerMeter;  /* X pixels per meter */
    Int32   biYPelsPerMeter;  /* Y pixels per meter */
    uInt32  biClrUsed;        /* Number of colors used */
    uInt32  biClrImportant;   /* Number of important colors */
} stBmpInfoHeader;

/*
 * Constants for the biCompression field...
 */

#define BIT_RGB       0             /* No compression - straight BGR data */
#define BIT_RLE8      1             /* 8-bit run-length compression */
#define BIT_RLE4      2             /* 4-bit run-length compression */
#define BIT_BITFIELDS 3             /* RGB bitmap with RGB masks */

/**** Colormap entry structure ****/
typedef struct
{
    uInt8   rgbBlue;          /* Blue value */
    uInt8   rgbGreen;         /* Green value */
    uInt8   rgbRed;           /* Red value */
    uInt8   rgbReserved;      /* Reserved */
} stRGB;

/**** Bitmap information structure ****/
typedef struct
{
    stBmpInfoHeader   bmiHeader;      /* Image header */
    union {
    	stRGB         bmiColors[256];  /* Image colormap */
    	uInt32        mask[3];        /* RGB masks */
    };
} stBmpInfo;
#pragma pack()

typedef struct
{
	uInt32 x;
	uInt32 y;
	uInt32 width;
	uInt32 height;
}stRect;


#define DU_NUM  		  (4)
#define FRAME_BUFFER_NUM  (2)
#define CUR_DU_IDX 		  (1)

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
}stDrmInfo;

#define CHECK_POINTER_VALID(P, V) \
	do{ \
		if(NULL == P){ \
			DRM_LOG_ERR("Pointer: %s is NULL!", #P); \
			return V; \
		} \
	}while(0)

#define MODULE_NAME ((const char *)"omapdrm")
static char* drm_module_names[] = {
	"omapdrm",
	"tidss"
};
	
typedef struct 
{
	uInt32 width;
	uInt32 height;
	uInt32 channel;
	uInt8* pBmpData;
	uInt32 size;
}stTestBmpInfo;

static stTestBmpInfo testBmpInfo[2] = {0};
static Int32	drm_current_du = 0;

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
		pDrmInfo->dispUnits[i].fourcc = DRM_FORMAT_XRGB8888;
		pDrmInfo->dispUnits[i].curFbIdx = 0;
		pDrmInfo->dispUnits[i].duValid = FALSE;
		pDrmInfo->dispUnits[i].pDrmConnector = NULL;
		pDrmInfo->dispUnits[i].pDrmMode = NULL;
		memset(&pDrmInfo->dispUnits[i].frameBuffers, 0, sizeof(stFrameBufferInfo));
	}
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

static Int32 drmFindCrtc(stDrmInfo *pDrmInfo, uInt32 curDuIdx, enBOOL first)
{
	CHECK_POINTER_VALID(pDrmInfo, -1);
	stDisplayUnit *pCurDu = &pDrmInfo->dispUnits[curDuIdx];
	#if !VIRTUAL_DRM
	drmModeEncoder *pDrmEnc = NULL;
	uInt32 i = 0, j = 0;
	/* First try the currently connector */
	if(pCurDu->pDrmConnector->encoder_id){
		pDrmEnc = drmModeGetEncoder(pDrmInfo->drmFd, pCurDu->pDrmConnector->encoder_id);
	}
	else{
		pDrmEnc = NULL;
	}

	if(first && pDrmEnc){
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
					if(!first && (pDrmEnc->crtc_id == pCurDu->crtcId)) {
						DRM_LOG_WARNING("prev crtc: %d fail, so skeep!", pCurDu->crtcId);
						continue;
					}
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
	#else
	pCurDu->crtcId = 30U;
	return 0;
	#endif
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
	if(ret < 0){
		DRM_LOG_ERR("Cannot set crtc for connector: %d, crtcID = %d, ret = %d!", pDispUnit->pDrmConnector->connector_id, pDispUnit->crtcId, ret);
		return -1;
	}
	return 0;
}

static uint8_t getNextColor(enBOOL *up, uint8_t cur, uInt32 mod)
{
	uint8_t next;

	next = cur + (*up ? 1 : -1) * (rand() % mod);
	if ((*up && next < cur) || (!*up && next > cur)) {
		*up = !*up;
		next = cur;
	}

	return next;
}

static void drmTestDrawRGB(stDrmInfo *pDrmInfo)
{
	uint8_t r, g, b;
	enBOOL r_up, g_up, b_up;
	uInt32 i, h, w, off;
	uInt32 bufferIdx = 0, cnt = 0;

	srand(time(NULL));
	r = rand() % 0xff;
	g = rand() % 0xff;
	b = rand() % 0xff;
	r_up = g_up = b_up = TRUE;

	while(1){
		r = getNextColor(&r_up, r, 20);
		g = getNextColor(&g_up, g, 10);
		b = getNextColor(&b_up, b, 5);

		for (i = 0; i < pDrmInfo->pDrmResources->count_connectors; i++) {
			stDisplayUnit *pdu = &pDrmInfo->dispUnits[i];
			//if(pdu->pDrmConnector->connector_type != DRM_MODE_CONNECTOR_Unknown){
				stFrameBufferInfo *pfb = &pdu->frameBuffers[bufferIdx ^ 1];
				for (h = 0; h < pdu->height; ++h) {
					for (w = 0; w < pdu->width; ++w) {
						off = pfb->stride * h + w * 4;
						*(uint32_t*)&pfb->pMapAddr[off] = (r << 16) | (g << 8) | b;
					}
				}
				bufferIdx ^= 1;
			//}
		}

		usleep(100000);
		if(++cnt > 1000){
			break;
		}
	}
	return;
}

static enBOOL isBigEndian()
{
	typedef union {
		Int16 s;
		Int8 a;
	}uT;
	uT t = {0};
	t.s = 0x0001;
	return (t.a == 0);
}


uInt8 * loadBmp(const char *fileName, uInt32 *width, uInt32 *height, uInt32 *bytePix)
{
    FILE    *fp = NULL;          /* Open file pointer */
    uInt8   *bits = NULL;        /* Bitmap pixel bits */
    uInt32   bitsize;      /* Size of bitmap */
    uInt32   filesize = 0;
    Int32    infosize;     /* Size of header information */
    stBmpFileHeader  header = {0};       /* File header */
	stBmpInfo bmpInfo = {0};

    /* Try opening the file; use "rb" mode to read this *binary* file. */
    fp = fopen(fileName, "rb");
    if(!fp){
        /* Failed to open the file. */
        DRM_LOG_ERR("Failed to open the file %s", fileName);
        return (NULL);
    }

    /* Read the file header and any following bitmap information... */
	fread(&header, sizeof(stBmpFileHeader), 1, fp);
	DRM_LOG_NOTIFY("bfType: %x, bfSize: %d, bfOffBits: %d", header.bfType, header.bfSize, header.bfOffBits);
    if (header.bfType != BF_TYPE){
        /* Not a bitmap file - return NULL... */
        DRM_LOG_ERR("*ERROR*  Not a bitmap file, bfType = %x", header.bfType);
        fclose(fp);
        return (NULL);
    }

    infosize = header.bfOffBits - 14; // sizeof(header);
	/* read bmp info head data */
	fread(&bmpInfo.bmiHeader, sizeof(stBmpInfoHeader), 1, fp);

    if (infosize > 40){
		/* read color map table */
        Int32 n = (infosize - 40) / 4;
        uInt32 *p = (uInt32 *)(bmpInfo.bmiColors);
		fread(bmpInfo.bmiColors, sizeof(uInt32), n, fp);
    }

    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);

    /* Seek to the image. */
    if (fseek(fp, header.bfOffBits, SEEK_SET) != 0){
        fclose(fp);
        DRM_LOG_ERR("*ERROR* bitmap file error");
        return (NULL);
    }

    /* Now that we have all the header info read in, allocate memory for *
     * the bitmap and read *it* in...                                    */
    if ((bitsize = bmpInfo.bmiHeader.biSizeImage) == 0){
        bitsize = (bmpInfo.bmiHeader.biWidth) *
                   ((bmpInfo.bmiHeader.biBitCount + 7) / 8) * abs(bmpInfo.bmiHeader.biHeight);
    }
    else{
        if ((Int32)bitsize < (DRM_ALIGN((bmpInfo.bmiHeader.biWidth) *
                   (bmpInfo.bmiHeader.biBitCount), 8) >> 3) * abs(bmpInfo.bmiHeader.biHeight)){
            DRM_LOG_ERR("*ERROR* bitmap format wrong!");
			fclose(fp);
            return (NULL);
        }
    }

    if (header.bfOffBits + bitsize > filesize){
        DRM_LOG_ERR( "*ERROR* bitmap format wrong!");
		fclose(fp);
        return (NULL);
    }

    if ((bits = (unsigned char *)malloc(bitsize)) == NULL){
        /* Couldn't allocate memory - return NULL! */
        fclose(fp);
        DRM_LOG_ERR("*ERROR* out-of-memory2");
        return (NULL);
    }

	DRM_LOG_NOTIFY("bmp data size = %d, bitsPix = %d, wxh = %d x %d",
		bitsize, bmpInfo.bmiHeader.biBitCount, bmpInfo.bmiHeader.biWidth, bmpInfo.bmiHeader.biHeight);
	*width = bmpInfo.bmiHeader.biWidth;
	*height = bmpInfo.bmiHeader.biHeight;
	*bytePix = bmpInfo.bmiHeader.biBitCount >> 3;

    if (fread(bits, 1, bitsize, fp) < bitsize){
        /* Couldn't read bitmap - free memory and return NULL! */
        free(bits);
        fclose(fp);
        DRM_LOG_ERR("*ERROR* read bmp file error");
        return (NULL);
    }

    /* OK, everything went fine - return the allocated bitmap... */
    fclose(fp);
    return (bits);
}

// pRect: x和y是源在目标的起始位置，高宽必须是源图像的高宽
static void fillDataToRect(uInt8 *pDstAddr, uInt32 dstWidth, uInt8 *pSrcAddr, stRect *pRect, uInt8 srcPixBytes)
{
	uInt8 *pDstStartPos = NULL, *pSrcStartPos = NULL;
	uInt32 i = 0;
	uInt32 dst_stride = dstWidth * 4; // dest alway xrgb, so is 4
	uInt32 src_stride = pRect->width * srcPixBytes;
	pDstStartPos = pDstAddr+ pRect->y * dst_stride + pRect->x * 4;
	pSrcStartPos = pSrcAddr;
	for (i = 0; i < pRect->height; ++i) {
		if(srcPixBytes == 4) {
	    	memcpy(pDstStartPos, pSrcStartPos, src_stride);
		}
		else {
			int j = 0, k = 0;
			for(j = 0; j < src_stride; j += srcPixBytes) {
				pDstStartPos[k] = pSrcStartPos[j];
				pDstStartPos[k + 1] = pSrcStartPos[j + 1];
				pDstStartPos[k + 2] = pSrcStartPos[j + 2];
				pDstStartPos[k + 3] = 0xff;
				k += 4;
			}
		}
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

static void epollAddEvent(Int32 epollFd, Int32 addFd, Int32 state)
{
	struct epoll_event ev;
	ev.events = state;
	ev.data.fd = addFd;
	epoll_ctl(epollFd, EPOLL_CTL_ADD, addFd, &ev);
	return;
}

static void drawBmpToDu(Int32 drmFd, stDisplayUnit *pDu)
{
	Int32 ret = -1;
	static stRect rect[2] = {{0, 0, 640, 360}, {400, 400, 640, 360}};
	stFrameBufferInfo *pfb = &pDu->frameBuffers[pDu->curFbIdx];
	//memset(pfb->pMapAddr, 0, pfb->size);

	fillDataToRect(pfb->pMapAddr, pDu->width, testBmpInfo[0].pBmpData, &rect[0], testBmpInfo[0].channel);
	fillDataToRect(pfb->pMapAddr, pDu->width, testBmpInfo[1].pBmpData, &rect[1], testBmpInfo[1].channel);

	ret = drmModePageFlip(drmFd, pDu->crtcId, pfb->fbId, DRM_MODE_PAGE_FLIP_EVENT, pDu);
	if (ret) {
		DRM_LOG_ERR("cannot flip CRTC for connector %u : %d", pDu->pDrmConnector->connector_id, errno);
	}
	else{
		pDu->curFbIdx ^= 1;
	}
	//DRM_LOG_NOTIFY("Flip crtc id = %d, curFbIdx = %d", pDu->crtcId, pDu->curFbIdx);
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
	drawBmpToDu(drmFd, pDu);
	return;
}

static void drmTestDrawBmpVsync(stDrmInfo *pDrmInfo)
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

	drawBmpToDu(pDrmInfo->drmFd ,&pDrmInfo->dispUnits[drm_current_du]);	

	FILE *pf = fopen("fb.xrgb", "wb");
	fwrite(pDrmInfo->dispUnits[drm_current_du].frameBuffers[0].pMapAddr, pDrmInfo->dispUnits[drm_current_du].frameBuffers[0].size, 1, pf);
	fclose(pf);
	
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

Int32 findValidDu(stDrmInfo *pDrmInfo)
{
	Int32 i = 0;
	for(i = 0; i < DU_NUM; i++) {
		if(pDrmInfo->dispUnits[i].duValid = TRUE) {
			drm_current_du = i;
			DRM_LOG_NOTIFY("Find Valid Display Unit: %d", i);
			return 0;
		}
	}
	return -1;
}

void dump_data(Int32 size, void* buff)
{	
	static Int32 idx = 0;
	char file[128] = {'\0'};
	sprintf(file, "./bmp%02d.rgb", idx++);
	FILE *pf = fopen(file, "wb");
	fwrite(buff, size, 1, pf);
	fclose(pf);
}

Int32 drmInit(stDrmInfo *pDrmInfo, const char* dev)
{
	CHECK_POINTER_VALID(pDrmInfo, -1);
	uInt32 i = 0, j = 0;
	uint64_t hasDumb;
	drmModeConnectorPtr drmConn = NULL;
	if(pDrmInfo->init) return 0;
	drmInfoClear(pDrmInfo);
	/* open drm device */
	#if !VIRTUAL_DRM
	//pDrmInfo->drmFd = drmOpen(MODULE_NAME, NULL);
	pDrmInfo->drmFd = open(dev, O_RDWR | O_CLOEXEC);
	#else
	pDrmInfo->drmFd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	#endif
	
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
		if(drmFindCrtc(pDrmInfo, i, TRUE) < 0){
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
		
		pDrmInfo->dispUnits[i].duValid = TRUE;
		drmConn = NULL;
    }
	pDrmInfo->init = TRUE;
	return 0;
FAIL:

	if(pDrmInfo->pDrmResources){
		for (i = 0; i < pDrmInfo->pDrmResources->count_connectors; i++) {
			if(pDrmInfo->dispUnits[i].pDrmConnector){
				drmModeFreeConnector(pDrmInfo->dispUnits[i].pDrmConnector);
				pDrmInfo->dispUnits[i].pDrmConnector = NULL;
			}
		}
		drmModeFreeResources(pDrmInfo->pDrmResources);
		pDrmInfo->pDrmResources = NULL;
	}

	if(pDrmInfo->drmFd){
		drmClose(pDrmInfo->drmFd);
		pDrmInfo->drmFd = -1;
	}
	
	return -1;
}

void drmFin(stDrmInfo *pDrmInfo)
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

Int32 main(Int32 argc, char* argv[])
{
	const char* card = NULL;
	if (argc > 1)
		card = argv[1];
	else
		card = "/dev/dri/card0";
	
	stDrmInfo drmInfo = {0};
	Int32 ret = -1;
	ret = drmInit(&drmInfo, card);
	if(ret < 0){
		DRM_LOG_ERR("DRM Init Fail!!!");
		return -1;
	}

	if(findValidDu(&drmInfo) < 0) {
		DRM_LOG_ERR("Can not find valid Display Unit!!!!");
		goto DRM_FIN;
	}

	uInt32 w, h, bp;
	testBmpInfo[0].pBmpData = loadBmp("1.bmp", &w, &h, &bp);
	if(NULL == testBmpInfo[0].pBmpData){
		DRM_LOG_ERR("Load bmp: rgb8_640x480.bmp fail...");
		drmFin(&drmInfo);
		return -1;
	}
	testBmpInfo[0].width = w;
	testBmpInfo[0].height = h;
	testBmpInfo[0].channel = bp;
	testBmpInfo[0].size = w * h * bp;

	

	testBmpInfo[1].pBmpData = loadBmp("2.bmp", &w, &h, &bp);
	if(NULL == testBmpInfo[1].pBmpData){
		DRM_LOG_ERR("Load bmp: rgb8_800x600.bmp fail...");
		drmFin(&drmInfo);
		return - 1;
	}
	testBmpInfo[1].width = w;
	testBmpInfo[1].height = h;
	testBmpInfo[1].channel = bp;
	testBmpInfo[1].size = w * h * bp;

	dump_data(testBmpInfo[0].size, testBmpInfo[0].pBmpData);
	dump_data(testBmpInfo[1].size, testBmpInfo[1].pBmpData);
	
	drmTestDrawBmpVsync(&drmInfo);
	free( testBmpInfo[0].pBmpData);
	testBmpInfo[0].pBmpData = NULL;
	free( testBmpInfo[1].pBmpData);
	testBmpInfo[1].pBmpData = NULL;

DRM_FIN:	
	drmFin(&drmInfo);
	return 0;
}
