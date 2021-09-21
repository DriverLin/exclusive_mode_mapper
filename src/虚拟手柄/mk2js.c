#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>

#define DOWN 0x1
#define UP 0x0

bool map_flag = false;
int uinput_fd;
char touch_dev_path[80];
char keyboard_dev_path[80];
char mouse_dev_path[80];
int keyboard_dev = 16;
int mouse_dev = 15;

struct input_event single_queue[16]; //整合设备信号队列
int s_len = 0;                       //整合队列长度
int mouse_last_x = 0;                //鼠标在手柄读取频率下的移动量
int mouse_last_y = 0;                //每次读取后清0 鼠标在下次读取之间的偏移累加

void readMouseLoop()
{
    while (map_flag)
    {
        usleep(4000);
        if (mouse_last_x != 0 || mouse_last_y != 0)
        {

            struct input_event X_EVENT = {.type = EV_ABS, .code = REL_Z, .value = 0x7FFF + mouse_last_x * 0X7FF};
            struct input_event Y_EVENT = {.type = EV_ABS, .code = REL_RZ, .value = 0X7FFF + mouse_last_y * 0X7FF};
            struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};
            write(uinput_fd, &X_EVENT, sizeof(struct input_event));
            write(uinput_fd, &Y_EVENT, sizeof(struct input_event));
            write(uinput_fd, &SYNC_EVENT, sizeof(struct input_event));

            printf("offset = [%d,%d]\n", mouse_last_x, mouse_last_y);
            mouse_last_x = 0;
            mouse_last_y = 0;
            //set js = x,y
        }
        else
        {
            printf("couhnt zero\n");
            struct input_event X_EVENT = {.type = EV_ABS, .code = REL_Z, .value = 0X7FFF};
            struct input_event Y_EVENT = {.type = EV_ABS, .code = REL_RZ, .value = 0X7FFF};
            struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};
            write(uinput_fd, &X_EVENT, sizeof(struct input_event));
            write(uinput_fd, &Y_EVENT, sizeof(struct input_event));
            write(uinput_fd, &SYNC_EVENT, sizeof(struct input_event));
        }
    }
}

void handelEventQueue() //处理所有事件
{
    int x = 0;
    int y = 0;
    for (int i = 0; i < s_len; i++) //main loop
    {
        if (single_queue[i].code == KEY_GRAVE && single_queue[i].value == UP) //独占和非独占都关注 ` 用于切换状态  `键不响应键盘映射
        {
            map_flag = !map_flag;
            printf("map_flag changed !\n");
            break;
        }
        if (map_flag)
        {
            if (single_queue[i].type == EV_REL)
            {
                if (single_queue[i].code == REL_X)
                    x = single_queue[i].value;
                else if (single_queue[i].code == REL_Y)
                    y = single_queue[i].value;
            }
            else if (single_queue[i].type == EV_KEY)
            {
                int keyCode = single_queue[i].code;
                int updown = single_queue[i].value;
                printf("%d : %d\n", keyCode, updown);
            }
        }
    }
    if (map_flag == true && (x != 0 || y != 0))
    { //有鼠标事件
        // printf("[%d,%d]\n",x,y);
        mouse_last_x += x;
        mouse_last_y += y;
    }
    s_len = 0;
}

int reveive_from_UDP(int port)
{
    int sin_len;
    unsigned char message[6];
    int socket_descriptor;
    struct sockaddr_in sin;
    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);
    sin_len = sizeof(sin);
    socket_descriptor = socket(AF_INET, SOCK_DGRAM, 0);
    bind(socket_descriptor, (struct sockaddr *)&sin, sizeof(sin));
    while (1)
    {
        memset(message, '\0', 6);
        recvfrom(socket_descriptor, message, sizeof(message), 0, (struct sockaddr *)&sin, &sin_len);
        if (!strcmp(message, "end")) //发送END  结束
            return 0;
        int code = message[0] * 0x100 + message[1];
        switch (code)
        {
        case ABS_Z: //包括 ZR : code,x,y  0~65535
        {
            struct input_event X_EVENT = {.type = EV_ABS, .code = REL_Z, .value = message[2] * 0x100 + message[3]};
            struct input_event Y_EVENT = {.type = EV_ABS, .code = REL_RZ, .value = message[4] * 0x100 + message[5]};
            struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};
            write(uinput_fd, &X_EVENT, sizeof(struct input_event));
            write(uinput_fd, &Y_EVENT, sizeof(struct input_event));
            write(uinput_fd, &SYNC_EVENT, sizeof(struct input_event));
            break;
        }
        case ABS_X: //包括Y: code,x,y  0~65535
        {
            struct input_event X_EVENT = {.type = EV_ABS, .code = REL_X, .value = message[2] * 0x100 + message[3]};
            struct input_event Y_EVENT = {.type = EV_ABS, .code = REL_Y, .value = message[4] * 0x100 + message[5]};
            struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};
            write(uinput_fd, &X_EVENT, sizeof(struct input_event));
            write(uinput_fd, &Y_EVENT, sizeof(struct input_event));
            write(uinput_fd, &SYNC_EVENT, sizeof(struct input_event));
            break;
        }
        case ABS_BRAKE: //: code,value 0~65535
        case ABS_GAS:
        {
            struct input_event VALUE = {.type = EV_ABS, .code = code, .value = message[2] * 0x100 + message[3]};
            struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};
            write(uinput_fd, &VALUE, sizeof(struct input_event));
            write(uinput_fd, &SYNC_EVENT, sizeof(struct input_event));
            break;
        }
        case ABS_HAT0X:
        case ABS_HAT0Y: // code,value 0~2
        {
            struct input_event VALUE = {.type = EV_ABS, .code = code, .value = message[3] - 1};
            struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};
            write(uinput_fd, &VALUE, sizeof(struct input_event));
            write(uinput_fd, &SYNC_EVENT, sizeof(struct input_event));
            break;
        }
        default: //KEY_EVENT code,value 0|1
        {
            struct input_event EV_KEY_EVENT = {.type = EV_KEY, .code = code, .value = message[3]};
            struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};
            write(uinput_fd, &EV_KEY_EVENT, sizeof(struct input_event));
            write(uinput_fd, &SYNC_EVENT, sizeof(struct input_event));
            break;
        }
        }
    }
    close(socket_descriptor);
    return 0;
}

int createController()
{
    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK); //opening of uinput
    if (uinput_fd < 0)
    {
        printf("Opening of uinput failed!\n");
        return 1;
    }
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);       //setting Gamepad keys
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_GAMEPAD); //A
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_EAST);    //B
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_NORTH);   //X
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_WEST);    //Y
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TL);      //LB
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TR);      //RB
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_SELECT);  //SELECT
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_START);   //START
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_THUMBL);  //LS
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_THUMBR);  //RS

    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);     //setting Gamepad thumbsticks
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X);     //X
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y);     //Y
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Z);     //RX
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_RZ);    //RY
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_BRAKE); //LT
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_GAS);   //RT
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_HAT0X); //RT
    ioctl(uinput_fd, UI_SET_ABSBIT, ABS_HAT0Y); //RT

    struct uinput_user_dev uidev; //setting the default settings of Gamepad
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Xbox Wireless Controller"); //Name of Gamepad
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor = 0x3;
    uidev.id.product = 0x3;
    uidev.id.version = 2;

    uidev.absmax[ABS_X] = 0xffff; //Parameters of thumbsticks
    uidev.absmin[ABS_X] = 0;

    uidev.absmax[ABS_Y] = 0xffff;
    uidev.absmin[ABS_Y] = 0;

    uidev.absmax[ABS_Z] = 0xffff;
    uidev.absmin[ABS_Z] = 0;

    uidev.absmax[ABS_RZ] = 0xffff;
    uidev.absmin[ABS_RZ] = 0;

    uidev.absmax[ABS_BRAKE] = 0x3ff;
    uidev.absmin[ABS_BRAKE] = 0;

    uidev.absmax[ABS_GAS] = 0x3ff;
    uidev.absmin[ABS_GAS] = 0;

    uidev.absmax[ABS_HAT0X] = 1;
    uidev.absmin[ABS_HAT0X] = -1;

    uidev.absmax[ABS_HAT0Y] = 1;
    uidev.absmin[ABS_HAT0Y] = -1;

    if (write(uinput_fd, &uidev, sizeof(uidev)) < 0) //writing settings
    {
        printf("error: write");
        return 1;
    }

    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) //writing ui dev create
    {
        printf("error: ui_dev_create");
        return 1;
    }
    return 0;
}

void mkEventLoop()
{
    printf("mkevent");
    while (true)
    {
        int keyboard_fd = open(keyboard_dev_path, O_RDONLY | O_NONBLOCK);
        int mouse_fd = open(mouse_dev_path, O_RDONLY | O_NONBLOCK);
        struct input_event mouse_event;
        struct input_event keyboard_event;

        while (!map_flag) //等待进入信号
        {
            if (read(mouse_fd, &mouse_event, sizeof(mouse_event)) != -1)
            {
                single_queue[s_len++] = mouse_event;
                if (mouse_event.type == 0 && mouse_event.code == 0 && mouse_event.value == 0)
                    handelEventQueue();
            }
            if (read(keyboard_fd, &keyboard_event, sizeof(keyboard_event)) != -1)
            {
                single_queue[s_len++] = keyboard_event;
                if (keyboard_event.type == 0 && keyboard_event.code == 0 && keyboard_event.value == 0)
                    handelEventQueue();
            }
        }
        if (map_flag)
        { //申请独占
            if (keyboard_fd == -1 || mouse_fd == -1)
            {
                printf("Failed to open keyboard or mouse.\n");
                exit(1);
            }
            char keyboard_name[256] = "Unknown";
            ioctl(keyboard_fd, EVIOCGNAME(sizeof(keyboard_name)), keyboard_name);
            printf("Reading From : %s \n", keyboard_name);
            printf("Getting exclusive access: %s\n", (ioctl(keyboard_fd, EVIOCGRAB, 1) == 0) ? "SUCCESS" : "FAILURE");

            char mouse_name[256] = "Unknown";
            ioctl(mouse_fd, EVIOCGNAME(sizeof(mouse_name)), mouse_name);
            printf("Reading From : %s \n", mouse_name);
            printf("Getting exclusive access: %s\n", (ioctl(mouse_fd, EVIOCGRAB, 1) == 0) ? "SUCCESS" : "FAILURE");

            pthread_t mouse_offset_read_thread;
            pthread_create(&mouse_offset_read_thread, NULL, (void *)&readMouseLoop, NULL);
        }

        while (map_flag) //读取 改变状态
        {
            if (read(mouse_fd, &mouse_event, sizeof(mouse_event)) != -1)
            {
                single_queue[s_len++] = mouse_event;
                if (mouse_event.type == 0 && mouse_event.code == 0 && mouse_event.value == 0)
                    handelEventQueue();
            }
            if (read(keyboard_fd, &keyboard_event, sizeof(keyboard_event)) != -1)
            {
                single_queue[s_len++] = keyboard_event;
                if (keyboard_event.type == 0 && keyboard_event.code == 0 && keyboard_event.value == 0)
                    handelEventQueue();
            }
        }
        if (!map_flag)
        {
            ioctl(keyboard_fd, EVIOCGRAB, 1);
            ioctl(mouse_fd, EVIOCGRAB, 1);
            close(keyboard_fd);
            close(mouse_fd);
        }
        //清除状态
    }
}
void mapThread()
{
    while (true)
    {
        while (map_flag)
        {
            //read data
            //change uinput stause
            //sleep
        }
        //sleep to wait for map_flag to be true
    }
}

int main(int argc, char *argv[])
{

    int mouse_dev_num = atoi(argv[1]);
    int keyboard_dev_num = atoi(argv[2]);
    mouse_dev = mouse_dev_num;
    keyboard_dev = keyboard_dev_num;
    sprintf(mouse_dev_path, "/dev/input/event%d", mouse_dev_num);
    sprintf(keyboard_dev_path, "/dev/input/event%d", keyboard_dev_num);

    printf("Mouse_dev_path:%s\n", mouse_dev_path);
    printf("Keyboard_dev_path:%s\n", keyboard_dev_path);

    if (createController() == 0)
    {
        // reveive_from_UDP(8848);

        pthread_t mapper_thread_thread;
        pthread_create(&mapper_thread_thread, NULL, (void *)&mapThread, NULL);
        //create mapper thread
        mkEventLoop();
        //call read event loop

        pthread_join(mapper_thread_thread, NULL);

        if (ioctl(uinput_fd, UI_DEV_DESTROY) < 0)
        {
            printf("error: ioctl");
            return 1;
        }
        close(uinput_fd);
        return 0;
    }
    else
    {
        printf("create uinput failed");
        return -1;
    }
}
