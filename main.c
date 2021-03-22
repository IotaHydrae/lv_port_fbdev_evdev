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
    unsigned int *fb_base;
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

    /*  */
    bool touchdown;
    unsigned short last_x;
    unsigned short last_y;

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


/* old  */
static int fd_fb;
static struct fb_var_screeninfo var;
static int screen_size;
static unsigned char *fb_base;
static unsigned int line_width, pixel_width;

/* touchpad data */
static bool my_touchpad_touchdown = false;
static int16_t last_x = 0;
static int16_t last_y = 0;

/* poll event of input device touchpad*/
static int tp_fd;
nfds_t nfds = 1;
struct pollfd mpollfd[1];
struct input_event my_event;


static struct fbdev_struct fbdev_info;
static struct screen_struct screen_info;



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
    if(ioctl(fbdev_info.fd_fb, FBIOGET_VSCREENINFO, &var) < 0) {
        handle_error("can not ioctl");
    }
    /* already get the var screen info */
    line_width = var.xres * var.bits_per_pixel / 8;
    pixel_width = var.bits_per_pixel / 8;
    screen_size = var.xres * var.yres * var.bits_per_pixel / 8;
    screen_info.width = var.xres;
    screen_info.height = var.yres;
    screen_info.line_width = var.xres * var.bits_per_pixel / 8;
    screen_info.pixel_width = var.bits_per_pixel / 8;
    screen_info.screen_size = var.xres * var.yres * var.bits_per_pixel / 8;
    /* mmap the fb_base */
    fbdev_info.fb_base = (unsigned int *)mmap(NULL, screen_info.screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbdev_info.fd_fb, 0);
    if(fbdev_info.fb_base == (unsigned int *) -1) {
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
    tp_fd = open(DEFAULT_LINUX_TOUCHPAD_PATH, O_RDWR);
    if(tp_fd < 0) {
        handle_error("can not open /dev/input/event1");
    }
    mpollfd[0].fd = tp_fd;
    mpollfd[0].events = POLLIN;
    mpollfd[0].revents = 0;
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
    len = poll(mpollfd, nfds, INPUT_SAMEPLING_TIME);
    if(len > 0) {       /* There is data to read */
        len = read(tp_fd, &my_event, sizeof(my_event));
        if(len == sizeof(my_event)) {   /* On success */
            //printf("get event: type = 0x%x,code = 0x%x,value = 0x%x\n",my_event.type,my_event.code,my_event.value);
            switch(my_event.type) {
                case EV_SYN:    /* Sync event. Do nonthing */
                    break;
                case EV_KEY:    /* Key event. Provide the pressure data of touchscreen*/
                    if(my_event.code == BTN_TOUCH) {        /* Screen touch event */
                        if(1 == my_event.value) {       /* Touch down */
                            my_touchpad_touchdown = true;
                        } else
                            if(0 == my_event.value) {  /* Touch up */
                                my_touchpad_touchdown = false;
                            } else                          /* Unexcepted data */
                            { goto touchdown_err; }
                    }
                    break;
                case EV_ABS:    /* Abs event. Provide the position data of touchscreen*/
                    if(my_event.code == ABS_MT_POSITION_X)
                    { last_x = my_event.value; }
                    if(my_event.code == ABS_MT_POSITION_Y)
                    { last_y = my_event.value; }
                    break;
                default:
                    break;
            }
        } else {          /* On error */
            handle_error("read error\n");
        }
    } else
        if(len == 0) { /* Time out */
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
    data->state = my_touchpad_touchdown ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if(data->state == LV_INDEV_STATE_PR) {
        data->point.x = last_x;
        data->point.y = last_y;
    }
    return false;
}

/* main thread of lvgl */
int main(void)
{
    lv_init();
    my_fb_init();
    my_touchpad_init();
    
    static lv_disp_buf_t disp_buf;	/* lvgl display buffer */
    
    static lv_color_t buf[DISP_BUF_SIZE];	/* Declare a buffer for 1/10 screen size */
    
    lv_disp_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);	/* Initialize the display buffer */
    
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);	/* register display driver */
    
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);	/* register input device driver */
    
    lv_task_create(my_touchpad_thread, SYSTEM_RESPONSE_TIME, 
					LV_TASK_PRIO_MID, NULL);	/* create a thread to collect screen input data */
	
    /* App here */
    lv_demo_benchmark();
    //lv_demo_widgets();
    //lv_demo_printer();
    //lv_demo_music();
    //first_app_examples();
    while(1) {
        lv_task_handler();
        usleep(SYSTEM_RESPONSE_TIME * 1000);
        lv_tick_inc(SYSTEM_RESPONSE_TIME);
    }
    return 0;
}

