#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include <tslib.h>
#include <pthread.h>
#include "font_8x8.h"

#define anchor()        printf("-> %s [%d]\n", __func__, __LINE__)
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
extern fb_v41 vd;
extern int buttonSnapshotPressed;
extern pthread_mutex_t mutex;

static int initialized = 0;
static unsigned colormap [256];
static struct tsdev *ts;

static int palette [] =
{
	0x000000, 0xffe080, 0xffffff, 0xe0c0a0, 0x304050, 0x80b8c0
};
#define NR_COLORS (sizeof (palette) / sizeof (palette [0]))
#define NR_BUTTONS	1

static int button_palette[6] =
{
	1, 4, 2,
	1, 5, 0
};

static int fbdev_init (void);

void pixel (int x, int y, unsigned colidx)
{
	if ((x < 0 || x >= vd.vinfo.xres) ||
			(y < 0 || y >= vd.vinfo.yres))
		return;
	if (vd.fbp == NULL)
		return;	

	if (!initialized)
		fbdev_init ();

	switch (vd.vinfo.bits_per_pixel) {
		case 8:
			break;
		case 16: {
			unsigned short *fbp = (unsigned short *)vd.fbp;
			unsigned short *pix = fbp + x + y * vd.vinfo.xres;
			*pix = colormap[colidx];
			break;
		}
		case 24:
			break;
		case 32:
			break;
	}
}

void put_char(int x, int y, int c, int colidx)
{
	int i,j,bits;

	for (i = 0; i < font_vga_8x8.height; i++) {
		bits = font_vga_8x8.data [font_vga_8x8.height * c + i];
		for (j = 0; j < font_vga_8x8.width; j++, bits <<= 1)
			if (bits & 0x80)
				pixel (x + j, y + i, colidx);
	}
}

void put_string(int x, int y, char *s, unsigned colidx)
{
	int i;
	for (i = 0; *s; i++, x += font_vga_8x8.width, s++)
		put_char (x, y, *s, colidx);
}

void put_string_center(int x, int y, char *s, unsigned colidx)
{
	size_t sl = strlen (s);
        put_string (x - (sl / 2) * font_vga_8x8.width,
                    y - font_vga_8x8.height / 2, s, colidx);
}

void line (int x1, int y1, int x2, int y2, unsigned colidx)
{
	int tmp;
	int dx = x2 - x1;
	int dy = y2 - y1;

	if (abs (dx) < abs (dy)) {
		if (y1 > y2) {
			tmp = x1; x1 = x2; x2 = tmp;
			tmp = y1; y1 = y2; y2 = tmp;
			dx = -dx; dy = -dy;
		}
		x1 <<= 16;
		/* dy is apriori >0 */
		dx = (dx << 16) / dy;
		while (y1 <= y2) {
			pixel (x1 >> 16, y1, colidx);
			x1 += dx;
			y1++;
		}
	} else {
		if (x1 > x2) {
			tmp = x1; x1 = x2; x2 = tmp;
			tmp = y1; y1 = y2; y2 = tmp;
			dx = -dx; dy = -dy;
		}
		y1 <<= 16;
		dy = dx ? (dy << 16) / dx : 0;
		while (x1 <= x2) {
			pixel (x1, y1 >> 16, colidx);
			y1 += dy;
			x1++;
		}
	}
}

void rect (int x1, int y1, int x2, int y2, unsigned colidx)
{
	line (x1, y1, x2, y1, colidx);
	line (x2, y1, x2, y2, colidx);
	line (x2, y2, x1, y2, colidx);
	line (x1, y2, x1, y1, colidx);
}

void fillrect (int x1, int y1, int x2, int y2, unsigned colidx)
{
	int tmp;
	__u32 xres = vd.vinfo.xres;
	__u32 yres = vd.vinfo.yres;

	/* Clipping and sanity checking */
	if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
	if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }
	if (x1 < 0) x1 = 0; if ((__u32)x1 >= xres) x1 = xres - 1;
	if (x2 < 0) x2 = 0; if ((__u32)x2 >= xres) x2 = xres - 1;
	if (y1 < 0) y1 = 0; if ((__u32)y1 >= yres) y1 = yres - 1;
	if (y2 < 0) y2 = 0; if ((__u32)y2 >= yres) y2 = yres - 1;

	if ((x1 > x2) || (y1 > y2))
		return;

	for (y1; y1 <= y2; y1++) {
		int tmp;
		for (tmp = x1; tmp <= x2; tmp++) {
			pixel (tmp, y1, colidx);
		}
	}
}


static void setcolor(unsigned colidx, unsigned value)
{
	unsigned res;
	unsigned short red, green, blue;
	struct fb_cmap cmap;
	int fb_fd = vd.fbfd;

	switch (vd.vinfo.bits_per_pixel / 8) {
	default:
	case 1:
		res = colidx;
		red = (value >> 8) & 0xff00;
		green = value & 0xff00;
		blue = (value << 8) & 0xff00;
		cmap.start = colidx;
		cmap.len = 1;
		cmap.red = &red;
		cmap.green = &green;
		cmap.blue = &blue;
		cmap.transp = NULL;

		if (ioctl (fb_fd, FBIOPUTCMAP, &cmap) < 0)
			perror("ioctl FBIOPUTCMAP");
		break;
	case 2:
	case 3:
	case 4:
		red = (value >> 16) & 0xff;
		green = (value >> 8) & 0xff;
		blue = value & 0xff;
		res = ((red >> (8 - vd.vinfo.red.length)) << vd.vinfo.red.offset) |
                      ((green >> (8 - vd.vinfo.green.length)) << vd.vinfo.green.offset) |
                      ((blue >> (8 - vd.vinfo.blue.length)) << vd.vinfo.blue.offset);
	}
	colormap [colidx] = res;
}

static int fbdev_init (void)
{
	int i;

	for (i = 0; i < NR_COLORS; i++)
		setcolor (i, palette [i]);	
	return 0;
}

struct ts_button {
	int x, y, w, h;
	char *text;
	int flags;
#define BUTTON_ACTIVE 0x00000001
};

static struct ts_button buttons [NR_BUTTONS];

static void button_draw (struct ts_button *button)
{
	int s = (button->flags & BUTTON_ACTIVE) ? 3 : 0;
	rect (button->x, button->y, button->x + button->w - 1,
	      button->y + button->h - 1, button_palette [s]);
	fillrect (button->x + 1, button->y + 1,
		  button->x + button->w - 2,
		  button->y + button->h - 2, button_palette [s + 1]);
	put_string_center (button->x + button->w / 2,
			   button->y + button->h / 2,
			   button->text, button_palette [s + 2]);
}

static int button_handle (struct ts_button *button, struct ts_sample *samp)
{
	int inside = (samp->x >= button->x) && (samp->y >= button->y) &&
		(samp->x < button->x + button->w) &&
		(samp->y < button->y + button->h);

	if (samp->pressure > 0) {
		if (inside) {
			if (!(button->flags & BUTTON_ACTIVE)) {
				button->flags |= BUTTON_ACTIVE;
				button_draw (button);
			}
		}
		else if (button->flags & BUTTON_ACTIVE) {
			button->flags &= ~BUTTON_ACTIVE;
			button_draw (button);
		}
	} else if (button->flags & BUTTON_ACTIVE) {
		button->flags &= ~BUTTON_ACTIVE;
		button_draw (button);
    	return 1;
	}

	return 0;
}

void button_init (void)
{
	int i;

	memset (&buttons, 0, sizeof (buttons));
	buttons [0].w = vd.vinfo.xres / 4;
	buttons [0].h = 30;
	buttons [0].x = (vd.vinfo.xres - buttons [0].w) >> 1;
	buttons [0].y = vd.vinfo.yres - buttons [0].h - 5;
	buttons [0].text = "Capture";

	for (i = 0; i < NR_BUTTONS; i++) {
		button_draw (&buttons[i]);
	}
}

static void *tsScanThread (void)
{
	struct ts_sample samp;
	int ret;
	int i;

	while (1) {
		ret = ts_read(ts, &samp, 1);
		if (ret < 0) {
			perror("ts_read");
			exit(1);
		}

		for (i = 0; i < NR_BUTTONS; i++) {
			if (button_handle (&buttons [i], &samp)) {
				switch (i) {
				case 0:
					pthread_mutex_lock (&mutex);
					buttonSnapshotPressed = 1;
					pthread_mutex_unlock (&mutex);
					break;
				default:
					break;
				}
			}
		}
	}
}

int ts_openDev (void)
{
	pthread_t  thread;

	char *tsdevice = getenv("TSLIB_TSDEVICE");
	if (tsdevice == NULL) {
		printf ("Error: TSLIB_TSDEVICE not set");
		ts = NULL;
		return 1;
	}
	ts = ts_open(tsdevice,0);

	if (ts_config(ts)) {
		perror("ts_config");
		exit(1);
	}

	if (pthread_create (&thread, NULL, (void*)tsScanThread, NULL)) {
		printf("Error: Create pthread error!/n");
		exit (1);
	}

	return 0;
}


