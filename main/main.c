/* LVGL Example project
 *
 * Basic project to test LVGL on ESP32 based projects.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"


#include "lvgl.h"

#include "lvgl_helpers.h"

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_eth.h"

#include "cJSON.h"


#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

#define USE_MENU    0
#if USE_MENU
    #define wifi_name CONFIG_wifi_Name
    #define wifi_password CONFIG_wifi_Password
    #define ChoiceQueryCity CONFIG_wifi_choice_City
#else
    #define wifi_name "1102"
    #define wifi_password "Hsht68121314"
    #define ChoiceQueryCity "foshan"
#endif


#define USE_BEBUG 1
#if USE_BEBUG
    #define DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else 
    (void)0
#endif 

#define MAX_HTTP_OUTPUT_BUFFER 2048

typedef struct _weather{
    char *city;                     //城市文字
    char *text_day;                 //白天天气现象文字
    char *text_night;               //晚间天气现象文字
    char *high_temp;                //当天最高温度
    char *low_temp;                 //当天最低温度
    char *wind_direction;           //风向文字
    char *wind_direction_degree;    //风向角度，范围0~360
    char *wind_speed;               //风速，单位km/h（当unit=c时）、mph（当unit=f时）
    char *wind_scale;               //风力等级
    char *humidity;                 //相对湿度，0~100，单位为百分比
    char *temperature;              //温度
}_weather;

_weather weather;


static lv_obj_t *lv_time_label;
static lv_obj_t *lv_date_label;
static lv_obj_t *lv_city_label;

static lv_obj_t *lv_logo_img;

uint32_t Refresh_Time_EVENT , Refresh_Weather_EVENT;

static const char *TAG = "API_get_weather";
const char *WIFI_TAG = "WIFI_TAG";

/*********************
 *      DEFINES
 *********************/
#define LV_TICK_PERIOD_MS 1
#define LV_IMG_Refresh_MS (70/portTICK_PERIOD_MS)


LV_FONT_DECLARE(SegFont_40);
LV_FONT_DECLARE(cityFont_48);



/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void create_demo_application(void);

static void wifi_config(void);
static void API_get_weather(void *pvParameters);
static void API_get_temp(void *pvParameters);
static void API_Timeinit(void *pvParameters);
void API_ShowImg(void *pvParameters);

void API_Refresh_time(void *pvParameter);

static void utc_sntp_init(void);

/**********************
 *   APPLICATION MAIN
 **********************/
void app_main() {

    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    wifi_config();
    utc_sntp_init();
    xTaskCreate(guiTask, "gui", 4096*2, NULL, 10, NULL);

}

/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

void API_Refresh_time(void *pvParameter)
{
    while (1)
    {
        /* code */
        lv_event_send(lv_time_label, Refresh_Time_EVENT, NULL);
        
        vTaskDelay(499 / portTICK_RATE_MS);

    }
}


void desktop(void)
{
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_black(),0);
}

static void guiTask(void *pvParameter) {

    (void) pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();

    lv_color_t* buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    lv_color_t* buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2 != NULL);
#else
    static lv_color_t *buf2 = NULL;
#endif

    static lv_disp_draw_buf_t disp_buf;

    uint32_t size_in_px = DISP_BUF_SIZE;

    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;

    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    /* Create the demo application */
    desktop();
    create_demo_application();
    

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
       }
    }

    /* A task should NEVER return */
    free(buf1);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    free(buf2);
#endif
    vTaskDelete(NULL);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    
    switch (event_id) 
    {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(WIFI_TAG, "SYSTEM_EVENT_STA_START");
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            ESP_LOGI(WIFI_TAG, "成功连接到WIFI");
            break;
        case IP_EVENT_STA_GOT_IP:
            ESP_LOGI(WIFI_TAG, "成功获取到IP");
            xTaskCreate(API_Refresh_time,"Refresh_time",4096,NULL, 9, NULL);
//           xTaskCreate(API_get_weather, "get_weather", 10240, NULL, 5, NULL);
            xTaskCreate(API_get_temp, "get_temp", 8096, NULL, 5, NULL);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(WIFI_TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
            esp_wifi_connect();
            break;
        default:
                break;
    }
}

static void wifi_config(void)
{
    
    nvs_flash_init();
    tcpip_adapter_init();
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    esp_event_handler_instance_t instance;
    

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL,&instance);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL,&instance);

    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    wifi_config_t sta_config = {
        .sta = {
            .ssid = wifi_name,
            .password = wifi_password,
            .bssid_set = false
        }
    };
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );


}

static void API_get_weather(void *pvParameters)
{
//02-1 定义需要的变量
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   //用于接收通过http协议返回的数据
    int content_length = 0;  //http协议头的长度
    //02-2 配置http结构体
   
   //定义http配置结构体，并且进行清零
    esp_http_client_config_t config ;
    memset(&config,0,sizeof(config));
    //向配置结构体内部写入url
    static const char *temp_url = "https://api.seniverse.com/v3/weather/daily.json?key=S18Lw5hJiUbZN5IsD&location="ChoiceQueryCity"&language=zh-Hans&unit=c&start=0&days=1";
    //初始化结构体
    esp_http_client_handle_t client = NULL;
    esp_err_t err = 0;

    cJSON * parent = NULL;
    cJSON * child =  NULL;

    config.url = temp_url;
    client = esp_http_client_init(&config);	//初始化http连接
    while(1)
    {
    //设置发送请求 
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    // 与目标主机创建连接，并且声明写入内容长度为0
    err = esp_http_client_open(client, 0);

    //如果连接失败
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } 
    //如果连接成功
    else {

        //读取目标主机的返回内容的协议头
        content_length = esp_http_client_fetch_headers(client);

        //如果协议头长度小于0，说明没有成功读取到
        if (content_length < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } 

        //如果成功读取到了协议头
        else {

            //读取目标主机通过http的响应内容
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {
#if 0
                //打印响应内容，包括响应状态，响应体长度及其内容
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),				//获取响应状态信息
                esp_http_client_get_content_length(client));			//获取响应信息长度
#endif
                parent = cJSON_Parse(output_buffer);
                child = cJSON_GetObjectItem(parent,"results");
                parent = cJSON_GetArrayItem(child,0);
                child = cJSON_GetObjectItem(parent,"daily");
                parent = cJSON_GetArrayItem(child,0);
                weather.text_day = cJSON_GetObjectItem(parent,"text_day")->valuestring;
                weather.text_night = cJSON_GetObjectItem(parent,"text_night")->valuestring;
                weather.high_temp = cJSON_GetObjectItem(parent,"high")->valuestring;
                weather.low_temp = cJSON_GetObjectItem(parent,"low")->valuestring;
                weather.wind_direction = cJSON_GetObjectItem(parent,"wind_direction")->valuestring;
                weather.wind_direction_degree = cJSON_GetObjectItem(parent,"wind_direction_degree")->valuestring;
                weather.wind_speed = cJSON_GetObjectItem(parent,"wind_speed")->valuestring;
                weather.wind_scale = cJSON_GetObjectItem(parent,"wind_scale")->valuestring;
                weather.humidity = cJSON_GetObjectItem(parent,"humidity")->valuestring;
                
                DEBUG("白天天气现象:%s\n晚间天气现象:%s\n当天最高温度:%s\n当天最低温度:%s\n风向:%s\n风向角度:%s\n风速:%s\n风力等级:%s\n相对湿度:%s\n",
                        weather.text_day,weather.text_night,weather.high_temp,weather.low_temp,weather.wind_direction,weather.wind_direction_degree,
                        weather.wind_speed,weather.wind_scale,weather.humidity);
            } 
            //如果不成功
            else {
                ESP_LOGE(TAG, "Failed to read response");
            }
        }
    }
    //关闭连接
    esp_http_client_close(client);
    //延时
    vTaskDelay(10000/portTICK_PERIOD_MS);
    }
}

static void API_get_temp(void *pvParameters)
{
//02-1 定义需要的变量
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   //用于接收通过http协议返回的数据
    int content_length = 0;  //http协议头的长度
    //02-2 配置http结构体
   
   //定义http配置结构体，并且进行清零
    esp_http_client_config_t config ;
    memset(&config,0,sizeof(config));
    //向配置结构体内部写入url
    static const char *weather_url = "https://api.seniverse.com/v3/weather/now.json?key=S18Lw5hJiUbZN5IsD&location="ChoiceQueryCity"&language=zh-Hans&unit=c";

    //初始化结构体
    esp_http_client_handle_t client = NULL;
    esp_err_t err = 0;

    cJSON * parent = NULL;
    cJSON * child =  NULL;

    config.url = weather_url;
    client = esp_http_client_init(&config);	//初始化http连接
    while(1)
    {

    //设置发送请求 
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    // 与目标主机创建连接，并且声明写入内容长度为0
    err = esp_http_client_open(client, 0);
    //如果连接失败
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } 
    //如果连接成功
    else {
        //读取目标主机的返回内容的协议头
        content_length = esp_http_client_fetch_headers(client);
        //如果协议头长度小于0，说明没有成功读取到
        if (content_length < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } 
        //如果成功读取到了协议头
        else {
            //读取目标主机通过http的响应内容
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) {

#if 0
                //打印响应内容，包括响应状态，响应体长度及其内容
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),				//获取响应状态信息
                esp_http_client_get_content_length(client));			//获取响应信息长度
#endif
                parent = cJSON_Parse(output_buffer);
                child = cJSON_GetObjectItem(parent,"results");
                parent = cJSON_GetArrayItem(child,0);
                child = cJSON_GetObjectItem(parent,"now");
                weather.city = cJSON_GetObjectItem(cJSON_GetObjectItem(parent,"location"),"name")->valuestring;
                weather.temperature = cJSON_GetObjectItem(child,"temperature")->valuestring;
                DEBUG("城市:%s\n环境温度:%s\n",weather.city,weather.temperature);
                lv_event_send(lv_city_label, Refresh_Weather_EVENT, NULL);

            } 
            //如果不成功
            else {
                ESP_LOGE(TAG, "Failed to read response");
            }
        }
    }
    //关闭连接
    esp_http_client_close(client);
//    esp_http_client_cleanup(client);
    //延时
    vTaskDelay(5000/portTICK_PERIOD_MS);
    }
}

static void API_show_Weather_event_Handler(lv_event_t * e)
{
    lv_label_set_text_fmt(lv_city_label,"#00f0f0 %s°#",weather.temperature);

}

void API_Show_Weather(void)
{
    /*城市显示函数*/
    Refresh_Weather_EVENT = lv_event_register_id();
    lv_city_label = lv_label_create(lv_scr_act());
    lv_obj_align(lv_city_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_label_set_recolor(lv_city_label, true);
    static lv_style_t city_style;
    lv_style_init(&city_style);
    lv_style_set_text_font(&city_style,&lv_font_montserrat_44);
    lv_style_set_text_opa(&city_style,LV_OPA_100);
    lv_obj_add_style(lv_city_label, &city_style,0);

    lv_label_set_text(lv_city_label,"#00f0f0 00°#");
    lv_obj_add_event_cb(lv_city_label, API_show_Weather_event_Handler,Refresh_Weather_EVENT,NULL);
}

void sntp_set_time_sync_callback(struct timeval *tv)
{
    struct tm timeinfo = {0};
    ESP_LOGI(TAG, "tv_sec: %lld", (uint64_t)tv->tv_sec);
    localtime_r((const time_t *)&(tv->tv_sec), &timeinfo);
    ESP_LOGI(TAG, "%d %d %d %d:%d:%d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    //sntp_stop();
    // utc_set_time((uint64_t)tv->tv_sec);
}

static void utc_sntp_init(void)
{ 
    ESP_LOGI(TAG, "------------Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");	//设置访问服务器	
    sntp_setservername(1, "ntp1.aliyun.com");	//设置访问服务器	
    sntp_setservername(2, "ntp2.aliyun.com");	//设置访问服务器	
    sntp_setservername(3, "ntp3.aliyun.com");	//设置访问服务器	
    sntp_setservername(4, "ntp4.aliyun.com");	//设置访问服务器	

    setenv("TZ", "CST-8", 1);  
    tzset();

    sntp_set_time_sync_notification_cb(&sntp_set_time_sync_callback);

    sntp_init();
}

static void API_show_time_event_Handler(lv_event_t * e)
{
    struct tm *t ;
    static char date[8];
    static bool time_poll = 0;
    time_t tt;
    time(&tt);
    t=localtime(&tt);
    if(time_poll == 0)
    {
        lv_label_set_text_fmt(lv_time_label,"#ff0000 %02d:%02d#",t->tm_hour,t->tm_min);
        lv_label_set_text_fmt(lv_date_label,"#fff000 %04d/%02d/%02d#",t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
        strftime(date,sizeof (date),"%A",t);
        printf("%s\n",date);
        time_poll = 1;
    }
    else
    {
        lv_label_set_text_fmt(lv_time_label,"#ff0000 %02d %02d#",t->tm_hour,t->tm_min);
        time_poll = 0;
    }

}

static void API_show_time(void)
{  
    /*时间显示创建*/
    Refresh_Time_EVENT = lv_event_register_id();
    lv_time_label = lv_label_create(lv_scr_act());
    lv_obj_set_pos(lv_time_label,5,10);
    lv_label_set_recolor(lv_time_label, true);
    static lv_style_t style ;
    static lv_style_t data_style ;
    lv_style_init(&style);
    lv_style_set_text_font(&style,&SegFont_40);
    lv_obj_add_style(lv_time_label, &style,0);
    lv_label_set_text(lv_time_label,"#ff0000 00:00#");

    lv_obj_add_event_cb(lv_time_label, API_show_time_event_Handler,Refresh_Time_EVENT,NULL);

    /*日期显示创建*/
    lv_date_label =lv_label_create(lv_scr_act());
//    lv_obj_set_pos(lv_date_lable,5,lv_obj_get_x());
    lv_obj_align_to(lv_date_label,lv_time_label,LV_ALIGN_OUT_BOTTOM_LEFT,12,5);
    lv_label_set_recolor(lv_date_label,true);
    lv_style_set_text_font(&data_style,&lv_font_montserrat_20);
    lv_obj_add_style(lv_date_label, &data_style,0);
    lv_label_set_text(lv_date_label,"#fff000 0000/00/00#");
}

void API_ShowImg(void *pvParameters)
{
    LV_IMG_DECLARE(SPACE_1);LV_IMG_DECLARE(SPACE_2);LV_IMG_DECLARE(SPACE_3);LV_IMG_DECLARE(SPACE_4);LV_IMG_DECLARE(SPACE_5);
    LV_IMG_DECLARE(SPACE_6);LV_IMG_DECLARE(SPACE_7);LV_IMG_DECLARE(SPACE_8);LV_IMG_DECLARE(SPACE_9);LV_IMG_DECLARE(SPACE_10);
    LV_IMG_DECLARE(SPACE_11);LV_IMG_DECLARE(SPACE_12);LV_IMG_DECLARE(SPACE_13);LV_IMG_DECLARE(SPACE_14);LV_IMG_DECLARE(SPACE_15);
    LV_IMG_DECLARE(SPACE_16);LV_IMG_DECLARE(SPACE_17);LV_IMG_DECLARE(SPACE_18);

    lv_logo_img = lv_img_create(lv_scr_act());
    lv_img_set_src(lv_logo_img, &SPACE_1);
    lv_obj_align(lv_logo_img, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    while(1)
    {
        lv_img_set_src(lv_logo_img, &SPACE_1);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_2);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_3);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_4);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_5);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_6);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_7);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_8);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_9);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_10);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_11);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_12);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_13);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_14);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_15);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_16);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_17);
        vTaskDelay(LV_IMG_Refresh_MS);
        lv_img_set_src(lv_logo_img, &SPACE_18);
        vTaskDelay(LV_IMG_Refresh_MS);

    }
}

static void create_demo_application(void)
{

    API_Show_Weather();
    API_show_time();
//    xTaskCreate(API_ShowImg,"ShowImg",4096,NULL, 9, NULL);
}

static void lv_tick_task(void *arg) {
    (void) arg;

    lv_tick_inc(LV_TICK_PERIOD_MS);
}
