/*
 *      test.c  --  USB Video Class test application
 *
 *      Copyright (C) 2005-2008
 *          Laurent Pinchart (laurent.pinchart@skynet.be)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 */

/*
 * WARNING: This is just a test application. Don't fill bug reports, flame me,
 * curse me on 7 generations :-).
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <jpeglib.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <signal.h>

#define anchor()        printf("-> %s [%d]\n", __func__, __LINE__)


#define FB_FILE 				"/dev/fb0"
#define FILE_SAVE_PREFIX		"/tmp/capture_"

typedef struct  _fb_v4l
{
	int fbfd ;                                                                     
	char *fbp;
	struct fb_var_screeninfo vinfo;                                                 
	struct fb_fix_screeninfo finfo;                                              
} fb_v41;

/*
 * Global var
 */
fb_v41 vd;
int buttonSnapshotPressed = 0;
pthread_mutex_t mutex;

static void jpeg_to_framebuffer(fb_v41 *vd, int width, int height, int xoffset, int yoffset, JSAMPARRAY buffer)
{
	int x, y;
	unsigned char *local_ptr;
	unsigned char Red, Green, Blue;
	unsigned short tmp;

	for(y = 0; y < height; y++){

		local_ptr = (unsigned char *) vd->fbp + 
			xoffset * (vd->vinfo.bits_per_pixel / 8) + (y + yoffset) * vd->finfo.line_length;

		for(x = 0; x < width; x++){
			Red = buffer[y][x * 3];
			Green = buffer[y][x * 3 + 1];
			Blue = buffer[y][x * 3 + 2];
			if (vd->vinfo.bits_per_pixel == 16) {
				tmp = ((Red >> 3) << 11) + ((Green >> 2) << 5) + (Blue >> 3) << 0;		
				*local_ptr++ = tmp & 0xFF;
				*local_ptr++ = (tmp & 0xFF00) >> 8;
			} 
			else {
				*local_ptr++ = Blue;
				*local_ptr++ = Green;
				*local_ptr++ = Red;
				*local_ptr++ = 0x00;
			}
		}
	}
}

static void yuv_to_framebuffer(fb_v41 *vd, int width, int height, int xoffset, int yoffset, unsigned char *buffer)
{
	int i;
	unsigned char *local_ptr, *data_ptr;
	int w = width;
	int h = height;
	int alignCenter = 1;
	int buttonHeight = 40;

	if (w > vd->vinfo.xres)
		w = vd->vinfo.xres;
	if (h > vd->vinfo.yres)
		h = vd->vinfo.yres;
	if (alignCenter) {
		xoffset = (vd->vinfo.xres - w) >> 1;
		yoffset = (vd->vinfo.yres - h) >> 1;
	}
	if (vd->vinfo.yres < h + yoffset + buttonHeight) {
		yoffset = 0;
		h = vd->vinfo.yres - buttonHeight;
	}

	for (i = 0; i < h; i++) {
		local_ptr = (unsigned char *) vd->fbp + 
			xoffset * (vd->vinfo.bits_per_pixel / 8) + (i + yoffset) * vd->finfo.line_length;
		data_ptr = (unsigned char *) (buffer + i * width * (vd->vinfo.bits_per_pixel / 8));
		memcpy(local_ptr, data_ptr, w * (vd->vinfo.bits_per_pixel / 8));
	}
}

static inline void yuv_to_rgb16(unsigned char y,
		unsigned char u,
		unsigned char v,
		unsigned char *rgb)
{
	register int r,g,b;
	int rgb16;

	r = (1192 * (y - 16) + 1634 * (v - 128) ) >> 10;
	g = (1192 * (y - 16) - 833 * (v - 128) - 400 * (u -128) ) >> 10;
	b = (1192 * (y - 16) + 2066 * (u - 128) ) >> 10;

	r = r > 255 ? 255 : r < 0 ? 0 : r;
	g = g > 255 ? 255 : g < 0 ? 0 : g;
	b = b > 255 ? 255 : b < 0 ? 0 : b;

	rgb16 = (int)(((r >> 3)<<11) | ((g >> 2) << 5)| ((b >> 3) << 0));

	*rgb = (unsigned char)(rgb16 & 0xFF);
	rgb++;
	*rgb = (unsigned char)((rgb16 & 0xFF00) >> 8);
}

static inline void yuv_to_rgb24(unsigned char y,
		unsigned char u,
		unsigned char v,
		unsigned char *rgb)
{
	register int r,g,b;

	r = (1192 * (y - 16) + 1634 * (v - 128) ) >> 10;
	g = (1192 * (y - 16) - 833 * (v - 128) - 400 * (u -128) ) >> 10;
	b = (1192 * (y - 16) + 2066 * (u - 128) ) >> 10;

	r = r > 255 ? 255 : r < 0 ? 0 : r;
	g = g > 255 ? 255 : g < 0 ? 0 : g;
	b = b > 255 ? 255 : b < 0 ? 0 : b;

	*rgb = (unsigned char)r;
	rgb++;
	*rgb = (unsigned char)g;
	rgb++;
	*rgb = (unsigned char)b;
}

static inline void yuv_to_rgb32(unsigned char y,
		unsigned char u,
		unsigned char v,
		unsigned char *rgb)
{
	register int r,g,b;

	r = (1192 * (y - 16) + 1634 * (v - 128) ) >> 10;
	g = (1192 * (y - 16) - 833 * (v - 128) - 400 * (u -128) ) >> 10;
	b = (1192 * (y - 16) + 2066 * (u - 128) ) >> 10;

	r = r > 255 ? 255 : r < 0 ? 0 : r;
	g = g > 255 ? 255 : g < 0 ? 0 : g;
	b = b > 255 ? 255 : b < 0 ? 0 : b;

	*rgb = (unsigned char)b;
	rgb++;
	*rgb = (unsigned char)g;
	rgb++;
	*rgb = (unsigned char)r;
	rgb++;
	*rgb = 0x00;
}

void convert_to_rgb16(unsigned char *buf, unsigned char *rgb, int width, int height)
{
	int x,y,z=0;
	int blocks;

	blocks = (width * height) * 2;

	for (y = 0; y < blocks; y+=4) {
		unsigned char Y1, Y2, U, V;

		Y1 = buf[y + 0];
		U = buf[y + 1];
		Y2 = buf[y + 2];
		V = buf[y + 3];

		yuv_to_rgb16(Y1, U, V, &rgb[y]);
		yuv_to_rgb16(Y2, U, V, &rgb[y + 2]);
	}
}

void convert_to_rgb24(unsigned char *buf, unsigned char *rgb, int width, int height)
{
	int x,y,z=0;
	int blocks;

	blocks = (width * height) * 2;

	for (y = 0, z = 0; y < blocks; y+=4, z+=6) {
		unsigned char Y1, Y2, U, V;

		Y1 = buf[y + 0];
		U = buf[y + 1];
		Y2 = buf[y + 2];
		V = buf[y + 3];

		yuv_to_rgb24(Y1, U, V, &rgb[z]);
		yuv_to_rgb24(Y2, U, V, &rgb[z + 3]);
	}
}

void convert_to_rgb32(unsigned char *buf, unsigned char *rgb, int width, int height)
{
	int x,y,z=0;
	int blocks;

	blocks = (width * height) * 2;

	for (y = 0, z = 0; y < blocks; y+=4, z+=8) {
		unsigned char Y1, Y2, U, V;

		Y1 = buf[y + 0];
		U = buf[y + 1];
		Y2 = buf[y + 2];
		V = buf[y + 3];

		yuv_to_rgb32(Y1, U, V, &rgb[z]);
		yuv_to_rgb32(Y2, U, V, &rgb[z + 4]);
	}
}


static int open_framebuffer(char *ptr,fb_v41 *vd)
{
	int fbfd, screensize;

	fbfd = open (ptr, O_RDWR);
	if (fbfd < 0) {
		printf("Error: cannot open framebuffer device.%x\n",fbfd);
		return 0;
	}
	printf("The framebuffer device was opened successfully.\n");

	vd->fbfd = fbfd;

	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &vd->finfo)) {
		printf("Error reading fixed information.\n");
		return 0;
	}

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vd->vinfo)) {
		printf("Error reading variable information.\n");
		return 0;
	}

	printf("%dx%d, %dbpp, xoffset=%d ,yoffset=%d \n", vd->vinfo.xres, vd->vinfo.yres, vd->vinfo.bits_per_pixel,vd->vinfo.xoffset,vd->vinfo.yoffset );
	printf("Length of a line in bytes: %d\n", vd->finfo.line_length);

	screensize = vd->vinfo.xres * vd->vinfo.yres * vd->vinfo.bits_per_pixel / 8;

	vd->fbp = (char *)mmap(NULL, screensize, PROT_READ|PROT_WRITE, MAP_SHARED, fbfd, 0);
	if ((int)vd->fbp == -1) {
		printf("Error: failed to map framebuffer device to memory.\n");
		return 0;
	}

	/* clear screen */
	memset (vd->fbp, 0, screensize);

	printf("The framebuffer device was mapped to memory successfully.\n");
	return  1;
}

static void close_framebuffer (fb_v41 *vd)
{
	if (vd->fbp) {
		int screensize = vd->vinfo.xres * vd->vinfo.yres * vd->vinfo.bits_per_pixel / 8;
		memset (vd->fbp, 0, screensize);
		munmap (vd->fbp, screensize);
		vd->fbp = NULL;
	}
	if (vd->fbfd > 0) {
		close (vd->fbfd);
		vd->fbfd = -1;
	}
}

static int video_open(const char *devname)
{
	struct v4l2_capability cap;
	int dev, ret;

	dev = open(devname, O_RDWR);
	if (dev < 0) {
		printf("Error opening device %s: %d.\n", devname, errno);
		return dev;
	}

	memset(&cap, 0, sizeof cap);
	ret = ioctl(dev, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("Error opening device %s: unable to query device.\n",
				devname);
		close(dev);
		return ret;
	}

	if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
		printf("Error opening device %s: video capture not supported.\n",
				devname);
		close(dev);
		return -EINVAL;
	}

	printf("Device %s opened: %s.\n", devname, cap.card);
	return dev;
}

static void uvc_set_control(int dev, unsigned int id, int value)
{
	struct v4l2_control ctrl;
	int ret;

	ctrl.id = id;
	ctrl.value = value;

	ret = ioctl(dev, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		printf("unable to set gain control: %s (%d).\n",
				strerror(errno), errno);
		return;
	}
}

static int video_set_format(int dev, unsigned int w, unsigned int h, unsigned int format)
{
	struct v4l2_format fmt;
	int ret;

	printf("video_set_format: width: %u height: %u\n");

	memset(&fmt, 0, sizeof fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = w;
	fmt.fmt.pix.height = h;
	fmt.fmt.pix.pixelformat = format;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	ret = ioctl(dev, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("Unable to set format: %d.\n", errno);
		return ret;
	}

	printf("Video format set: width: %u height: %u buffer size: %u\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.sizeimage);
	return 0;
}

static int video_set_framerate(int dev)
{
	struct v4l2_streamparm parm;
	int ret;

	memset(&parm, 0, sizeof parm);
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = ioctl(dev, VIDIOC_G_PARM, &parm);
	if (ret < 0) {
		printf("Unable to get frame rate: %d.\n", errno);
		return ret;
	}

	printf("Current frame rate: %u/%u\n",
			parm.parm.capture.timeperframe.numerator,
			parm.parm.capture.timeperframe.denominator);

	parm.parm.capture.timeperframe.numerator = 1;	/* FIXME */
	parm.parm.capture.timeperframe.denominator = 25;

	ret = ioctl(dev, VIDIOC_S_PARM, &parm);
	if (ret < 0) {
		printf("Unable to set frame rate: %d.\n", errno);
		return ret;
	}

	ret = ioctl(dev, VIDIOC_G_PARM, &parm);
	if (ret < 0) {
		printf("Unable to get frame rate: %d.\n", errno);
		return ret;
	}

	printf("Frame rate set: %u/%u\n",
			parm.parm.capture.timeperframe.numerator,
			parm.parm.capture.timeperframe.denominator);

	return 0;
}

static int video_reqbufs(int dev, int nbufs)
{
	struct v4l2_requestbuffers rb;
	int ret;

	memset(&rb, 0, sizeof rb);
	rb.count = nbufs;
	rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rb.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(dev, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		printf("Unable to allocate buffers: %d.\n", errno);
		return ret;
	}

	printf("%u buffers allocated.\n", rb.count);
	return rb.count;
}

static int video_enable(int dev, int enable)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

	ret = ioctl(dev, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("Unable to %s capture: %d.\n",
				enable ? "start" : "stop", errno);
		return ret;
	}

	return 0;
}

static void video_query_menu(int dev, unsigned int id)
{
	struct v4l2_querymenu menu;
	int ret;

	menu.index = 0;
	while (1) {
		menu.id = id;
		ret = ioctl(dev, VIDIOC_QUERYMENU, &menu);
		if (ret < 0)
			break;

		printf("  %u: %.32s\n", menu.index, menu.name);
		menu.index++;
	};
}

static void video_list_controls(int dev)
{
	struct v4l2_queryctrl query;
    struct v4l2_control ctrl;
    char value[12];
    int ret;

#ifndef V4L2_CTRL_FLAG_NEXT_CTRL
    unsigned int i;

    for (i = V4L2_CID_BASE; i <= V4L2_CID_LASTP1; ++i) {
        query.id = i;
#else
    query.id = 0;
    while (1) {
        query.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
#endif
        ret = ioctl(dev, VIDIOC_QUERYCTRL, &query);
        if (ret < 0)
            break;

        if (query.flags & V4L2_CTRL_FLAG_DISABLED)
            continue;

        ctrl.id = query.id;
        ret = ioctl(dev, VIDIOC_G_CTRL, &ctrl);
        if (ret < 0)
            strcpy(value, "n/a");
        else
            sprintf(value, "%d", ctrl.value);

        printf("control 0x%08x %s min %d max %d step %d default %d current %s.\n",
            query.id, query.name, query.minimum, query.maximum,
            query.step, query.default_value, value);

        if (query.type == V4L2_CTRL_TYPE_MENU)
            video_query_menu(dev, query.id);

    }
}

int enum_frame_intervals(int dev, __u32 pixfmt, __u32 width, __u32 height)
{
	int ret;
	struct v4l2_frmivalenum fival;

	memset(&fival, 0, sizeof(fival));
	fival.index = 0;
	fival.pixel_format = pixfmt;
	fival.width = width;
	fival.height = height;
	printf("\tTime interval between frame: ");
	while ((ret = ioctl(dev, VIDIOC_ENUM_FRAMEINTERVALS, &fival)) == 0) {
		if (fival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
			printf("%u/%u, ",
					fival.discrete.numerator, fival.discrete.denominator);
		} else if (fival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
			printf("{min { %u/%u } .. max { %u/%u } }, ",
					fival.stepwise.min.numerator, fival.stepwise.min.numerator,
					fival.stepwise.max.denominator, fival.stepwise.max.denominator);
			break;
		} else if (fival.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
			printf("{min { %u/%u } .. max { %u/%u } / "
					"stepsize { %u/%u } }, ",
					fival.stepwise.min.numerator, fival.stepwise.min.denominator,
					fival.stepwise.max.numerator, fival.stepwise.max.denominator,
					fival.stepwise.step.numerator, fival.stepwise.step.denominator);
			break;
		}
		fival.index++;
	}
	printf("\n");
	if (ret != 0 && errno != EINVAL) {
		perror("ERROR enumerating frame intervals");
		return errno;
	}

	return 0;
}

static int enum_frame_sizes(int dev, __u32 pixfmt)
{
	int ret;
	struct v4l2_frmsizeenum fsize;

	memset(&fsize, 0, sizeof(fsize));
	fsize.index = 0;
	fsize.pixel_format = pixfmt;
	while ((ret = ioctl(dev, VIDIOC_ENUM_FRAMESIZES, &fsize)) == 0) {
		if (fsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
			printf("{ discrete: width = %u, height = %u }\n",
					fsize.discrete.width, fsize.discrete.height);
			ret = enum_frame_intervals(dev, pixfmt,
					fsize.discrete.width, fsize.discrete.height);
			if (ret != 0)
				printf("  Unable to enumerate frame sizes.\n");
		} else if (fsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
			printf("{ continuous: min { width = %u, height = %u } .. "
					"max { width = %u, height = %u } }\n",
					fsize.stepwise.min_width, fsize.stepwise.min_height,
					fsize.stepwise.max_width, fsize.stepwise.max_height);
			printf("  Refusing to enumerate frame intervals.\n");
			break;
		} else if (fsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
			printf("{ stepwise: min { width = %u, height = %u } .. "
					"max { width = %u, height = %u } / "
					"stepsize { width = %u, height = %u } }\n",
					fsize.stepwise.min_width, fsize.stepwise.min_height,
					fsize.stepwise.max_width, fsize.stepwise.max_height,
					fsize.stepwise.step_width, fsize.stepwise.step_height);
			printf("  Refusing to enumerate frame intervals.\n");
			break;
		}
		fsize.index++;
	}
	if (ret != 0 && errno != EINVAL) {
		perror("ERROR enumerating frame sizes");
		return errno;
	}

	return 0;
}

static void video_list_formats(int dev)
{
	struct v4l2_fmtdesc fmt;
	int ret;

	memset(&fmt, 0, sizeof(fmt));
	fmt.index = 0;
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	while ((ret = ioctl(dev, VIDIOC_ENUM_FMT, &fmt)) == 0) {
		printf("{ pixelformat = '%c%c%c%c', description = '%s' }\n",
				fmt.pixelformat & 0xFF, (fmt.pixelformat >> 8) & 0xFF,
				(fmt.pixelformat >> 16) & 0xFF, (fmt.pixelformat >> 24) & 0xFF,
				fmt.description);
		ret = enum_frame_sizes(dev, fmt.pixelformat);
		if(ret != 0)
			printf("  Unable to enumerate frame sizes.\n");

		fmt.index++;
	}

	if (errno != EINVAL) {
		perror("ERROR enumerating frame formats");
	}
}

static void video_enum_inputs(int dev)
{
	struct v4l2_input input;
	unsigned int i;
	int ret;

	for (i = 0; ; ++i) {
		memset(&input, 0, sizeof input);
		input.index = i;
		ret = ioctl(dev, VIDIOC_ENUMINPUT, &input);
		if (ret < 0)
			break;

		if (i != input.index)
			printf("Warning: driver returned wrong input index "
					"%u.\n", input.index);

		printf("Input %u: %s.\n", i, input.name);
	}
}

static int video_get_input(int dev)
{
	__u32 input;
	int ret;

	ret = ioctl(dev, VIDIOC_G_INPUT, &input);
	if (ret < 0) {
		printf("Unable to get current input: %s.\n", strerror(errno));
		return ret;
	}

	return input;
}

static int video_set_input(int dev, unsigned int input)
{
	__u32 _input = input;
	int ret;

	ret = ioctl(dev, VIDIOC_S_INPUT, &_input);
	if (ret < 0)
		printf("Unable to select input %u: %s.\n", input,
				strerror(errno));

	return ret;
}


#define V4L_BUFFERS_DEFAULT	3
#define V4L_BUFFERS_MAX		32

static void usage(const char *argv0)
{
	printf("Usage: %s [options] device\n", argv0);
	printf("Supported options:\n");
	printf("-d, --delay             Delay (in ms) before requeuing buffers\n");
	printf("-f, --format format	Set the video format (mjpg/yuyv)\n");
	printf("-h, --help		Show this help screen\n");
	printf("-i, --input input	Select the video input\n");
	printf("-l, --list-controls	List available controls\n");
	printf("-L, --list-formats	List available formats\n");
	printf("-n, --nbufs n		Set the number of video buffers\n");
	printf("-s, --size WxH		Set the frame size\n");
	printf("-S, --stream		Stream capturing mode\n");
	printf("    --enum-inputs	Enumerate inputs\n");
	printf("    --skip n		Skip the first n frames\n");
}

#define OPT_ENUM_INPUTS		256
#define OPT_SKIP_FRAMES		257

static struct option opts[] = {
	{"delay", 1, 0, 'd'},
	{"enum-inputs", 0, 0, OPT_ENUM_INPUTS},
	{"format", 1, 0, 'f'},
	{"help", 0, 0, 'h'},
	{"input", 1, 0, 'i'},
	{"list-controls", 0, 0, 'l'},
	{"list-formats", 0, 0, 'L'},
	{"stream", 0, 0, 'S'},
	{"size", 1, 0, 's'},
	{"skip", 1, 0, OPT_SKIP_FRAMES},
	{0, 0, 0, 0}
};

static void sig (int sig)
{
	close_framebuffer(&vd);
	printf("signal %d caught\n", sig);
	fflush(stdout);
	exit (1);
}

static int capture_file_id (int index)
{
	char s[256];

	while (1) {
		sprintf (s, "%s%03d.jpg", FILE_SAVE_PREFIX, index);
		if (access (s, F_OK) == 0)
			index++;
		else 
			break;
	}
	return index;
}

int main(int argc, char *argv[])
{
	char filename[256];
	int dev, ret;

	/* Options parsings */
	int do_enum_inputs = 0, do_capture = 0, do_stream = 0;
	int do_list_controls = 0, do_list_formats = 0, do_set_input = 0;
	char *endptr;
	int c;
	unsigned int input = 0;
	unsigned int skip = 0;

	/* V4L */
	void *mem[V4L_BUFFERS_MAX];
	unsigned int pixelformat = V4L2_PIX_FMT_YUYV;
	unsigned int width = 640;
	unsigned int height = 480;
	unsigned int nbufs = V4L_BUFFERS_DEFAULT;
	struct v4l2_buffer buf;

	/* Capture loop */
	struct timeval start, end, ts;
	unsigned int delay = 0, nframes = ~0;
	FILE *file;
	double fps;
	unsigned int i;

	/* Jpeg */
	struct jpeg_decompress_struct cinfo_decompress;
	struct jpeg_compress_struct cinfo_compress;
	struct jpeg_error_mgr jerr_decompress;
	struct jpeg_error_mgr jerr_compress;
	JSAMPARRAY buffer;
	JSAMPROW row_pointer[1];
	unsigned char *tmpBuffer, *dispBuffer;
	int row_stride;

	signal(SIGSEGV, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	pthread_mutex_init (&mutex, NULL);

	opterr = 0;
	while ((c = getopt_long(argc, argv, "c::d:f:hi:lLn:s:S", opts, NULL)) != -1) {

		switch (c) {
			case 'c':
				do_capture = 1;
				if (optarg)
					nframes = atoi(optarg);
				break;
			case 'd':
				delay = atoi(optarg);
				break;
			case 'f':
				if (strcmp(optarg, "mjpg") == 0)
					pixelformat = V4L2_PIX_FMT_MJPEG;
				else if (strcmp(optarg, "yuyv") == 0)
					pixelformat = V4L2_PIX_FMT_YUYV;
				else {
					printf("Unsupported video format '%s'\n", optarg);
					return 1;
				}
				break;
			case 'h':
				usage(argv[0]);
				return 0;
			case 'i':
				do_set_input = 1;
				input = atoi(optarg);
				break;
			case 'l':
				do_list_controls = 1;
				break;
			case 'L':
				do_list_formats = 1;
				break;
			case 'n':
				nbufs = atoi(optarg);
				if (nbufs > V4L_BUFFERS_MAX)
					nbufs = V4L_BUFFERS_MAX;
				break;
			case 's':
				width = strtol(optarg, &endptr, 10);
				if (*endptr != 'x' || endptr == optarg) {
					printf("Invalid size '%s'\n", optarg);
					return 1;
				}
				height = strtol(endptr + 1, &endptr, 10);
				if (*endptr != 0) {
					printf("Invalid size '%s'\n", optarg);
					return 1;
				}
				break;
			case 'S':
				do_stream = 1;
				break;
			case OPT_ENUM_INPUTS:
				do_enum_inputs = 1;
				break;
			case OPT_SKIP_FRAMES:
				skip = atoi(optarg);
				break;
			default:
				printf("Invalid option -%c\n", c);
				printf("Run %s -h for help.\n", argv[0]);
				return 1;
		}
	}

	if (optind >= argc) {
		usage(argv[0]);
		return 1;
	}

	ret = open_framebuffer(FB_FILE, &vd);
	if (ret == 0){
		printf("open framebuffer error!\n");
		return 0;	
	} 

	button_init ();
	ts_openDev ();

	/* Open the video device. */
	dev = video_open(argv[optind]);
	if (dev < 0)
		return dev;

	if (do_list_controls){
		video_list_controls(dev);
		return 0;
	}	

	if (do_list_formats){
		video_list_formats(dev);
		return 0;
	}

	if (do_enum_inputs)
		video_enum_inputs(dev);

	if (do_set_input)
		video_set_input(dev, input);

	ret = video_get_input(dev);
	printf("Input %d selected\n", ret);

	/* Set the video format. */
	if (video_set_format(dev, width, height, pixelformat) < 0) {
		close(dev);
		return 1;
	}

	/* Set the frame rate. */
	if (video_set_framerate(dev) < 0) {
		close(dev);
		return 1;
	}

	/* Allocate buffers. */
	if ((int)(nbufs = video_reqbufs(dev, nbufs)) < 0) {
		close(dev);
		return 1;
	}

	/* Map the buffers. */
	for (i = 0; i < nbufs; ++i) {
		memset(&buf, 0, sizeof buf);
		buf.index = i;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(dev, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			printf("Unable to query buffer %u (%d).\n", i, errno);
			close(dev);
			return 1;
		}
		printf("length: %u offset: %u\n", buf.length, buf.m.offset);

		mem[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev, buf.m.offset);
		if (mem[i] == MAP_FAILED) {
			printf("Unable to map buffer %u (%d)\n", i, errno);
			close(dev);
			return 1;
		}
		printf("Buffer %u mapped at address %p.\n", i, mem[i]);
	}

	/* Start streaming. */
	video_enable(dev, 1);
	usleep(1000*1000);

	/* Queue the buffers. */
	for (i = 0; i < nbufs; ++i) {
		memset(&buf, 0, sizeof buf);
		buf.index = i;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(dev, VIDIOC_QBUF, &buf);
		if (ret < 0) {
			printf("Unable to queue buffer (%d).\n", errno);
			close(dev);
			return 1;
		}
	}

	cinfo_decompress.err = jpeg_std_error(&jerr_decompress);
	cinfo_compress.err = jpeg_std_error(&jerr_compress);
	jpeg_create_decompress(&cinfo_decompress);
	jpeg_create_compress(&cinfo_compress);

	for (i = 0; i < nframes; ++i) {
		int buttondown;
		/* Dequeue a buffer. */
		memset(&buf, 0, sizeof buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(dev, VIDIOC_DQBUF, &buf);
		if (ret < 0) {
			printf("Unable to dequeue buffer (%d).\n", errno);
			close(dev);
			return 1;
		}

		gettimeofday(&ts, NULL);
		printf("[%5u] %6u bytes, %ld.%06ld %ld.%06ld\n", i, buf.bytesused, \
				buf.timestamp.tv_sec, buf.timestamp.tv_usec, \
				ts.tv_sec, ts.tv_usec);

		if (i == 0)
			start = ts;

		pthread_mutex_lock (&mutex);
		buttondown = buttonSnapshotPressed;
		pthread_mutex_unlock (&mutex);
		if (buttondown) {
			pthread_mutex_lock (&mutex);
			buttonSnapshotPressed = 0;
			pthread_mutex_unlock (&mutex);
			if(pixelformat == V4L2_PIX_FMT_MJPEG){
				sprintf(filename, "/tmp/capture.jpg");
				file = fopen(filename, "wb");
				if (file != NULL) {
					fwrite(mem[buf.index], buf.bytesused, 1, file);
					fclose(file);
				}
			}
			else if(pixelformat == V4L2_PIX_FMT_YUYV){
				char s[256];
				static int idx = 0;

				idx = capture_file_id (idx);
				sprintf (filename, "%s%03d.jpg", FILE_SAVE_PREFIX, idx);
				file = fopen(filename, "wb");
				if (file != NULL) {
					tmpBuffer = malloc(width * height * 2);
					dispBuffer = malloc(width * height * 3);

					memcpy(tmpBuffer, mem[buf.index], buf.bytesused);
					convert_to_rgb24(tmpBuffer, dispBuffer, width, height);

					jpeg_stdio_dest(&cinfo_compress, file);

					cinfo_compress.image_width = width;
					cinfo_compress.image_height = height;
					cinfo_compress.input_components = 3;
					cinfo_compress.in_color_space = JCS_RGB;
					jpeg_set_defaults(&cinfo_compress);
					jpeg_set_quality(&cinfo_compress, 50, TRUE);

					jpeg_start_compress(&cinfo_compress, TRUE);

					row_stride = width * 3;
					while (cinfo_compress.next_scanline < cinfo_compress.image_height) {
						row_pointer[0] = & dispBuffer[cinfo_compress.next_scanline * row_stride];
						(void) jpeg_write_scanlines(&cinfo_compress, row_pointer, 1);
					}

					jpeg_finish_compress(&cinfo_compress);

					fclose(file);
					sprintf (s, "Save: %s", filename);
					put_string_center(vd.vinfo.xres >> 1, vd.vinfo.yres >> 1, s, 1);
					/* delay in order to see */
					sleep (1);
					idx++;
				}
			}
		}

		if (do_stream && !skip){
			if(pixelformat == V4L2_PIX_FMT_MJPEG){
				file = fopen(filename, "rb");
				if(file == NULL){
					printf("Open file error!\n");
					return 0;
				}

				jpeg_stdio_src(&cinfo_decompress, file);

				jpeg_read_header(&cinfo_decompress, TRUE);

				jpeg_start_decompress(&cinfo_decompress);			

				row_stride = cinfo_decompress.output_width * cinfo_decompress.output_components;

				buffer = (*cinfo_decompress.mem->alloc_sarray)
					((j_common_ptr) &cinfo_decompress, JPOOL_IMAGE, row_stride, cinfo_decompress.output_height);

				int rowsRead = 0;
				while (cinfo_decompress.output_scanline < cinfo_decompress.output_height){
					rowsRead += jpeg_read_scanlines(&cinfo_decompress, &buffer[rowsRead], cinfo_decompress.output_height - rowsRead);	
				}

				jpeg_to_framebuffer(&vd, cinfo_decompress.output_width, cinfo_decompress.output_height, 0, 0, buffer);

				jpeg_finish_decompress(&cinfo_decompress);

				fclose(file);		
			}
			else if(pixelformat == V4L2_PIX_FMT_YUYV){
				tmpBuffer = malloc(width * height * 2);
				dispBuffer = malloc(width * height * 4);

				memcpy(tmpBuffer, mem[buf.index], buf.bytesused);
				if (vd.vinfo.bits_per_pixel == 16) {
					convert_to_rgb16(tmpBuffer, dispBuffer, width, height); 
				} 
				else if (vd.vinfo.bits_per_pixel == 32) {
					convert_to_rgb32(tmpBuffer, dispBuffer, width, height);	
				}

				yuv_to_framebuffer(&vd, width, height, 0, 0, (unsigned char *)dispBuffer);

				free(dispBuffer);
				free(tmpBuffer);
			}
		}	

		if (skip)
			--skip;

		if (delay > 0)
			usleep(delay * 1000);

		/* Requeue the buffer. */
		ret = ioctl(dev, VIDIOC_QBUF, &buf);
		if (ret < 0) {
			printf("Unable to requeue buffer (%d).\n", errno);
			close(dev);
			return 1;
		}

		fflush(stdout);
	}

	gettimeofday(&end, NULL);

	jpeg_destroy_decompress(&cinfo_decompress);
	jpeg_destroy_compress(&cinfo_compress);

	/* Stop streaming. */
	video_enable(dev, 0);

	end.tv_sec -= start.tv_sec;
	end.tv_usec -= start.tv_usec;
	if (end.tv_usec < 0) {
		end.tv_sec--;
		end.tv_usec += 1000000;
	}
	fps = (i-1)/(end.tv_usec+1000000.0*end.tv_sec)*1000000.0;

	printf("Captured %u frames in %lu.%06lu seconds (%f fps).\n",
			i-1, end.tv_sec, end.tv_usec, fps);

	close(dev);

	return 0;
}

