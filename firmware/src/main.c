#include <asf.h>
#include "conf_board.h"
#include <string.h>
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"
#define  EOF 'X'
/************************************************************************/
/* LCD / LVGL                                                           */
/************************************************************************/
#define DEBUG_SERIAL

#ifdef DEBUG_SERIAL
#define USART_COM USART1
#define USART_COM_ID ID_USART1
#else
#define USART_COM USART0
#define USART_COM_ID ID_USART0
#endif

#define LV_HOR_RES_MAX          (320)
#define LV_VER_RES_MAX          (240)

#define AFEC_POT AFEC0
#define AFEC_POT_ID ID_AFEC0
#define AFEC_POT_CHANNEL 5


#define TASK_ADC_STACK_SIZE (1024 * 10 / sizeof(portSTACK_TYPE))
#define TASK_ADC_STACK_PRIORITY (tskIDLE_PRIORITY)
QueueHandle_t xQueueEnvia;
TimerHandle_t xTimer;


typedef struct {
	uint value;
} adcData;
//Label bot�es
static  lv_obj_t * labelBtnPlay;
static  lv_obj_t * labelBtnForward;
static  lv_obj_t * labelBtnBackward;
static  lv_obj_t * labelBtnShuffle;
static  lv_obj_t * labelBtnRepeat;
/*A static or global variable to store the buffers*/
static lv_disp_draw_buf_t disp_buf;

/*Static or global buffer(s). The second buffer is optional*/
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];
static lv_disp_drv_t disp_drv;          /*A variable to hold the drivers. Must be static or global.*/
static lv_indev_drv_t indev_drv;

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY            (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = CONF_UART_BAUDRATE,
		#if (defined CONF_UART_CHAR_LENGTH)
		.charlength = CONF_UART_CHAR_LENGTH,
		#endif
		.paritytype = CONF_UART_PARITY,
		#if (defined CONF_UART_STOP_BITS)
		.stopbits = CONF_UART_STOP_BITS,
		#endif
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	#if defined(__GNUC__)
	setbuf(stdout, NULL);
	#else
	/* Already the case in IAR's Normal DLIB default configuration: printf()
	* emits one character at a time.
	*/
	#endif
}

uint32_t usart_puts(uint8_t *pstring) {
	uint32_t i ;

	while(*(pstring + i))
	if(uart_is_tx_empty(USART_COM))
	usart_serial_putchar(USART_COM, *(pstring+i++));
}

void usart_put_string(Usart *usart, char str[]) {
	usart_serial_write_packet(usart, str, strlen(str));
}

int usart_get_string(Usart *usart, char buffer[], int bufferlen, uint timeout_ms) {
	uint timecounter = timeout_ms;
	uint32_t rx;
	uint32_t counter = 0;

	while( (timecounter > 0) && (counter < bufferlen - 1)) {
		if(usart_read(usart, &rx) == 0) {
			buffer[counter++] = rx;
		}
		else{
			timecounter--;
			vTaskDelay(1);
		}
	}
	buffer[counter] = 0x00;
	return counter;
}

void usart_send_command(Usart *usart, char buffer_rx[], int bufferlen,
char buffer_tx[], int timeout) {
	usart_put_string(usart, buffer_tx);
	usart_get_string(usart, buffer_rx, bufferlen, timeout);
}

void config_usart0(void) {
	sysclk_enable_peripheral_clock(ID_USART0);
	usart_serial_options_t config;
	config.baudrate = 9600;
	config.charlength = US_MR_CHRL_8_BIT;
	config.paritytype = US_MR_PAR_NO;
	config.stopbits = false;
	usart_serial_init(USART0, &config);
	usart_enable_tx(USART0);
	usart_enable_rx(USART0);

	// RX - PB0  TX - PB1
	pio_configure(PIOB, PIO_PERIPH_C, (1 << 0), PIO_DEFAULT);
	pio_configure(PIOB, PIO_PERIPH_C, (1 << 1), PIO_DEFAULT);
}

int hc05_init(void) {
	char buffer_rx[128];
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT+NAMEagoravai", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT+PIN0000", 100);
}


/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/
volatile int shuffle;
volatile int pausa;
volatile int repeat;
static void play_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	if(code == LV_EVENT_CLICKED) {
		char c = 'P';
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xQueueSendFromISR(xQueueEnvia, &c, &xHigherPriorityTaskWoken);
		if (!pausa){
			lv_label_set_text_fmt(labelBtnPlay,LV_SYMBOL_PAUSE);
			pausa = 1;
		}
		else{
			lv_label_set_text_fmt(labelBtnPlay,LV_SYMBOL_PLAY);
			pausa = 0;
		}
	}
}

static void forward_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		char c = 'F';
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xQueueSendFromISR(xQueueEnvia, &c, &xHigherPriorityTaskWoken);
	}
}

static void backward_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		char c = 'B';
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xQueueSendFromISR(xQueueEnvia, &c, &xHigherPriorityTaskWoken);
	}
}
static void shuffle_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		char c = 'S';
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xQueueSendFromISR(xQueueEnvia, &c, &xHigherPriorityTaskWoken);
		if(!shuffle){
			lv_obj_set_style_text_color(labelBtnShuffle, lv_palette_main(LV_PALETTE_LIGHT_GREEN), LV_STATE_DEFAULT);
			shuffle =1;
		}
		else{
			shuffle = 0;
			lv_obj_set_style_text_color(labelBtnShuffle, lv_color_white(), LV_STATE_DEFAULT);
		}
	}
}

static void repeat_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	
	if(code == LV_EVENT_CLICKED) {
		char c = 'R';
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xQueueSendFromISR(xQueueEnvia, &c, &xHigherPriorityTaskWoken);
		if(!repeat){
			lv_obj_set_style_text_color(labelBtnRepeat, lv_palette_main(LV_PALETTE_LIGHT_GREEN), LV_STATE_DEFAULT);
			repeat =1;
		}
		else{
			repeat = 0;
			lv_obj_set_style_text_color(labelBtnRepeat, lv_color_white(), LV_STATE_DEFAULT);
		}
	}
}



void lv_tela(void) {
	//Estilo Shuffle
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	lv_style_set_border_width(&style, 0);
	//Cria bot�o Shuffle 
	lv_obj_t * btnShuffle = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btnShuffle, shuffle_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btnShuffle, LV_ALIGN_LEFT_MID, 5, 0);
	lv_obj_add_style(btnShuffle, &style, 0);
	lv_obj_set_width(btnShuffle, 60);
	lv_obj_set_height(btnShuffle, 60);
	//Label Shuffle
	labelBtnShuffle = lv_label_create(btnShuffle);
	lv_label_set_text(labelBtnShuffle,LV_SYMBOL_SHUFFLE);
	lv_obj_center(labelBtnShuffle);
	
	//Cria bot�o Bacward
	lv_obj_t * btnBacward = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btnBacward, backward_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnBacward, btnShuffle,LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
	lv_obj_add_style(btnBacward, &style, 0);
	lv_obj_set_width(btnBacward, 60);
	lv_obj_set_height(btnBacward, 60);
	//Label Shuffle
	labelBtnBackward = lv_label_create(btnBacward);
	lv_label_set_text(labelBtnBackward,LV_SYMBOL_PREV);
	lv_obj_center(labelBtnBackward);
	
	//Cria bot�o Play
	lv_obj_t * btnPlay = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btnPlay, play_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnPlay, btnBacward,LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
	lv_obj_add_style(btnPlay, &style, 0);
	lv_obj_set_width(btnPlay, 60);
	lv_obj_set_height(btnPlay, 60);
	//Label Shuffle
	labelBtnPlay = lv_label_create(btnPlay);
	lv_label_set_text(labelBtnPlay,LV_SYMBOL_PLAY);
	lv_obj_center(labelBtnPlay);
	
	//Cria bot�o Forward
	lv_obj_t * btnForward = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btnForward,forward_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnForward,btnPlay,LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
	lv_obj_add_style(btnForward, &style, 0);
	lv_obj_set_width(btnForward, 40);
	lv_obj_set_height(btnForward, 60);
	//Label Shuffle
	labelBtnForward = lv_label_create(btnForward);
	lv_label_set_text(labelBtnForward,LV_SYMBOL_NEXT);
	lv_obj_center(labelBtnForward);
	
	//Cria bot�o Repeat
	lv_obj_t * btnRepeat = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btnRepeat,repeat_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnRepeat,btnForward,LV_ALIGN_OUT_RIGHT_TOP, 5, 0);
	lv_obj_add_style(btnRepeat, &style, 0);
	lv_obj_set_width(btnRepeat, 60);
	lv_obj_set_height(btnRepeat, 60);
	//Label Shuffle
	labelBtnRepeat = lv_label_create(btnRepeat);
	lv_label_set_text(labelBtnRepeat,LV_SYMBOL_REFRESH);
	lv_obj_center(labelBtnRepeat);
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/


static void task_envia(void *pvParameters) {
	hc05_init();
	config_usart0();
	char caractere;
	for (;;)  {
		if (xQueueReceive(xQueueEnvia, &caractere, 1000)) {
			while(!usart_is_tx_ready(USART_COM)) {
				continue;
			}
			usart_write(USART_COM, caractere);
			
			while(!usart_is_tx_ready(USART_COM)) {
				continue;
			}
			usart_write(USART_COM, EOF);
		}
	}
}


static void task_lvgl(void *pvParameters) {
	int px, py;

	lv_tela();

	for (;;)  {
		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}


/************************************************************************/
/* configs                                                              */
/************************************************************************/

static void configure_lcd(void) {
	/**LCD pin configure on SPI*/
	pio_configure_pin(LCD_SPI_MISO_PIO, LCD_SPI_MISO_FLAGS);  //
	pio_configure_pin(LCD_SPI_MOSI_PIO, LCD_SPI_MOSI_FLAGS);
	pio_configure_pin(LCD_SPI_SPCK_PIO, LCD_SPI_SPCK_FLAGS);
	pio_configure_pin(LCD_SPI_NPCS_PIO, LCD_SPI_NPCS_FLAGS);
	pio_configure_pin(LCD_SPI_RESET_PIO, LCD_SPI_RESET_FLAGS);
	pio_configure_pin(LCD_SPI_CDS_PIO, LCD_SPI_CDS_FLAGS);
	
	ili9341_init();
	ili9341_backlight_on();
}

/************************************************************************/
/* port lvgl                                                            */
/************************************************************************/

void my_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
	ili9341_set_top_left_limit(area->x1, area->y1);   ili9341_set_bottom_right_limit(area->x2, area->y2);
	ili9341_copy_pixels_to_screen(color_p,  (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
	
	/* IMPORTANT!!!
	* Inform the graphics library that you are ready with the flushing*/
	lv_disp_flush_ready(disp_drv);
}

void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data) {
	int px, py, pressed;
	
	if (readPoint(&px, &py))
		data->state = LV_INDEV_STATE_PRESSED;
	else
		data->state = LV_INDEV_STATE_RELEASED; 
	
	data->point.x = px;
	data->point.y = py;
}

void configure_lvgl(void) {
	lv_init();
	lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX);
	
	lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
	disp_drv.draw_buf = &disp_buf;          /*Set an initialized buffer*/
	disp_drv.flush_cb = my_flush_cb;        /*Set a flush callback to draw to the display*/
	disp_drv.hor_res = LV_HOR_RES_MAX;      /*Set the horizontal resolution in pixels*/
	disp_drv.ver_res = LV_VER_RES_MAX;      /*Set the vertical resolution in pixels*/

	lv_disp_t * disp;
	disp = lv_disp_drv_register(&disp_drv); /*Register the driver and save the created display objects*/
	
	/* Init input on LVGL */
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = my_input_read;
	lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
}


/************************************************************************/
/* main                                                                 */
/************************************************************************/
int main(void) {
	/* board and sys init */
	board_init();
	sysclk_init();
	configure_console();

	/* LCd, touch and lvgl init*/
	configure_lcd();
	configure_touch();
	configure_lvgl();
	xQueueEnvia = xQueueCreate(32, sizeof(char));
	if (xQueueEnvia == NULL)
		printf("falha em criar a queue xQueueEnvia \n");
	/* Create task to control oled */
	if (xTaskCreate(task_lvgl, "LVGL", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lvgl task\r\n");
	}	
	if (xTaskCreate(task_envia, "Envia", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create envia task\r\n");
	}
	shuffle = 0;
	pausa = 0;
	repeat = 0;
	/* Start the scheduler. */
	vTaskStartScheduler();

	while(1);
}