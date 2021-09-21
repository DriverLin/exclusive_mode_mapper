#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define DEBUG 0

#define DOWN 0x1
#define UP 0x0
#define MOVE_FLAG 0x0
#define RELEASE_FLAG 0x2
#define REQURIE_FLAG 0x1
#define WHEEL_REQUIRE 0X3
#define MOUSE_REQUIRE 0X4

#define rand_offset() (rand() % 80 - 40) //随机偏移 不需要改成0
#define mini_rand_offset() (rand() % 20 - 10)

char touch_dev_path[80];
char keyboard_dev_path[80];
char mouse_dev_path[80];

int keyboard_dev = 16;
int mouse_dev = 15;

int touch_fd; //触屏的设备文件指针

int Exclusive_mode_flag = 0;    //独占模式标识
int no_Exclusive_mode_flag = 1; //刚开始 进入非独占模式

struct input_event Mouse_queue[16]; //鼠标信号队列
int m_len = 0;                      //队列长度

struct input_event Keyboard_queue[16]; //键盘信号队列
int k_len = 0;                         //键盘队列长度

int touch_id[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int allocatedID_num = 0;

struct input_event SYNC_EVENT = {.type = EV_SYN, .code = SYN_REPORT, .value = 0x0};              //同步 最常用的 直接用
struct input_event SWITCH_ID_EVENT = {.type = EV_ABS, .code = ABS_MT_SLOT, .value = 0xffffffff}; //切换触摸点 修改value再用
struct input_event POS_X_EVENT = {.type = EV_ABS, .code = ABS_MT_POSITION_X};                    //X坐标
struct input_event POS_Y_EVENT = {.type = EV_ABS, .code = ABS_MT_POSITION_Y};                    //Y坐标
struct input_event DEFINE_UID_EVENT = {.type = EV_ABS, .code = ABS_MT_TRACKING_ID};              //声明识别ID 用于消除
struct input_event BTN_DOWN_EVENT = {.type = EV_KEY, .code = BTN_TOUCH, .value = DOWN};          //按下 没有触摸点的时候使用
struct input_event BTN_UP_EVENT = {.type = EV_KEY, .code = BTN_TOUCH, .value = UP};              //释放 触摸点全部释放的时候使用
//type = 0,1,2 id = -1,.... ,x,y      ID为-1 则是按下，获取返回的ID，下次带上才可进行滑动或者释放操作
//x,y为绝对坐标 越界重置也由外部完成
//按下 移动 释放
//滑动 = 按下+移动+释放
//不提供点击功能
//点击 = 按下 + 释放
//超出10点个不响应
//返回触摸点的ID 下次带上
//鼠标的映射 鼠标一开始就占一个 切换后才释放 是申请还是移动 在外边判断
//由于多线程 保证安全加上PV
sem_t touch_dev_controler_sem_control;
bool mouse_touch_allocater = true; //鼠标每次越界重新分配ID 0/1 尝试解决视角问题
int touch_dev_controler(int type, int unclear_id, int x, int y)
{

    sem_wait(&touch_dev_controler_sem_control);
    int id = unclear_id;
    if ((type == MOVE_FLAG) && (id != -1)) //移动:  切换ID,X,Y,同步 编码格式 "0 id x y"
    {
        if (SWITCH_ID_EVENT.value != id)
        {
            SWITCH_ID_EVENT.value = id;
            write(touch_fd, &SWITCH_ID_EVENT, sizeof(SWITCH_ID_EVENT));
        }
        POS_X_EVENT.value = x;
        POS_Y_EVENT.value = y;
        write(touch_fd, &POS_X_EVENT, sizeof(POS_X_EVENT));
        write(touch_fd, &POS_Y_EVENT, sizeof(POS_Y_EVENT));
        write(touch_fd, &SYNC_EVENT, sizeof(SYNC_EVENT));
    }
    else if ((type == RELEASE_FLAG) && (id != -1)) //释放: 切换ID,uid=-1,同步 编码格式 "2 id * *"
    {
        touch_id[id] = 0;  // 释放
        allocatedID_num--; //占用数目-1
        if (SWITCH_ID_EVENT.value != id)
        {
            SWITCH_ID_EVENT.value = id;
            write(touch_fd, &SWITCH_ID_EVENT, sizeof(SWITCH_ID_EVENT));
        }
        DEFINE_UID_EVENT.value = 0xffffffff;
        write(touch_fd, &DEFINE_UID_EVENT, sizeof(DEFINE_UID_EVENT));
        if (allocatedID_num == 0) //为0 全部释放 btn up
        {
            write(touch_fd, &BTN_UP_EVENT, sizeof(BTN_UP_EVENT));
        }
        write(touch_fd, &SYNC_EVENT, sizeof(SYNC_EVENT));
    }
    else if (type == MOUSE_REQUIRE || type == WHEEL_REQUIRE || type == REQURIE_FLAG)
    { //按下： 切换ID，uid=自定义，x，y，同步 编码格式 "1 * x y"
        if (type == MOUSE_REQUIRE)
        {
            id = mouse_touch_allocater ? 0 : 1;
            mouse_touch_allocater = !mouse_touch_allocater;
        }
        else if (type == WHEEL_REQUIRE)
            id = 2;
        else
        {
            for (int i = 3; i < 10; i++) // 0 1 保留给鼠标移动和方向控制
            {
                if (touch_id[i] == 0) //找寻一个空的
                {
                    id = i; //分配id
                    break;
                }
            }
        }
        if (id != -1)
        {
            touch_id[id] = 1; //记录此置位已占用
            allocatedID_num++;
            SWITCH_ID_EVENT.value = id;
            DEFINE_UID_EVENT.value = id;
            POS_X_EVENT.value = x;
            POS_Y_EVENT.value = y;
            write(touch_fd, &SWITCH_ID_EVENT, sizeof(SWITCH_ID_EVENT));
            write(touch_fd, &DEFINE_UID_EVENT, sizeof(DEFINE_UID_EVENT));
            if (allocatedID_num == 1)
            { //为1 则是头一次按下 btn down
                write(touch_fd, &BTN_DOWN_EVENT, sizeof(BTN_DOWN_EVENT));
            }
            write(touch_fd, &POS_X_EVENT, sizeof(POS_X_EVENT));
            write(touch_fd, &POS_Y_EVENT, sizeof(POS_Y_EVENT));
            write(touch_fd, &SYNC_EVENT, sizeof(SYNC_EVENT));
        }
    }
    if (DEBUG == 1)
    {
        // printf("[%d,%d,%d,%d,%d]\n", type, unclear_id, x, y, id);
        printf("[type=%d\t,ucid=%d\t,x=%d\t,y=%d\t,allocated id=%d]\n", type, unclear_id, x, y, id);
    }

    sem_post(&touch_dev_controler_sem_control);
    return id;
}

int mouse_touch_id = -1; //鼠标映射的ID 唯一 第一次产生移动事件时按下 之后只有移动  切换映射的时候才释放
int mouse_Start_x;       ///开始结束坐标
int mouse_Start_y;       //中途可能有切换 还是会回到这里的
int screen_x;
int screen_y;
int realtive_x, realtive_y; //保存当前移动坐标
int mouse_speedRatio = 1;
int km_map_id[256 + 8];      //键盘鼠标code 对应分配的ID 按下获取并存入 释放的时候就从这里获取ID释放
                             //鼠标编码0x110开始 0~7个
                             //将其放在了一起 鼠标加偏移量256
int map_postion[256 + 8][2]; //映射的XY坐标

int wheel_satuse[4];                                                                                //wasd按键状态                                                                                                                    //默认为0 初始化时和结束时也手动清0
int wheel_postion[9][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}; //8个状态的坐标
int wheel_touch_id = -1;
int cur_x = 0, cur_y = 0;       //当前位置
int tar_x = 0, tar_y = 0;       //目标位置
int move_speed = 60;            //方向移动速度
int frequency = 5000;           //方向移动频率 关系到相应方向键速度
int release_flag = 0;           //确保释放操作只执行一次
bool move_event_flag = false;   //鼠标事件标识
int none_mouse_event_count = 0; //无鼠标事件周期数

void manager_thread()
{
    while (Exclusive_mode_flag) //与独占模式共存亡
    {
        if (release_flag > 0 && tar_x == wheel_postion[4][0] && tar_y == wheel_postion[4][1]) //目标是中点 则直接释放
        {
            cur_x = tar_x;
            cur_y = tar_y;
            touch_dev_controler(RELEASE_FLAG, wheel_touch_id, 0, 0); //释放
            wheel_touch_id = -1;
            release_flag--; //确保在不按下按键时 执行
        }
        else
        {
            int div_x = tar_x - cur_x;
            int div_y = tar_y - cur_y;
            if (div_x)
            {
                if (abs(div_x) > move_speed)
                    cur_x += div_x > 0 ? 1 * move_speed : -1 * move_speed;
                else
                    cur_x = tar_x;
            }
            if (div_y)
            {
                if (abs(div_y) > move_speed)
                    cur_y += div_y > 0 ? 1 * move_speed : -1 * move_speed;
                else
                    cur_y = tar_y;
            }
            if (div_x || div_y)
                touch_dev_controler(MOVE_FLAG, wheel_touch_id, cur_x + mini_rand_offset(), cur_y + mini_rand_offset()); //正常移动
        }
        //追加部分 用于鼠标回位
        if (move_event_flag)
        {
            none_mouse_event_count = 0;
            move_event_flag = false;
        }
        else
        {
            if (none_mouse_event_count == 50)
            {
                touch_dev_controler(RELEASE_FLAG, mouse_touch_id, 0, 0);
                mouse_touch_id = -1;
            }
            else
            {
                none_mouse_event_count++;
            }
        }

        usleep(frequency);
    }
}
void change_wheel_satuse(int keyCode, int updown)
{
    int x_Asix = 1 - wheel_satuse[1] + wheel_satuse[3];
    int y_Asix = 1 - wheel_satuse[2] + wheel_satuse[0];
    int last_map_value = x_Asix * 3 + y_Asix;
    int index = -1;
    switch (keyCode)
    {
    case KEY_W:
        wheel_satuse[0] = updown;
        break;
    case KEY_A:
        wheel_satuse[1] = updown;
        break;
    case KEY_S:
        wheel_satuse[2] = updown;
        break;
    case KEY_D:
        wheel_satuse[3] = updown;
        break;
    default:
        break;
    }
    x_Asix = 1 - wheel_satuse[1] + wheel_satuse[3];
    y_Asix = 1 - wheel_satuse[2] + wheel_satuse[0];
    int map_value = x_Asix * 3 + y_Asix;
    if (last_map_value == 4 && map_value != 4) //按下 移动
    {
        tar_x = wheel_postion[4][0];
        tar_y = wheel_postion[4][1];
        cur_x = tar_x;
        cur_y = tar_y;                                                                                         //设置起始位置和目标位置为中点
        wheel_touch_id = touch_dev_controler(WHEEL_REQUIRE, -1, cur_x + rand_offset(), cur_y + rand_offset()); //按下中点
        tar_x = wheel_postion[map_value][0] + rand_offset();
        tar_y = wheel_postion[map_value][1] + rand_offset(); //设置移动目标
    }
    else
    {
        if (map_value != 4) //正常移动
        {
            tar_x = wheel_postion[map_value][0] + rand_offset();
            tar_y = wheel_postion[map_value][1] + rand_offset();
        }
        else //移动目标为中点 释放
        {
            release_flag++; //确保只释放一次
            tar_x = wheel_postion[4][0];
            tar_y = wheel_postion[4][1]; //管理器检测目标为中点 直接释放
        }
    }
}

struct input_event single_queue[16]; //整合设备信号队列
int s_len = 0;                       //整合队列长度
void handelEventQueue()              //处理所有事件
{
    int x = 0;
    int y = 0;
    for (int i = 0; i < s_len; i++) //main loop
    {
        if (single_queue[i].code == KEY_GRAVE && single_queue[i].value == UP) //独占和非独占都关注 ` 用于切换状态  `键不响应键盘映射
        {
            int tmp = Exclusive_mode_flag;
            Exclusive_mode_flag = no_Exclusive_mode_flag;
            no_Exclusive_mode_flag = tmp;
            break;
        }
        if (Exclusive_mode_flag)
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
                int keyCode = single_queue[i].code > 256 ? 256 + single_queue[i].code - BTN_MOUSE : single_queue[i].code;
                int updown = single_queue[i].value;
                if (keyCode == KEY_W || keyCode == KEY_A || keyCode == KEY_S || keyCode == KEY_D) //方向键 额外处理
                    change_wheel_satuse(keyCode, updown);
                else if (map_postion[keyCode][0] && map_postion[keyCode][1])
                { //映射坐标不为0 设定映射
                    if (updown == DOWN)
                        km_map_id[keyCode] = touch_dev_controler(REQURIE_FLAG, -1, map_postion[keyCode][0] + rand_offset(), map_postion[keyCode][1] + rand_offset()); //按下
                    else
                        touch_dev_controler(RELEASE_FLAG, km_map_id[keyCode], 0, 0); //释放
                }
            }
        }
    }
    if (Exclusive_mode_flag == 1 && (x != 0 || y != 0))
    { //有鼠标事件
        move_event_flag = true;
        realtive_x -= y * mouse_speedRatio;
        realtive_y += x * mouse_speedRatio;
        if (mouse_touch_id == -1 || realtive_x < 32 || realtive_x > screen_x - 32 || realtive_y < 32 || realtive_y > screen_y - 32)
        {
            int rand_X = rand_offset();
            int rand_Y = rand_offset();
            touch_dev_controler(RELEASE_FLAG, mouse_touch_id, 0, 0);                                                 //松开
            mouse_touch_id = touch_dev_controler(MOUSE_REQUIRE, -1, mouse_Start_x + rand_X, mouse_Start_y + rand_Y); //再按下
            realtive_x = mouse_Start_x + rand_X - y * mouse_speedRatio;
            realtive_y = mouse_Start_y + rand_Y + x * mouse_speedRatio; //相对X,Y
        }
        touch_dev_controler(MOVE_FLAG, mouse_touch_id, realtive_x, realtive_y); //移动
    }
    s_len = 0;
}

int virtualPointer_x = 100; //虚拟鼠标位置
int virtualPointer_y = 100;
int virtualPointer_touchID = -1; //按下的ID

int socket_descriptor = -1; //套接口描述字
struct sockaddr_in address;   //处理网络通信的地址
void sendvirtualPointerPostion(int x, int y)
{
    if (socket_descriptor == -1)
    {
        bzero(&address, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr("127.0.0.1"); //这里不一样
        address.sin_port = htons(6533);
        socket_descriptor = socket(AF_INET, SOCK_DGRAM, 0); //IPV4  SOCK_DGRAM 数据报套接字（UDP协议）
    }

    char msg[64];
    sprintf(msg, "%d,%d",  y,screen_x - x);
    sendto(socket_descriptor, msg, strlen(msg), 0, (struct sockaddr *)&address, sizeof(address));
}
void handelMouseQueue() //独立处理鼠标事件
{
    int x = 0;
    int y = 0;
    for (int i = 0; i < s_len; i++) //main loop
    {
        if (Exclusive_mode_flag == 0) //为0才处理
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
                if (updown == DOWN)
                    virtualPointer_touchID = touch_dev_controler(MOUSE_REQUIRE, -1, virtualPointer_x, virtualPointer_y); //再按下
                else
                {
                    touch_dev_controler(RELEASE_FLAG, virtualPointer_touchID, 0, 0); //释放
                    virtualPointer_touchID = -1;
                }
            }
        }
    }
    if (Exclusive_mode_flag == 0 && (x != 0 || y != 0))
    {
        virtualPointer_x -= y;

        if (virtualPointer_x < 0)
        {
            virtualPointer_x = 0;
        }
        else if (virtualPointer_x > screen_x)
        {
            virtualPointer_x = screen_x;
        }

        virtualPointer_y += x;

        if (virtualPointer_y < 0)
        {
            virtualPointer_y = 0;
        }
        else if (virtualPointer_y > screen_y)
        {
            virtualPointer_y = screen_y;
        }

        sendvirtualPointerPostion(virtualPointer_x, virtualPointer_y);
        if (virtualPointer_touchID != -1)
        {
            touch_dev_controler(MOVE_FLAG, virtualPointer_touchID, virtualPointer_x, virtualPointer_y);
        }
    }
    s_len = 0;
}

int Exclusive_mode()
{
    touch_fd = open(touch_dev_path, O_RDWR); //触摸设备号
    for (int i = 0; i < 4; i++)
        wheel_satuse[i] = 0; //清除方向盘状态
    if (touch_fd < 0)
    {
        fprintf(stderr, "could not open touchScreen\n");
        int tmp = Exclusive_mode_flag;
        Exclusive_mode_flag = no_Exclusive_mode_flag;
        no_Exclusive_mode_flag = tmp; //切换回非独占
        return 1;
    }

    int rcode = 0;
    char keyboard_name[256] = "Unknown";
    int keyboard_fd = open(keyboard_dev_path, O_RDONLY | O_NONBLOCK);
    if (keyboard_fd == -1)
    {
        printf("Failed to open keyboard.\n");
        exit(1);
    }
    rcode = ioctl(keyboard_fd, EVIOCGNAME(sizeof(keyboard_name)), keyboard_name);
    printf("Reading From : %s \n", keyboard_name);
    printf("Getting exclusive access: ");
    rcode = ioctl(keyboard_fd, EVIOCGRAB, 1);
    printf("%s\n", (rcode == 0) ? "SUCCESS" : "FAILURE");
    struct input_event keyboard_event;

    char mouse_name[256] = "Unknown";
    int mouse_fd = open(mouse_dev_path, O_RDONLY | O_NONBLOCK);
    if (mouse_fd == -1)
    {
        printf("Failed to open mouse.\n");
        exit(1);
    }
    rcode = ioctl(mouse_fd, EVIOCGNAME(sizeof(mouse_name)), mouse_name);
    printf("Reading From : %s \n", mouse_name);
    printf("Getting exclusive access: ");
    rcode = ioctl(mouse_fd, EVIOCGRAB, 1);
    printf("%s\n", (rcode == 0) ? "SUCCESS" : "FAILURE");
    struct input_event mouse_event;
    pthread_t manager_thread_thread;
    pthread_create(&manager_thread_thread, NULL, (void *)&manager_thread, NULL);

    while (Exclusive_mode_flag == 1)
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

    printf("Exiting.\n");
    pthread_join(manager_thread_thread, NULL);
    rcode = ioctl(keyboard_fd, EVIOCGRAB, 1);
    close(keyboard_fd);
    rcode = ioctl(mouse_fd, EVIOCGRAB, 1);
    close(mouse_fd);            //解除独占状态
    for (int i = 0; i < 4; i++) //初始化
        wheel_satuse[i] = 0;    //清除方向盘状态
    cur_x = wheel_postion[4][0];
    cur_y = wheel_postion[4][1];
    tar_x = cur_x;
    tar_y = cur_y; //管理器位置重置
    for (int i = 0; i < 10; i++)
        if (touch_id[i] != 0)
            touch_dev_controler(RELEASE_FLAG, i, 0, 0); //释放所有按键
    realtive_x = mouse_Start_x;
    realtive_y = mouse_Start_y; //相对X,Y
    wheel_touch_id = -1;
    mouse_touch_id = -1;
    SWITCH_ID_EVENT.value = 0xffffffff;
    allocatedID_num = 0;
    close(touch_fd);
    return 0;
}

int Exclusive_mode_single_Dev_Version()
{
    touch_fd = open(touch_dev_path, O_RDWR); //触摸设备号
    for (int i = 0; i < 4; i++)
        wheel_satuse[i] = 0; //清除方向盘状态
    if (touch_fd < 0)
    {
        fprintf(stderr, "could not open touchScreen\n");
        int tmp = Exclusive_mode_flag;
        Exclusive_mode_flag = no_Exclusive_mode_flag;
        no_Exclusive_mode_flag = tmp; //切换回非独占
        return 1;
    }
    int rcode = 0;
    char keyboard_name[256] = "Unknown";
    int singledev_fd = open(keyboard_dev_path, O_RDONLY | O_NONBLOCK);
    if (singledev_fd == -1)
    {
        printf("Failed to open DEV.\n");
        exit(1);
    }
    rcode = ioctl(singledev_fd, EVIOCGNAME(sizeof(keyboard_name)), keyboard_name);
    printf("Reading From : %s \n", keyboard_name);
    printf("Getting exclusive access: ");
    rcode = ioctl(singledev_fd, EVIOCGRAB, 1);
    printf("%s\n", (rcode == 0) ? "SUCCESS" : "FAILURE");
    struct input_event event;
    pthread_t manager_thread_thread;
    pthread_create(&manager_thread_thread, NULL, (void *)&manager_thread, NULL);
    while (Exclusive_mode_flag == 1)
    {
        if (read(singledev_fd, &event, sizeof(event)) != -1)
        {
            single_queue[s_len++] = event;
            if (event.type == 0 && event.code == 0 && event.value == 0)
            {
                handelEventQueue();
            }
        }
    }
    printf("Exiting.\n");
    pthread_join(manager_thread_thread, NULL);
    rcode = ioctl(singledev_fd, EVIOCGRAB, 1);
    close(singledev_fd);
    for (int i = 0; i < 4; i++) //初始化
        wheel_satuse[i] = 0;    //清除方向盘状态
    cur_x = wheel_postion[4][0];
    cur_y = wheel_postion[4][1];
    tar_x = cur_x;
    tar_y = cur_y; //管理器位置重置
    for (int i = 0; i < 10; i++)
        if (touch_id[i] != 0)
            touch_dev_controler(RELEASE_FLAG, i, 0, 0);
    //释放所有按键
    realtive_x = mouse_Start_x;
    realtive_y = mouse_Start_y; //相对X,Y
    wheel_touch_id = -1;
    mouse_touch_id = -1;
    SWITCH_ID_EVENT.value = 0xffffffff;
    allocatedID_num = 0;
    close(touch_fd);
    return 0;
}

int no_Exclusive_mode()
{
    touch_fd = open(touch_dev_path, O_RDWR); //触摸设备号
    for (int i = 0; i < 4; i++)
        wheel_satuse[i] = 0; //清除方向盘状态
    if (touch_fd < 0)
    {
        fprintf(stderr, "could not open touchScreen\n");
        exit(-1);
    }

    int rcode = 0;
    char keyboard_name[256] = "Unknown";
    int keyboard_fd = open(keyboard_dev_path, O_RDONLY | O_NONBLOCK);
    if (keyboard_fd == -1)
    {
        printf("Failed to open keyboard.\n");
        exit(1);
    }
    rcode = ioctl(keyboard_fd, EVIOCGNAME(sizeof(keyboard_name)), keyboard_name);
    printf("Reading From : %s \n", keyboard_name);
    struct input_event keyboard_event;

    char mouse_name[256] = "Unknown";
    int mouse_fd = open(mouse_dev_path, O_RDONLY | O_NONBLOCK);
    if (mouse_fd == -1)
    {
        printf("Failed to open mouse.\n");
        exit(1);
    }
    rcode = ioctl(mouse_fd, EVIOCGNAME(sizeof(mouse_name)), mouse_name);
    printf("Reading From : %s \n", mouse_name);
    printf("Getting exclusive access: ");
    rcode = ioctl(mouse_fd, EVIOCGRAB, 1);
    printf("%s\n", (rcode == 0) ? "SUCCESS" : "FAILURE");
    struct input_event mouse_event;

    while (no_Exclusive_mode_flag == 1)
    {
        if (read(keyboard_fd, &keyboard_event, sizeof(keyboard_event)) != -1)
        {
            single_queue[s_len++] = keyboard_event;
            if (keyboard_event.type == 0 && keyboard_event.code == 0 && keyboard_event.value == 0)
                handelEventQueue();
        }
        if (read(mouse_fd, &mouse_event, sizeof(mouse_event)) != -1)
        {
            single_queue[s_len++] = mouse_event;
            if (mouse_event.type == 0 && mouse_event.code == 0 && mouse_event.value == 0)
                handelMouseQueue();
        }
    }
    sendvirtualPointerPostion(-500, -500);
    printf("Exiting.\n");
    rcode = ioctl(mouse_fd, EVIOCGRAB, 1);
    close(mouse_fd); //解除独占状态
    close(keyboard_fd);
    return 0;
}

int main(int argc, char *argv[]) //触屏设备号 键盘设备号 鼠标设备号 mapper映射文件路径
                                 //首先是非独占模式 由`键启动进入独占模式 独占模式也可以退出到非独占 非独占只关注`键
{
    srand((unsigned)time(NULL)); //点击坐标+随机数
    int touch_dev_num = atoi(argv[1]);
    int mouse_dev_num = atoi(argv[2]);
    int keyboard_dev_num = atoi(argv[3]);
    mouse_dev = mouse_dev_num;
    keyboard_dev = keyboard_dev_num;
    sprintf(touch_dev_path, "/dev/input/event%d", touch_dev_num);
    sprintf(mouse_dev_path, "/dev/input/event%d", mouse_dev_num);
    sprintf(keyboard_dev_path, "/dev/input/event%d", keyboard_dev_num);
    printf("Touch_dev_path:%s\n", touch_dev_path);
    printf("Mouse_dev_path:%s\n", mouse_dev_path);
    printf("Keyboard_dev_path:%s\n", keyboard_dev_path);
    if (sem_init(&touch_dev_controler_sem_control, 0, 1) != 0)
    {
        perror("Fail to touch_dev_controler_sem_control init");
        exit(-1);
    }
    char buf[1024 * 8];      //配置文件大小最大8KB
    chdir(dirname(argv[0])); //设置当前目录为应用程序所在的目录
    printf("Reading config from %s\n", argv[4]);
    FILE *fp = fopen(argv[4], "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Can't read map file from %s, %s\n", argv[4], strerror(errno));
        exit(-2);
    }
    fread(buf, 1024 * 8, 1, fp);
    fclose(fp);
    int linecount = 0;
    char lines[68][32];
    char *token = strtok(buf, "\n");
    while (token != NULL)
    {
        strcpy(lines[linecount++], token);
        token = strtok(NULL, "\n");
    }
    int config[68][3];
    for (int i = 0; i < linecount; i++)
    {
        char *rowData = strtok(lines[i], " ");
        config[i][0] = atoi(rowData);
        config[i][1] = atoi(strtok(NULL, " "));
        config[i][2] = atoi(strtok(NULL, " "));
    }
    mouse_Start_x = config[0][0];
    mouse_Start_y = config[0][1];
    screen_x = mouse_Start_x * 2;
    screen_y = (mouse_Start_y - 200) * 2;
    mouse_speedRatio = config[0][2];
    for (int i = 0; i < 9; i++)
    {
        wheel_postion[i][0] = config[i + 1][1];
        wheel_postion[i][1] = config[i + 1][2];
    }
    for (int i = 9; i < linecount; i++)
    {
        map_postion[config[i][0]][0] = config[i][1];
        map_postion[config[i][0]][1] = config[i][2];
    }

    if (mouse_dev != keyboard_dev)
    {
        while (1)
        {
            no_Exclusive_mode();
            Exclusive_mode(); //记得先插鼠标 再插键盘
        }
    }
    else
    {
        printf("using single dev\n");
        while (1)
        {
            no_Exclusive_mode();
            Exclusive_mode_single_Dev_Version(); //记得先插鼠标 再插键盘
        }
    }
}