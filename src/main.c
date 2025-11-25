#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/random/random.h>

// Stack size settings
#define THREAD_STACK_SIZE 256

//define the sensors dt spec
static const struct gpio_dt_spec mag_1 = GPIO_DT_SPEC_GET(DT_ALIAS(mag_sensor_1), gpios);
static const struct gpio_dt_spec mag_2 = GPIO_DT_SPEC_GET(DT_ALIAS(mag_sensor_2), gpios);

// Define stack areas for the threads
K_THREAD_STACK_DEFINE(cam_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(speed_sensor_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(display_stack, THREAD_STACK_SIZE);


// Declare thread data structs
static struct k_thread cam_thread;
static struct k_thread speed_sensor_thread;
static struct k_thread display_thread;

//#ifdef TEST_ACTIVE
K_THREAD_STACK_DEFINE(tests_stack, THREAD_STACK_SIZE);
static struct k_thread tests_thread;
//#endif

K_SEM_DEFINE(speed_sem, 0, 1);   // Semáforo para notificar thread

static struct gpio_callback mag_1_cb_data;
static struct gpio_callback mag_2_cb_data;

#define SPEED_MSGQ_SIZE 4
#define SPEED_MSGQ_ITEM_SIZE sizeof(int)

K_MSGQ_DEFINE(speed_msgq, SPEED_MSGQ_ITEM_SIZE, SPEED_MSGQ_SIZE, 4);

#define DISPLAY_MSGQ_SIZE 4
#define DISPLAY_MSGQ_ITEM_SIZE sizeof(int[2])

K_MSGQ_DEFINE(display_msgq, DISPLAY_MSGQ_ITEM_SIZE, DISPLAY_MSGQ_SIZE, 4);

int64_t speed_last_time = 0;
int speed_delta = 0;
int speed_state = 0;
int speed = 0;

void mag_1_callback_func(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_msgq_purge(&speed_msgq);
    if (speed_state == 0){
        speed_state += 1;
        speed_last_time = k_uptime_get();
        k_sem_give(&speed_sem);
    }
}

void mag_2_callback_func(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    k_msgq_purge(&speed_msgq);
    if (speed_state == 1){
        speed_delta = k_uptime_get() - speed_last_time;
        speed_last_time = k_uptime_get();
    }
    speed_state += 1;
}

void radar_reset(){
    speed = 0;
    speed_delta = 0;
    speed_last_time = 0;
    speed_state = 0;
}

void speed_sensor_thread_start(void *arg_1, void *arg_2, void *arg_3)
{
    while(1){
        if (speed_state <= 1)
            k_sem_take(&speed_sem, K_FOREVER);

        else if (k_uptime_get() - speed_last_time > 2000){
            speed_state -= 1;
            int speed = CONFIG_RADAR_SENSOR_DISTANCE_MM/speed_delta * 3.6;
            k_msgq_purge(&speed_msgq);
            
            k_msgq_put(&speed_msgq, &speed_state, K_MSEC(1)); 
            k_sleep(K_MSEC(1));
            k_msgq_put(&speed_msgq, &speed, K_MSEC(1));
            
            
            radar_reset();
        }
        k_msleep(200); 
    }
}



// Blink thread entry point
void display_thread_start(void *arg_1, void *arg_2, void *arg_3)
{
    printk("display thread OK...\r\n");
    int display_info[2];
    while (1) {
        //printk("hello from cam\r\n");
        k_msleep(50);
        if (k_msgq_get(&display_msgq, &display_info, K_FOREVER) == 0){
            printk("╔═══════════════════════════════╗\r\n");
            printk("║            DISPLAY            ║\r\n");
            printk("║                               ║\r\n");
            if (display_info[1] == 2) printk("║   VERMELHO; velocidade: %d\t║\r\n", display_info[0]);
            else if (display_info[1] == 1) printk("║  AMARELO; velocidade: %d\t║\r\n", display_info[0]);
            else if (display_info[1] == 0) printk("║  VERDE; velocidade: %d\t║\r\n", display_info[0]);
            else printk("tem parada errada ai, irmao");
            printk("║                               ║\r\n");
            printk("╚═══════════════════════════════╝\r\n\n\n");
        }
        
        
    }
}

typedef struct{
    char data[8];
}cam_bus;

ZBUS_CHAN_DEFINE(cam_chan,
    cam_bus,
    NULL,         // validator
    NULL,         // user_data
    ZBUS_OBS_DECLARE(), // nenhum observer direto
    ZBUS_MSG_INIT(.data = "\0") // valor inicial
);

void generate_placa_mercosul(char *placa) {
    const char letras[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char numeros[] = "0123456789";

    // Inicialize o gerador de aleatórios (faça uma vez, fora da função, se for chamar muitas vezes em sequência)

    placa[0] = letras[sys_rand32_get() % 26];
    placa[1] = letras[sys_rand32_get() % 26];
    placa[2] = letras[sys_rand32_get() % 26];
    placa[3] = numeros[sys_rand32_get() % 10];
    placa[4] = letras[sys_rand32_get() % 10];
    placa[5] = numeros[sys_rand32_get() % 10];
    placa[6] = numeros[sys_rand32_get() % 10];
    placa[7] = '\0'; // Fim da string
}
void cam_thread_start(void *arg_1, void *arg_2, void *arg_3)
{
    printk("cam thread OK...\r\n");

    cam_bus data;
    char placa[8] = "0000000";

    while (1) {
        // Espera mensagem da main
        if (zbus_chan_read(&cam_chan, &data, K_FOREVER) == 0) {
            //printk("Thread recebeu: %.*s\n", 7, data.data);

            if (memcmp(data.data, "xxxxxxx", 8) == 0) {
                //gera placa aleatória
                if (CONFIG_RADAR_CAMERA_FAILURE_RATE_PERCENT <= sys_rand32_get() % 100){
                    generate_placa_mercosul(placa);
                }
                strncpy(data.data, placa, 8);
            }
            zbus_chan_pub(&cam_chan, &data, K_NO_WAIT);
        }
        k_msleep(5); // previne busy-loop
    }
}

//#ifdef CONFIG_TEST_ACTIVE
void tests_thread_start(void *ret, void *main_thread_paused, void *arg_3){
    static struct device dummy;
    static struct gpio_callback dummy_cb;

    radar_reset();
    k_sleep(K_MSEC(200)); 
    k_msgq_purge(&speed_msgq);

    /*-----------------------------------------------------------------------------*/
    /*-----------------------------------------------------------------------------*/
    /*------------------------TESTS WITH MAIN THREAD ACTIVE------------------------*/
    /*-----------------------------------------------------------------------------*/
    /*-----------------------------------------------------------------------------*/

    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(6000));
    
    //3 eixos
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(60));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(5000));


    //6 eixos
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_1_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(30));
    mag_2_callback_func(&dummy, &dummy_cb, 1 << 6);
    k_sleep(K_MSEC(8000));
}
//#endif

int main(void)
{
    int ret;

    printk("setting up the mag sensors...\r\n");

    if (!gpio_is_ready_dt(&mag_1)) {
        printk("ERROR: mag_1 not ready\r\n");
        return 0;
    }
    if (!gpio_is_ready_dt(&mag_2)) {
        printk("ERROR: mag_2 not ready\r\n");
        return 0;
    }
    
    //configure mag_1 to give green for mag_1_treatment
    gpio_pin_configure_dt(&mag_1, GPIO_INPUT | GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&mag_1_cb_data, mag_1_callback_func, BIT(mag_1.pin));
    gpio_add_callback(mag_1.port, &mag_1_cb_data);
    gpio_pin_interrupt_configure_dt(&mag_1, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_pin_configure_dt(&mag_2, GPIO_INPUT | GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&mag_2_cb_data, mag_2_callback_func, BIT(mag_2.pin));
    gpio_add_callback(mag_2.port, &mag_2_cb_data);
    gpio_pin_interrupt_configure_dt(&mag_2, GPIO_INT_EDGE_TO_ACTIVE);

    printk("mag sensors OK\r\n");
    printk("setting up the threads...\r\n");

    //define the theads ids
    k_tid_t cam_tid;
    k_tid_t speed_sensor_tid;
    k_tid_t display_tid;
    k_tid_t tests_tid;

    // Start the cam thread
    cam_tid = k_thread_create   (&cam_thread,          // Thread struct
                                cam_stack,            // Stack
                                K_THREAD_STACK_SIZEOF(cam_stack),
                                cam_thread_start,     // Entry point
                                NULL,                   // arg_1
                                NULL,                   // arg_2
                                NULL,                   // arg_3
                                5,                      // Priority
                                0,                      // Options
                                K_NO_WAIT);             // Delay
    
    display_tid = k_thread_create   (&display_thread,          // Thread struct
                                display_stack,            // Stack
                                K_THREAD_STACK_SIZEOF(display_stack),
                                display_thread_start,     // Entry point
                                NULL,                   // arg_1
                                NULL,                   // arg_2
                                NULL,                   // arg_3
                                5,                      // Priority
                                0,                      // Options
                                K_NO_WAIT);             // Delay

    speed_sensor_tid = k_thread_create (&speed_sensor_thread,          // Thread struct
                                speed_sensor_stack,            // Stack
                                K_THREAD_STACK_SIZEOF(speed_sensor_stack),
                                speed_sensor_thread_start,     // Entry point
                                NULL,                   // arg_1
                                NULL,                   // arg_2
                                NULL,                   // arg_3
                                5,                      // Priority
                                0,                      // Options
                                K_NO_WAIT);             // Delay

    //#ifdef CONFIG_TEST_ACTIVE
    bool main_thread_paused = false;
    tests_tid = k_thread_create (&tests_thread,          // Thread struct
                                tests_stack,            // Stack
                                K_THREAD_STACK_SIZEOF(tests_stack),
                                tests_thread_start,     // Entry point
                                &ret,                   // arg_1
                                &main_thread_paused,    // arg_2
                                NULL,                   // arg_3
                                5,                      // Priority
                                0,                      // Options
                                K_NO_WAIT);             // Delay
    //#endif
    printk("threads OK\r\n\n\n");
    int axis = 0;
    
    while (1) {

        //#ifdef CONFIG_TEST_ACTIVE
        if (main_thread_paused){
            k_msleep(100);
            continue;
        }
        //#endif

        int ret = k_msgq_get(&speed_msgq, &axis, K_MSEC(100));
        if (ret == 0) {
            int speed;
            k_msgq_get(&speed_msgq, &speed, K_MSEC(10));

            cam_bus data = {.data = "xxxxxxx"};
            zbus_chan_pub(&cam_chan, &data, K_NO_WAIT);
            k_msleep(100);
            zbus_chan_read(&cam_chan, &data, K_FOREVER);
            printk("debug ---> (placa: %s, velocidade: %d, eixos: %d)\n\r", data.data, speed, axis);

            int display_info[2];

            display_info[0] = speed;
            if (axis <= 2){
                if(speed > CONFIG_RADAR_SPEED_LIMIT_LIGHT_KMH){
                    display_info[1] = 2;
                }
                else if(speed > (CONFIG_RADAR_SPEED_LIMIT_LIGHT_KMH * (CONFIG_RADAR_WARNING_THRESHOLD_PERCENT / 100))){
                    display_info[1] = 1;
                }
                else{
                    display_info[1] = 0;
                }

            }
            else{
                if(speed > CONFIG_RADAR_SPEED_LIMIT_HEAVY_KMH){
                    display_info[1] = 2;
                }
                else if(speed > (CONFIG_RADAR_SPEED_LIMIT_HEAVY_KMH * (CONFIG_RADAR_WARNING_THRESHOLD_PERCENT / 100))){
                    display_info[1] = 1;
                }
                else{
                    display_info[1] = 1;
                }
            }
            k_msgq_put(&display_msgq, &display_info, K_NO_WAIT);
        }

        k_msleep(100);
    }

    return 0;
}