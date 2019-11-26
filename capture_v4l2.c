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
#include <linux/fb.h>
#include <SDL2/SDL.h>

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

#define CAPTURE_VIDEO_DEV	"/dev/video0"
#define CAPTURE_BUFF_NUM	(4)
#define CAPTURE_WIDTH		(640)
#define CAPTURE_HEIGHT		(480)

#define FB_DEV	"/dev/fb0"
#define FB_NUM	(4)

#define DRAW_TO_SDL	(1)  // draw to FB or SDL

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

typedef struct
{
	void *pBuffer;
	int buff_size;
}stUserBuff;

typedef struct
{
	int width;
	int height;
	int bpp;
	int size;
	unsigned char* fb_offset;
}stFbInfo;

typedef struct
{
	int width;
	int height;
	int format;
	SDL_Window* window;
	SDL_Renderer* render;
	SDL_Texture* texture;
}stSDL2;

static stUserBuff capture_usr_buffs[CAPTURE_BUFF_NUM];

//yuyv转rgb32的算法实现  
/*
YUV到RGB的转换有如下公式： 
R = 1.164*(Y-16) + 1.159*(V-128); 
G = 1.164*(Y-16) - 0.380*(U-128)+ 0.813*(V-128); 
B = 1.164*(Y-16) + 2.018*(U-128)); 
*/
int yuvtorgb(int y, int u, int v, int calc)
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

int yuyv2rgb24(unsigned char *yuv, unsigned char *rgb, unsigned int width, unsigned int height)
{
     unsigned int in, out;
     int y0, u, y1, v;
     unsigned int pixel32;
     unsigned char *pixel = (unsigned char *)&pixel32;
     //分辨率描述像素点个数，而yuv2个像素点占有4个字符，所以这里计算总的字符个数，需要乘2
     unsigned int size = width*height*2; 

     for(in = 0, out = 0; in < size; in += 4, out += 6) {
          y0 = yuv[in+0];
          u  = yuv[in+1];
          y1 = yuv[in+2];
          v  = yuv[in+3];

          pixel32 = yuvtorgb(y0, u, v, 1);
          rgb[out+0] = pixel[0];   
          rgb[out+1] = pixel[1];
          rgb[out+2] = pixel[2];

          pixel32 = yuvtorgb(y1, u, v, 0);
          rgb[out+4] = pixel[0];
          rgb[out+5] = pixel[1];
          rgb[out+6] = pixel[2];
     }
     return 0;
}

int capture_open_dev(const char* dev)
{
	return open(dev, O_RDWR);
}

int capture_query_cap(int fd)
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

void capture_show_fmts(int fd)
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

void capture_current_fmt(int fd)
{
	struct v4l2_format fmt;	
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0)
		printf( "capture: G_FMT after set format failed: %s\n", strerror(errno));

	printf("capture: G_FMT(start): width = %u, height = %u, 4cc = %.4s\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height,
			(char*)&fmt.fmt.pix.pixelformat);
}

void capture_check_fmt(int fd, int pixel_fmt)
{
	struct v4l2_format fmt;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = pixel_fmt;
	if(ioctl(fd, VIDIOC_TRY_FMT, &fmt) == -1) {
		printf("capture not support fmt: 0x%x\n", pixel_fmt);
	}
}

enStdType capture_get_standard(int fd)
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

int capture_set_format(int fd, int width, int height, int pixel_fmt)
{
	int ret;
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

int capture_set_crop(int fd, enStdType std)
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

int capture_request_buffers(int fd, int mem_num)
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

int capture_query_buffers(int fd, int mem_num, stUserBuff *pUserBuff)
{
	int i = 0;
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

int capture_control(int fd, enCtrlType ctrl)
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

int capture_dump_data(int fd, int cnt)
{
	int ret = -1;
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
	if(pf) {
		fwrite(capture_usr_buffs[dequeue_buf.index].pBuffer, dequeue_buf.length, 1, pf);
		fclose(pf);
		printf("Dump: %s, buff index = %d, len = %d\n", file, dequeue_buf.index, dequeue_buf.length);
	}
	else {
		printf("Open %s fail!\n", file);
	}

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

int fb_open(const char* dev)
{
	return open(dev, O_RDWR);
}

int fb_display_setup(int fd, stFbInfo* fb_info)
{
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo; 
    
    // Get fixed screen information
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo)) {
        printf("Error reading fixed information.\n");
        return -1;
    }

    // Get variable screen information
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo)) {            
        printf("Error reading variable information.\n");
        return -1;
    }
    //这里把整个显存一起初始化（xres_virtual 表示显存的x，比实际的xres大,bits_per_pixel位深）
    fb_info->size = vinfo.xres_virtual * vinfo.yres_virtual * vinfo.bits_per_pixel / 8;
    //获取实际的位色，这里很关键，后面转换和填写的时候需要
    fb_info->bpp = vinfo.bits_per_pixel;
	fb_info->width = vinfo.xres_virtual;
	fb_info->height = vinfo.yres_virtual;
    printf("%dx%d, %d bpp, screensize is %ld\n", vinfo.xres_virtual, vinfo.yres_virtual, vinfo.bits_per_pixel, fb_info->size);
    
    //映射出来，用户直接操作
    fb_info->fb_offset = mmap(0, fb_info->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd , 0);  
    if(fb_info->fb_offset == (void *)-1)  {  
        perror("memory map fail");  
        return -1;  
    }  
    return 0;
}

void fb_write(int x, int y, stFbInfo* fb_info, unsigned char* rgb24)
{
	int bytes_pix = fb_info->bpp / 8; 
	int stride = fb_info->width * bytes_pix;
	int row, col, cnt = 0;
	unsigned char* dst_start = fb_info->fb_offset + y * stride + x;
	for(row = 0; row < CAPTURE_HEIGHT; row++) {
		for(col = 0; col < CAPTURE_WIDTH; col++) {
			dst_start[row * stride + col * bytes_pix + 2] = rgb24[cnt];  // b
			dst_start[row * stride + col * bytes_pix + 1] = rgb24[cnt + 1]; // g
			dst_start[row * stride + col * bytes_pix] = rgb24[cnt + 2];  // r
			
			if(fb_info->bpp == 32) {
				dst_start[row * stride + col * bytes_pix + 3] = 0xff;
			}

			cnt += 3;
		}
	}
}

int capture_fb_show(int fd, stFbInfo* fb_info)
{
	int ret = -1;
	unsigned char* rgb24 = (unsigned char*)malloc(CAPTURE_WIDTH * CAPTURE_HEIGHT * 3 * sizeof(unsigned char));

	while(1) {
		struct v4l2_buffer dequeue_buf, queue_buf;
		dequeue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		dequeue_buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(fd, VIDIOC_DQBUF, &dequeue_buf);
		if(ret < 0) {
			printf("dqueue buffer fail!\n");
			return -1;
		}

		yuyv2rgb24(capture_usr_buffs[dequeue_buf.index].pBuffer, rgb24, CAPTURE_WIDTH, CAPTURE_HEIGHT);
		fb_write(0, 0, fb_info, rgb24);

		queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		queue_buf.memory = V4L2_MEMORY_MMAP;
		queue_buf.index = dequeue_buf.index;
		ret = ioctl(fd, VIDIOC_QBUF, &queue_buf);
		if(ret < 0) {
			printf("queue buffer fail!\n");
			return -1;
		}
	}

	free(rgb24);
	return 0;
}


int sdl2_init(stSDL2 *pSdl2)
{
	pSdl2->window = SDL_CreateWindow("sdl_win", 
									SDL_WINDOWPOS_UNDEFINED,
									SDL_WINDOWPOS_UNDEFINED,
									pSdl2->width,
									pSdl2->height,
									SDL_WINDOW_SHOWN);
	if(pSdl2->window == NULL) {
		printf("sdl2 create window fail!\n");
		return -1;
	}

	pSdl2->render = SDL_CreateRenderer(pSdl2->window, -1, 0);
	if (pSdl2->render == NULL) {
		printf("sdl2 create render error\n");
		return -1;
	}

	pSdl2->texture = SDL_CreateTexture(pSdl2->render, pSdl2->format, SDL_TEXTUREACCESS_STREAMING, pSdl2->width, pSdl2->height);
	if (pSdl2->texture == NULL) {
		printf("sdl2 create texture error\n");
		return -1;
	}
	return 0;
}

void sdl2_deinit(stSDL2 *pSdl2)
{




	SDL_DestroyWindow(pSdl2->window);
	SDL_DestroyRenderer(pSdl2->render);
	SDL_DestroyTexture(pSdl2->texture);
	SDL_Quit();










}

void sdl2_draw_texture(int x, int y, stSDL2 *pSdl2, unsigned char* rgb24)
{
	unsigned char *pix;
	int pitch = 0, bytes_pix = 0;
	SDL_Rect dst_rect;
	dst_rect.x = x;
	dst_rect.y = y;
	dst_rect.w = CAPTURE_WIDTH;
	dst_rect.h = CAPTURE_HEIGHT;
	SDL_LockTexture(pSdl2->texture, &dst_rect, &pix, &pitch);
	bytes_pix = pitch / pSdl2->width;
	//printf("pitch = %d\n", pitch);
	int row, col, cnt = 0;
	for(row = 0; row < CAPTURE_HEIGHT; row++) {
		for(col = 0; col < CAPTURE_WIDTH; col++) {
			pix[row * pitch + col * bytes_pix + 1] = rgb24[cnt + 2];  // b
			pix[row * pitch + col * bytes_pix + 2] = rgb24[cnt + 1]; // g
			pix[row * pitch + col * bytes_pix + 3] = rgb24[cnt];  // r
			
			if(bytes_pix == 4) {
				pix[row * pitch + col * bytes_pix] = 0xff;
			}

			cnt += 3;
		}
	}
	SDL_UnlockTexture(pSdl2->texture);
}

void sdl2_render(stSDL2 *pSdl2)
{
	SDL_RenderCopy(pSdl2->render, pSdl2->texture, NULL, NULL);
	SDL_RenderPresent(pSdl2->render);
}

int capture_sdl2_show(int fd, stSDL2* sdl_info)
{
	int ret = -1;
	unsigned char* rgb24 = (unsigned char*)malloc(CAPTURE_WIDTH * CAPTURE_HEIGHT * 3 * sizeof(unsigned char));

	while(1) {
		struct v4l2_buffer dequeue_buf, queue_buf;
		dequeue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		dequeue_buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(fd, VIDIOC_DQBUF, &dequeue_buf);
		if(ret < 0) {
			printf("dqueue buffer fail!\n");
			return -1;
		}

		if(sdl_info->format == SDL_PIXELFORMAT_RGBA8888) {
			yuyv2rgb24(capture_usr_buffs[dequeue_buf.index].pBuffer, rgb24, CAPTURE_WIDTH, CAPTURE_HEIGHT);
			sdl2_draw_texture(0, 0, sdl_info, rgb24);
		}
		else {
			SDL_Rect dst_rect;
			dst_rect.x = 0;
			dst_rect.y = 0;
			dst_rect.w = CAPTURE_WIDTH;
			dst_rect.h = CAPTURE_HEIGHT;
			SDL_UpdateTexture(sdl_info->texture, &dst_rect, capture_usr_buffs[dequeue_buf.index].pBuffer, CAPTURE_WIDTH * 2);
		}

		sdl2_render(sdl_info);

		queue_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		queue_buf.memory = V4L2_MEMORY_MMAP;
		queue_buf.index = dequeue_buf.index;
		ret = ioctl(fd, VIDIOC_QBUF, &queue_buf);
		if(ret < 0) {
			printf("queue buffer fail!\n");
			return -1;
		}
	}

	free(rgb24);
	return 0;
}


int main(int argc, char* argv[])
{
	// (1) 打开视频设备
	int fd = -1, fb_fd = -1, i = 0;
	fd = capture_open_dev(CAPTURE_VIDEO_DEV);
	if(fd < 0) {
		printf("open %s fail!\n", CAPTURE_VIDEO_DEV);
		return -1;
	}

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
		goto FREE_MAP_BUFF;
	}

	#if DRAW_TO_SDL
	// (8) 初始化SDL2
	stSDL2 sdl2_obj;
	sdl2_obj.width = CAPTURE_WIDTH;
	sdl2_obj.height = CAPTURE_HEIGHT;
	//sdl2_obj.format = SDL_PIXELFORMAT_RGBA8888;
	sdl2_obj.format = SDL_PIXELFORMAT_YUY2;
	if(sdl2_init(&sdl2_obj) < 0) {
		printf("sdl2_init fail\n");
		goto FREE_MAP_BUFF;
	}
	#else
	
	// (8) 打开framebuffer
	if((fb_fd = fb_open(FB_DEV)) < 0) {
		printf("open fb dev: %s fail\n", FB_DEV);
		goto FREE_MAP_BUFF;
	}

	stFbInfo fb_info;
	if(fb_display_setup(fb_fd, &fb_info) < 0) {
		printf("fb_display_setup fail!\n");
		goto FREE_MAP_BUFF;
	}
	#endif

	// (7) 抓取数据
	int cnt = 0;
	while(cnt < 5) {
		capture_dump_data(fd, cnt);
		cnt++;
	}

	#if DRAW_TO_SDL
	capture_sdl2_show(fd, &sdl2_obj);
	#else
	capture_fb_show(fd, &fb_info);
	#endif	

	if(capture_control(fd, EN_CTRL_STOP) < 0) {
		printf("capture_stop fail!\n");
		goto FREE_MAP_BUFF;
	}

	

FREE_MAP_BUFF:
	for(i = 0; i < CAPTURE_BUFF_NUM; i++) {
		munmap(capture_usr_buffs[i].pBuffer, capture_usr_buffs[i].buff_size);
		capture_usr_buffs[i].pBuffer = NULL;
	}

	#if !DRAW_TO_SDL
	munmap(fb_info.fb_offset, fb_info.size);
	close(fb_fd);
	#else
	sdl2_deinit(&sdl2_obj);	
	#endif

FAIL:	
	close(fd);
	return 0;
}
