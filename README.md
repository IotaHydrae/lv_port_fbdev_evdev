# lvgl_framebuffer

#### 介绍
将LVGL移植到Linux framebuffer

我提供了4种读取输入事件的方式，需要在`main.c`中设置设置宏`INPUT_READ_MODE`来进行事件读取方式的切换

实际项目中应该使用查询方式或者异步通知方式来处理输入事件已达到快速相应的目的。

#### 如何使用
如果你想使用本项目的话，需要自行准备这几个lvgl库
```
lv_drivers
lv_examples
lvgl
```
附上官方链接
[lv_drivers](https://github.com/lvgl/lv_drivers.git)
[lv_examples](https://github.com/lvgl/lv_examples.git)
[lvgl](https://github.com/lvgl/lvgl.git)

lvgl配置文件
```c
lv_conf.h
lv_drv_conf.h
lv_ex_conf.h
```