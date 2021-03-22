#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
//#include <poll.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/fb.h>
#include <linux/input.h>

#include "lvgl/lvgl.h"
#include "lv_examples/lv_examples.h"
#include "my_apps/my_apps.h"

/*
    Linux frame buffer like /dev/fb0
    which includes Single-board computers too like Raspberry Pi
*/

/* error handler */
#define handle_error(msg) do {perror(msg);} \
    while(0)

#define DEFAULT_LINUX_FB_PATH "/dev/fb0"
#define DEFAULT_LINUX_TOUCHPAD_PATH "/dev/input/event1"

/*
* Input system read mode.
* 0     query
* 1     poll
* 2     signal
*/
#define INPUT_READ_MODE 1

#if (INPUT_READ_MODE == 1)
#include <poll.h>
#elif (INPUT_READ_MODE == 2)
#include <signal.h>
#endif

#define DISP_BUF_SIZE LV_HOR_RES_MAX * LV_VER_RES_MAX /10
/* Default to 5 milliseconds to keep the system responsive */
#define SYSTEM_RESPONSE_TIME 4
#define INPUT_SAMEPLING_TIME 1



/* Framebuffer info */
struct fbdev_struct {
    int fd_fb;
    unsigned char *fb_base;

    struct fb_var_screeninfo fb_var;
};

/* Lcd info */
struct screen_struct  {
    int width;
    int height;
    int screen_size;
    int line_width;
    int bpp;
    int pixel_width;
};

/* Input device */
struct indev_struct   {
    /*  */
    int tp_fd;

#ifndef DEFAULT_LINUX_TOUCHPAD_PATH
    char *event;
#endif

    /* Data of touch panel */
    bool touchdown;
    unsigned short last_x;  /* Abs_mt_x */
    unsigned short last_y;  /* Abs_mt_y */

#if (INPUT_READ_MODE == 0)
    /* query mode */

#elif (INPUT_READ_MODE == 1)
#define DEVICE_NUM 1
    /* poll mode */
    nfds_t nfds;
    struct pollfd mpollfd[DEVICE_NUM];


#elif (INPUT_READ_MODE == 2)
    /* signal mode */

#endif

    struct input_event indev_event;
};


static struct fbdev_struct fbdev_info;  /* framebuffer */
static struct screen_struct screen_info;    /* scree */
static struct indev_struct indev_info;  /* touchpad data */


void my_fb_init(void);
void my_touchpad_init(void);
void my_touchpad_thread(lv_task_t *task);

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
bool my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data);


/**
 * Get the screen info.
 * mmap the framebuffer to memory.
 * clear the screen.
 * @param
 * @return
 */
void my_fb_init(void)
{
    fbdev_info.fd_fb = open(DEFAULT_LINUX_FB_PATH, O_RDWR);
    if(fbdev_info.fd_fb < 0) {
        handle_error("can not open /dev/fb0");
    }
    /* already get fd_fb */
    if(ioctl(fbdev_info.fd_fb, FBIOGET_VSCREENINFO, &fbdev_info.fb_var) < 0) {
        handle_error("can not ioctl");
    }
    /* already get the var screen info */
    screen_info.width = fbdev_info.fb_var.xres;
    screen_info.height = fbdev_info.fb_var.yres;
    screen_info.line_width = fbdev_info.fb_var.xres * fbdev_info.fb_var.bits_per_pixel / 8;
    screen_info.pixel_width = fbdev_info.fb_var.bits_per_pixel / 8;
    screen_info.screen_size = fbdev_info.fb_var.xres * fbdev_info.fb_var.yres * fbdev_info.fb_var.bits_per_pixel / 8;
    /* mmap the fb_base */
    fbdev_info.fb_base = (unsigned char *)mmap(NULL, screen_info.screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbdev_info.fd_fb, 0);
    if(fbdev_info.fb_base == (unsigned char *) -1) {
        handle_error("can not mmap frame buffer");
    }
    /* alreay get the start addr of framebuffer */
    memset(fbdev_info.fb_base, 0xff, screen_info.screen_size); /* clear the screen */
}

/**
 * Just initialize the touchpad
 * @param
 * @return
 */
void my_touchpad_init(void)
{
    indev_info.tp_fd = open(DEFAULT_LINUX_TOUCHPAD_PATH, O_RDWR);
    if(indev_info.tp_fd < 0) {
        handle_error("can not open /dev/input/event1");
    }
    indev_info.nfds = 1;
    indev_info.mpollfd[0].fd = indev_info.tp_fd;
    indev_info.mpollfd[0].events = POLLIN;
    indev_info.mpollfd[0].revents = 0;
}


/**
 * A thread to collect input data of screen.
 * @param
 * @return
 */
void my_touchpad_thread(lv_task_t *task)
{
    (void)task;
    int len;
    len = poll(indev_info.mpollfd, indev_info.nfds, INPUT_SAMEPLING_TIME);
    if(len > 0) {       /* There is data to read */
        len = read(indev_info.tp_fd, &indev_info.indev_event,
                   sizeof(indev_info.indev_event));
        if(len == sizeof(indev_info.indev_event)) {   /* On success */
            switch(indev_info.indev_event.type) {
                case EV_SYN:    /* Sync event. Do nonthing */
                    break;
                case EV_KEY:    /* Key event. Provide the pressure data of touchscreen*/
                    if(indev_info.indev_event.code == BTN_TOUCH) {        /* Screen touch event */
                        if(1 == indev_info.indev_event.value) {       /* Touch down */
                            indev_info.touchdown = true;
                        } else if(0 == indev_info.indev_event.value) { /* Touch up */
                            indev_info.touchdown = false;
                        } else {                        /* Unexcepted data */
                            goto touchdown_err;
                        }
                    }
                    break;
                case EV_ABS:    /* Abs event. Provide the position data of touchscreen*/
                    if(indev_info.indev_event.code == ABS_MT_POSITION_X) {
                        indev_info.last_x = indev_info.indev_event.value;
                    }
                    if(indev_info.indev_event.code == ABS_MT_POSITION_Y) {
                        indev_info.last_y = indev_info.indev_event.value;
                    }
                    break;
                default:
                    break;
            }
        } else {          /* On error */
            handle_error("read error\n");
        }
    } else if(len == 0) { /* Time out */
        /* Do nothing */
    } else { /* Error */
        handle_error("poll error!");
    }
touchdown_err:      /* Do nothing. Just return and ready for next event come. */
    return;
}


/**
 * releated to disp_drv.flush_cb
 * @param disp
 * @param area
 * @param color_p
 * @return
 */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t x, y;
    for(y = area->y1; y <= area->y2; y++) {
        for(x = area->x1; x <= area->x2; x++) {
            memcpy(fbdev_info.fb_base + x * screen_info.pixel_width + y * screen_info.line_width,
                   &color_p->full, sizeof(lv_color_t));
            color_p++;
        }
    }
    lv_disp_flush_ready(disp);
}

/**
 * releated to indev_drv.readcb
 * @param indev
 * @param data
 * @return false
 */
bool my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data)
{
    /* store the collected data */
    data->state = indev_info.touchdown ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if(data->state == LV_INDEV_STATE_PR) {
        data->point.x = indev_info.last_x;
        data->point.y = indev_info.last_y;
    }
    return false;
}

/* main thread of lvgl */
int main(void)
{
    lv_init();
    my_fb_init();
    my_touchpad_init();
    static lv_disp_buf_t disp_buf;  /* lvgl display buffer */
    static lv_color_t buf[DISP_BUF_SIZE];   /* Declare a buffer for 1/10 screen size */
    lv_disp_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);  /* Initialize the display buffer */
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);    /* register display driver */
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);  /* register input device driver */
    lv_task_create(my_touchpad_thread, SYSTEM_RESPONSE_TIME,
                   LV_TASK_PRIO_MID, NULL);    /* create a thread to collect screen input data */
    /* App here */
    //lv_demo_benchmark();
    //lv_demo_widgets();
    lv_demo_printer();
    //lv_demo_music();
    //first_app_examples();
    while(1) {
        lv_task_handler();
        usleep(SYSTEM_RESPONSE_TIME * 1000);
        lv_tick_inc(SYSTEM_RESPONSE_TIME);
    }
    return 0;
}

