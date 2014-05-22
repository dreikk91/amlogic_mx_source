/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/saradc.h>
#include <linux/adc_keypad.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>

#define LCD_SCREEN_X	1024
#define LCD_SCREEN_Y 	600
#define TRACKING_ID	10
#define ADC_KEY		200	
#define ADC_VALUE	60//100
#define XCENTER		100
#define MID_BLIND	120
//#define MINEDG_BLIND	25
//#define MAXEDG_BLIND	(1024 - 25)
//y = (value - JX2) * JX1 / 100 + JX2
#define JX1	 	138
#define JX2	 	(512 + 426)


#define STEP1		(512 - ADC_VALUE)
#define STEP2		(512 - ADC_VALUE*2)
#define STEP3		(512 - ADC_VALUE*3)
#define STEP4		(512 - ADC_VALUE*4)
#define STEP5		(512 - ADC_VALUE*5)
#define STEP6		(512 - ADC_VALUE*6)
#define STEP7		(512 - ADC_VALUE*7 + 10)
#define STEP8		(512 - ADC_VALUE*7 - 20)
#define	VALUE1		1
#define	VALUE2		1
#define	VALUE3		1
#define	VALUE4		2
#define	VALUE5		3
#define	VALUE6		4
#define	VALUE7		6
#define	VALUE8		9

#define	X1		1
#define	X2		2
#define	X3		3
#define	X4		4
#define	X5		5
#define	VX1		6
#define	VX2		4
#define	VX3		3
#define	VX4		2
#define	VX5		1

#define CENTER_TRY	3

//1, circle_x, circle_y, r, ax, ay, bx, by, xx, xy, yx, yy, lx, ly, rx, ry, l2x, l2y, r2x, r2y, view，view_x, view_y, leftx, lefty, rightx, righty, upx, upy, downx, downy
//2, circle_x, circle_y, r, ax, ay, bx, by, xx, xy, yx, yy, lx, ly, rx, ry, l2x, l2y, r2x, r2y, view_x，view_y, view_r, leftx, lefty, rightx, righty, upx, upy, downx, downy
static long key_param[31];

struct kp {
	struct input_dev *input_keytouch;
	struct input_dev *input_joystick;
	int js_value[SARADC_CHAN_NUM];
	int js_flag[SARADC_CHAN_NUM];
	struct timer_list timer;
	unsigned int cur_keycode[SARADC_CHAN_NUM];
	unsigned int cur_keycode_status[SARADC_CHAN_NUM];
	unsigned int tmp_code[SARADC_CHAN_NUM];
	int count[SARADC_CHAN_NUM];	
	int config_major;
	char config_name[20];
	struct class *config_class;
	struct device *config_dev;
	int chan[SARADC_CHAN_NUM];
	int key_code[SARADC_CHAN_NUM];
	int key_value[SARADC_CHAN_NUM];
	int key_valid[SARADC_CHAN_NUM];
	int circle_flag[2];
	int old_x, old_y;
	int chan_num;
	struct adc_key *key;
	struct work_struct work_update;
};
static struct kp *gp_kp=NULL;

static int release = 1;
static int second0 = 0, second1 = 0;

struct game_key{
	unsigned char *name;
	int code;       
	int value;
	int old_value;
	int flag;
	int num;   	//touch param num
	int id;		//touch point id
	int point;	//reporte gamekey touch point(report = 1; key only = 0)
};

static struct game_key gamekeys[] = {
	//name		code		value	old_value	flag	num	id	point      
	{"keya",	BTN_A,		0,	0,     		0, 	4,	2,	1},
	{"keyb",	BTN_B,		0,	0,     		0, 	6,	3,	1},
	{"keyx",	BTN_X,		0,	0,     		0, 	8,	4,	1},
	{"keyy",	BTN_Y,		0,	0,     		0, 	10,	5,	1},
	{"keyl",	BTN_TL,		0,	0,     		0, 	12,	6,	1},
	{"keyr",	BTN_TR,		0,	0,     		0, 	14,	7,	1},
	{"keyl2",	BTN_TL2,	0,	0,     		0, 	16,	8,	1},
	{"keyr2",	BTN_TR2,	0,	0,     		0, 	18,	9,	1},
	//{"LEFT",	KEY_LEFT,	0,	0,     		0, 	23,	12,	1},
	//{"RIGHT",	KEY_RIGHT,	0,	0,     		0, 	25,	13,	1},
	//{"UP",	KEY_UP,		0,	0,     		0, 	27,	10,	1},
	//{"DOWN",	KEY_DOWN,	0,	0,     		0, 	29,	11,	1},
	//{"START",	BTN_START,	0,	0,     		0, 	0,	0,	0},
	//{"SELECT",	BTN_SELECT,	0,	0,     		0, 	0,	0,	0},
};
static int keynum = sizeof(gamekeys)/sizeof(gamekeys[0]);
static int key_flag_num = sizeof(gamekeys)/sizeof(gamekeys[0]);
static unsigned int keyvalue = 0;

static void gpio_init(void)
{
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 3, 1);
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 4, 1);
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 5, 1);
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 6, 1);
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 7, 1);
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 8, 1);
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 9, 1);
	WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 10, 1);

	WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_0, 0, 6, 1);
	WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_3, 0, 0, 3);
	WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_3, 0, 5, 1);
	WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_6, 0, 19, 5);

	set_gpio_mode(GPIOA_bank_bit0_27(3), GPIOA_bit_bit0_27(3), GPIO_INPUT_MODE);
	set_gpio_mode(GPIOA_bank_bit0_27(4), GPIOA_bit_bit0_27(4), GPIO_INPUT_MODE);
	set_gpio_mode(GPIOA_bank_bit0_27(5), GPIOA_bit_bit0_27(5), GPIO_INPUT_MODE);
	set_gpio_mode(GPIOA_bank_bit0_27(6), GPIOA_bit_bit0_27(6), GPIO_INPUT_MODE);

	set_gpio_mode(GPIOA_bank_bit0_27(7), GPIOA_bit_bit0_27(7), GPIO_INPUT_MODE);
	set_gpio_mode(GPIOA_bank_bit0_27(8), GPIOA_bit_bit0_27(8), GPIO_INPUT_MODE);
	set_gpio_mode(GPIOA_bank_bit0_27(9), GPIOA_bit_bit0_27(9), GPIO_INPUT_MODE);
	set_gpio_mode(GPIOA_bank_bit0_27(10), GPIOA_bit_bit0_27(10), GPIO_INPUT_MODE);
}

static void read_keys_value(void)
{
	gamekeys[0].value = gpio_in_get(PAD_GPIOA_8);
	gamekeys[1].value = gpio_in_get(PAD_GPIOA_9);
	gamekeys[2].value = gpio_in_get(PAD_GPIOA_7);
	gamekeys[3].value = gpio_in_get(PAD_GPIOA_10);
	gamekeys[4].value = gpio_in_get(PAD_GPIOA_4);
	gamekeys[5].value = gpio_in_get(PAD_GPIOA_6);
	gamekeys[6].value = gpio_in_get(PAD_GPIOA_3);
	gamekeys[7].value = gpio_in_get(PAD_GPIOA_5);
}

static void js_report(struct kp *kp, long value, int id)
{
	if (id == 0) {
		if (value == 0) {
			if (kp->js_flag[0]) {
				input_report_abs(kp->input_joystick, ABS_X, 0);
				kp->js_flag[0] = 0;
			}
		} else {
			kp->js_flag[0] = 1;
			input_report_abs(kp->input_joystick, ABS_X, value);
		}
	}
	if (id == 1) {
		if (value == 0) {
			if (kp->js_flag[1]) {
				input_report_abs(kp->input_joystick, ABS_Y, 0);
				kp->js_flag[1] = 0;
			}
		} else {
			kp->js_flag[1] = 1;
			input_report_abs(kp->input_joystick, ABS_Y, value);
		}
	}
	if (id == 2) {
		if (value == 0) {
			if (kp->js_flag[2]) {
				//input_report_abs(kp->input_joystick, ABS_RX, 0);
				input_report_abs(kp->input_joystick, ABS_Z, 0);
				kp->js_flag[2] = 0;
			}
		} else {
			kp->js_flag[2] = 1;
			//input_report_abs(kp->input_joystick, ABS_RX, value);
			input_report_abs(kp->input_joystick, ABS_Z, value);
		}
	}
	if (id == 5) {
		if (value == 0) {
			if (kp->js_flag[5]) {
				//input_report_abs(kp->input_joystick, ABS_RY, 0);
				input_report_abs(kp->input_joystick, ABS_RZ, 0);
				kp->js_flag[5] = 0;
			}
		} else {
			kp->js_flag[5] = 1;
			//input_report_abs(kp->input_joystick, ABS_RY, value);
			input_report_abs(kp->input_joystick, ABS_RZ, value);
		}
	}
}

static void keytouch_report(struct kp *kp, long x, long y, int id)
{
	if (x != 0 || y != 0) {
		input_report_key(kp->input_keytouch, BTN_TOUCH, 1);
		input_report_abs(kp->input_keytouch, ABS_MT_TRACKING_ID, id);
		input_report_abs(kp->input_keytouch, ABS_MT_TOUCH_MAJOR, 20);
		input_report_abs(kp->input_keytouch, ABS_MT_WIDTH_MAJOR, 20);
		input_report_abs(kp->input_keytouch, ABS_MT_POSITION_X, x);
		input_report_abs(kp->input_keytouch, ABS_MT_POSITION_Y, y);
		input_mt_sync(kp->input_keytouch);
	}
	release = 1;
}

static void keytouch_release(struct kp *kp)
{
	release = 0;
	input_report_key(kp->input_keytouch, BTN_TOUCH, 0);
	input_report_abs(kp->input_keytouch, ABS_MT_TOUCH_MAJOR, 0);
	input_report_abs(kp->input_keytouch, ABS_MT_WIDTH_MAJOR, 0);
	input_mt_sync(kp->input_keytouch);
	input_sync(kp->input_keytouch);
	//printk("-------------- release all point -----------------\n");
}

static void scan_joystick(struct kp *kp, int channel)
{
	int value;
	long int js_value;

	//for (i=0; i<kp->chan_num; i++) {
	value = get_adc_sample(kp->chan[channel]);
	if (value >= 0) {
		if ((value >= 1023 / 2 - MID_BLIND) && (value <= 1023 / 2 + MID_BLIND)) {
			kp->js_value[channel] = 0;
		} else {
			js_value = value;
			if (channel == 1 || channel == 2 || channel == 3) {
				js_value = 1023 - js_value;
			}
			if (channel == 0 || channel == 1 || channel == 2 || channel == 3) {	
				if (js_value >= 512)
					js_value = (((js_value - JX2) * JX1 / 100 + JX2) - 512) / 2;
				else
					js_value = -((((1023 - js_value) - JX2) * JX1 / 100 + JX2) - 512) / 2;
				if (js_value <= -256)
					js_value = -256;
				else if (js_value >= 255)
					js_value = 255;
				/*
				if (js_value <= MINEDG_BLIND)
					js_value = -256;
				else if (js_value >= MAXEDG_BLIND)
					js_value = 255;
				else
					js_value = (js_value - 512) / 2;
				*/
				//printk("---------------------- %i js_value = %d -------------------\n", i, js_value);
			}
			kp->js_value[channel] = js_value;
		}
	}
	return 0;
}

static void scan_left_joystick(struct kp *kp)
{
	scan_joystick(kp, 2);
	scan_joystick(kp, 3);
	return 0;
}

static void scan_right_joystick(struct kp *kp)
{
	scan_joystick(kp, 0);
	scan_joystick(kp, 1);
	return 0;
}

static void scan_joystick_touchmapping(struct kp *kp, int channel)
{
	int value;

	value = get_adc_sample(kp->chan[channel]);
	if (value >= 0) {
		if ((value >= 1023 / 2 - MID_BLIND) && (value <= 1023 / 2 + MID_BLIND)) {
			kp->key_valid[channel] = 0;
		} else {
			kp->key_valid[channel] = 1;
		}
		kp->key_value[channel] = value;
	}

	return 0;
}
static void scan_left_joystick_touchmapping(struct kp *kp)
{
	scan_joystick_touchmapping(kp, 2);
	scan_joystick_touchmapping(kp, 3);
}
static void scan_right_joystick_touchmapping(struct kp *kp)
{
	scan_joystick_touchmapping(kp, 0);
	scan_joystick_touchmapping(kp, 1);
}

static void scan_android_key(struct kp *kp)
{
	int value;

	//channel 4
	value = get_adc_sample(kp->chan[4]);
	if (value >= 0) {
		if (value >= (150 - 40) && value <= (150 + 40))
			kp->key_code[4] = KEY_VOLUMEDOWN;
		else if (value >= (275 - 40) && value <= (275 + 40))
			kp->key_code[4] = KEY_VOLUMEUP;
		else if (value >= 0 && value <= (9 + 40))
			kp->key_code[4] = BTN_SELECT;
		else if (value >= (392 - 40) && value <= (392 + 40))
			kp->key_code[4] = BTN_START;
		else
			kp->key_code[4] = 0;
	}

	return 0;
}


static void report_joystick_key(struct kp *kp)
{
	int i;

	read_keys_value();
	for (i = 0; i < keynum; i++) {
		if(gamekeys[i].value == gamekeys[i].old_value) {
			if (gamekeys[i].value == gamekeys[i].flag) {
				if(gamekeys[i].value) {
					input_report_key(kp->input_joystick, gamekeys[i].code, 1);
					input_mt_sync(kp->input_joystick);
					gamekeys[i].flag = 0;
					//printk("%s press\n", gamekeys[i].name);
				} else {
					input_report_key(kp->input_joystick, gamekeys[i].code, 0);
					input_mt_sync(kp->input_joystick);
					gamekeys[i].flag = 1;
					//printk("%s release\n", gamekeys[i].name);
				}
			}
		}
		gamekeys[i].old_value = gamekeys[i].value;
	}
}


static void report_keytouch_key(struct kp *kp)
{
	int i;

	read_keys_value();
	for (i = 0; i < keynum; i++) {
		if(gamekeys[i].value == gamekeys[i].old_value) {
			if(gamekeys[i].value && gamekeys[i].point) {
				keytouch_report(kp, key_param[gamekeys[i].num], key_param[gamekeys[i].num + 1], gamekeys[i].id);
				gamekeys[i].flag = 0;
			} else {
				gamekeys[i].flag = 1;
			}
		}
		gamekeys[i].old_value = gamekeys[i].value;
	}
}


static ssize_t key_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	char i;
	for (i=0; i<31; i++) {
		printk("key_param[%d] = %d \n", i, key_param[i]);
	}
	return 0;
}

static ssize_t key_write(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld", \
			&key_param[0], &key_param[1], &key_param[2], &key_param[3], \
			&key_param[4], &key_param[5], &key_param[6], &key_param[7], \
			&key_param[8], &key_param[9], &key_param[10], &key_param[11], \
			&key_param[12], &key_param[13], &key_param[14], &key_param[15], \
			&key_param[16], &key_param[17], &key_param[18], &key_param[19], \
			&key_param[20], &key_param[21], &key_param[22], \
			&key_param[23], &key_param[24], &key_param[25], &key_param[26], \
			&key_param[27], &key_param[28], &key_param[29], &key_param[30]);
	if (key_param[0] == 1) {
		if (key_param[20] == 0)
			key_param[20] = X3;

		if (key_param[20] == X1)
			key_param[20] = VX1;
		else if (key_param[20] == X2)
			key_param[20] = VX2;
		else if (key_param[20] == X3)
			key_param[20] = VX3;
		else if (key_param[20] == X4)
			key_param[20] = VX4;
		else if (key_param[20] == X5)
			key_param[20] = VX5;
		else
			key_param[20] = VX3;

		if (key_param[21] == 0 || key_param[22] == 0) {
			key_param[21] = (LCD_SCREEN_X+XCENTER)/2;
			key_param[22] = LCD_SCREEN_Y/2;
		}
		gp_kp->old_x = key_param[21];
		gp_kp->old_y = key_param[22];
	}

	return count;
}
static DEVICE_ATTR(key, S_IRWXUGO, key_read, key_write);

static struct attribute *key_attr[] = {
	&dev_attr_key.attr,
	NULL
};
static struct attribute_group key_attr_group = {
	.name = NULL,
	.attrs = key_attr,
};

static int view_count;
static long int x, y;
static void circle_move(struct kp *kp)
{
	int tmp_x = 0;
	int tmp_y = 0;
	int dx = 0, dy = 0;

	if(view_count < key_param[20]) {
		x += kp->key_value[1];
		y += kp->key_value[0];
		kp->key_value[1] = kp->old_x;
		kp->key_value[0] = kp->old_y;
		view_count++;
		return;
	} else {
		x /= key_param[20];	
		y /= key_param[20];	
		view_count = 0;
	}

	if (512 - x >= 0) {
		if (x < STEP8) 
			tmp_x = kp->old_x + VALUE8;
		else if (x < STEP7) 
			tmp_x = kp->old_x + VALUE7;
		else if (x < STEP6) 
			tmp_x = kp->old_x + VALUE6;
		else if (x < STEP5) 
			tmp_x = kp->old_x + VALUE5;
		else if (x < STEP4) 
			tmp_x = kp->old_x + VALUE4;
		else if (x < STEP3) 
			tmp_x = kp->old_x + VALUE3;
		else if (x < STEP2) 
			tmp_x = kp->old_x + VALUE2;
		else if (x < STEP1) 
			tmp_x = kp->old_x + VALUE1;
		else
			tmp_x = kp->old_x;
	} else {
		dx = 1024 - x;
		if (dx < STEP8) 
			tmp_x = kp->old_x - VALUE8;
		else if (dx < STEP7) 
			tmp_x = kp->old_x - VALUE7;
		else if (dx < STEP6) 
			tmp_x = kp->old_x - VALUE6;
		else if (dx < STEP5) 
			tmp_x = kp->old_x - VALUE5;
		else if (dx < STEP4) 
			tmp_x = kp->old_x - VALUE4;
		else if (dx < STEP3) 
			tmp_x = kp->old_x - VALUE3;
		else if (dx < STEP2) 
			tmp_x = kp->old_x - VALUE2;
		else if (dx < STEP1) 
			tmp_x = kp->old_x - VALUE1;
		else
			tmp_x = kp->old_x;
	}

	if (y - 512 >= 0) {
		dy = 1024 - y;
		if (dy < STEP8) 
			tmp_y = kp->old_y + VALUE8;
		else if (dy < STEP7) 
			tmp_y = kp->old_y + VALUE7;
		else if (dy < STEP6) 
			tmp_y = kp->old_y + VALUE6;
		else if (dy < STEP5) 
			tmp_y = kp->old_y + VALUE5;
		else if (dy < STEP4) 
			tmp_y = kp->old_y + VALUE4;
		else if (dy < STEP3) 
			tmp_y = kp->old_y + VALUE3;
		else if (dy < STEP2) 
			tmp_y = kp->old_y + VALUE2;
		else if (dy < STEP1) 
			tmp_y = kp->old_y + VALUE1;
		else
			tmp_y = kp->old_y;
	} else {
		if (y < STEP8) 
			tmp_y = kp->old_y - VALUE8;
		else if (y < STEP7) 
			tmp_y = kp->old_y - VALUE7;
		else if (y < STEP6) 
			tmp_y = kp->old_y - VALUE6;
		else if (y < STEP5) 
			tmp_y = kp->old_y - VALUE5;
		else if (y < STEP4) 
			tmp_y = kp->old_y - VALUE4;
		else if (y < STEP3) 
			tmp_y = kp->old_y - VALUE3;
		else if (y < STEP2) 
			tmp_y = kp->old_y - VALUE2;
		else if (y < STEP1) 
			tmp_y = kp->old_y - VALUE1;
		else
			tmp_y = kp->old_y;
	}

	if (tmp_x > LCD_SCREEN_X) {
		tmp_x = LCD_SCREEN_X;
		tmp_y = kp->old_y;
	}
	if (tmp_x < 0) {
		tmp_x = 0;
		tmp_y = kp->old_y;
	}
	if (tmp_y > LCD_SCREEN_Y) {
		tmp_y = LCD_SCREEN_Y;
		tmp_x = kp->old_x;
	}
	if (tmp_y < 0) {
		tmp_y = 0;
		tmp_x = kp->old_x;
	}

	kp->key_value[1] = tmp_x;
	kp->key_value[0] = tmp_y;
	kp->old_x = tmp_x;
	kp->old_y = tmp_y;
	x = 0;
	y = 0;
}

static void kp_work(struct kp *kp)
{
	int i, code;
	int tmp = 1;


	/*******************************************************************************/
	//add for adc keys(channel 4)
	//report VOL+, VOL-, start, select
	scan_android_key(kp);
	i = 4;
	code = kp->key_code[i];
	if ((!code) && (!kp->cur_keycode[i])) {
		;
	} else if (code != kp->tmp_code[i]) {
		kp->tmp_code[i] = code;
		kp->count[i] = 0;
	} else if(++kp->count[i] == 2) {
		if (kp->cur_keycode[i] != code) {
			if (!code) {
				kp->cur_keycode_status[i] = 0;
				//printk("key %d up\n", kp->cur_keycode[i]);
				input_report_key(kp->input_joystick, kp->cur_keycode[i], 0);
				kp->cur_keycode[i] = code;
			} else if (kp->cur_keycode_status[i] == 1) {
				//printk("key %d up(force)\n", kp->cur_keycode[i]);
				input_report_key(kp->input_joystick, kp->cur_keycode[i], 0);
				kp->cur_keycode_status[i] = 0;
				kp->count[i] = 0;
			} else {
				kp->cur_keycode_status[i] = 1;
				//printk("key %d down\n", code);
				input_report_key(kp->input_joystick, code, 1);
				kp->cur_keycode[i] = code;
			}
		}
	}
	//end
	/*******************************************************************************/
	

	/*******************************************************************************/
	//report joystick
	if (key_param[0] == 0 || key_param[1] < 0 || key_param[2] < 0) {
		scan_left_joystick(kp);
		js_report(kp, kp->js_value[2], 0); //left
		js_report(kp, kp->js_value[3], 1); //left
		input_sync(kp->input_joystick);
	}
	if (key_param[0] == 0) {
		scan_right_joystick(kp);
		js_report(kp, kp->js_value[0], 5); //right
		js_report(kp, kp->js_value[1], 2); //right
		input_sync(kp->input_joystick);
	} else if (key_param[0] == 1 && (key_param[21] < 0 || key_param[22] < 0)) {
		scan_right_joystick(kp);
		js_report(kp, kp->js_value[0], 5); //right
		js_report(kp, kp->js_value[1], 2); //right
		input_sync(kp->input_joystick);
	} else if (key_param[0] == 2 && (key_param[20] < 0 || key_param[21] < 0)) {
		scan_right_joystick(kp);
		js_report(kp, kp->js_value[0], 5); //right
		js_report(kp, kp->js_value[1], 2); //right
		input_sync(kp->input_joystick);
	}
	if (key_param[0] == 0) {
		report_joystick_key(kp);
		input_sync(kp->input_joystick);
	}
	//end
	/*******************************************************************************/


	/*******************************************************************************/
	//report key mapping
	//left joystick
	if ((key_param[0] == 1 || key_param[0] == 2) && key_param[1] >= 0 && key_param[2] >= 0) {
		scan_left_joystick_touchmapping(kp);
		if ((kp->key_valid[2] == 1) || (kp->key_valid[3] == 1)) {
			kp->circle_flag[0] = 1;
			if(second0 < CENTER_TRY) {
				if(second0 == 0)
					keytouch_report(kp, key_param[1], key_param[2], 0);
				if(second0 == 1)
					keytouch_report(kp, key_param[1] + 1, key_param[2], 0);
				if(second0 == 2)
					keytouch_report(kp, key_param[1], key_param[2] + 1, 0);
				if(second0 == 3)
					keytouch_report(kp, key_param[1] - 1, key_param[2], 0);
				if(second0 == 4)
					keytouch_report(kp, key_param[1], key_param[2] - 1, 0);
				second0++;
			} else {
				keytouch_report(kp, key_param[1] +  (512 - kp->key_value[2]) * key_param[3] / 512, 
						key_param[2] +  (512 - kp->key_value[3]) * key_param[3] / 512, 0);
			}
		} else if (kp->circle_flag[0] == 1) {
			kp->circle_flag[0] = 0;
			second0 = 0;
		}
	} else if (key_param[0] == 1 || key_param[0] == 2) {
		kp->circle_flag[0] = 0;
		second0 = 0;
	}


	//right joystick
	if (key_param[0] == 1 && key_param[21] >= 0 && key_param[22] >= 0) { //mode 1
		scan_right_joystick_touchmapping(kp);
		if ((kp->key_valid[0] == 1) || (kp->key_valid[1] == 1)) {
			kp->circle_flag[1] = 1;
			if(!second1) {
				keytouch_report(kp, key_param[21], key_param[22], 1);
				second1 = 1;
			} else {
				circle_move(kp);
				keytouch_report(kp, kp->key_value[1], kp->key_value[0], 1);
			}
		} else if (kp->circle_flag[1] == 1) {
			kp->circle_flag[1] = 0;
			second1 = 0;
			kp->old_x = key_param[21];
			kp->old_y = key_param[22];
		}
	} else if (key_param[0] == 2 && key_param[20] >= 0 && key_param[21] >= 0) { //mode 2
		scan_right_joystick_touchmapping(kp);
		if ((kp->key_valid[0] == 1) || (kp->key_valid[1] == 1)) {
			kp->circle_flag[1] = 1;
			if(second1 < CENTER_TRY) {
				if(second1 == 0)
					keytouch_report(kp, key_param[20], key_param[21], 1);
				if(second1 == 1)
					keytouch_report(kp, key_param[20] + 1, key_param[21], 1);
				if(second1 == 2)
					keytouch_report(kp, key_param[20], key_param[21] + 1, 1);
				if(second1 == 3)
					keytouch_report(kp, key_param[20] - 1, key_param[21], 1);
				if(second1 == 4)
					keytouch_report(kp, key_param[20], key_param[21] - 1, 1);
				second1++;
			} else {
				keytouch_report(kp, key_param[20] +  (512 - kp->key_value[1]) * key_param[22] / 512, 
						key_param[21] +  (kp->key_value[0] - 512) * key_param[22] / 512, 1);
			}
		} else if (kp->circle_flag[1] == 1) {
			kp->circle_flag[1] = 0;
			second1 = 0;
		}
	} else if (key_param[0] == 1 || key_param[0] == 2) {
		kp->circle_flag[1] = 0;
		second1 = 0;
	}
	//end
	
	if ((key_param[0] == 1 || key_param[0] == 2)) {
		report_keytouch_key(kp);
		input_sync(kp->input_keytouch);
		if (release && (kp->circle_flag[0] == 0) && (kp->circle_flag[1] ==0)) {
			for (i = 0; i < key_flag_num; i++) {
				tmp = (tmp * gamekeys[i].flag);
			}
			if (tmp)
				keytouch_release(kp);
		}
	}
	/*******************************************************************************/
}

static void update_work_func(struct work_struct *work)
{
	struct kp *kp_data = container_of(work, struct kp, work_update);

	kp_work(kp_data);
}

static void kp_timer_sr(unsigned long data)
{
	struct kp *kp_data=(struct kp *)data;
	schedule_work(&(kp_data->work_update));
	mod_timer(&kp_data->timer,jiffies+msecs_to_jiffies(10));
}

static int adckpd_config_open(struct inode *inode, struct file *file)
{
	file->private_data = gp_kp;
	return 0;
}

static int adckpd_config_release(struct inode *inode, struct file *file)
{
	file->private_data=NULL;
	return 0;
}

static const struct file_operations keypad_fops = {
	.owner      = THIS_MODULE,
	.open       = adckpd_config_open,
	.release    = adckpd_config_release,
};

static int register_keypad_dev(struct kp  *kp)
{
	int ret=0;
	strcpy(kp->config_name,"am_adc_js");
	ret=register_chrdev(0, kp->config_name, &keypad_fops);
	if(ret<=0)
	{
		printk("register char device error\r\n");
		return  ret ;
	}
	kp->config_major=ret;
	printk("adc keypad major:%d\r\n",ret);
	kp->config_class=class_create(THIS_MODULE,kp->config_name);
	kp->config_dev=device_create(kp->config_class,	NULL,
			MKDEV(kp->config_major,0),NULL,kp->config_name);

	return ret;
}

static int __devinit adc_probe(struct platform_device *pdev)
{
	struct kp *kp;
	int i, ret;
	s8 phys[32];

	kp = kzalloc(sizeof(struct kp), GFP_KERNEL);
	if (!kp) {
		kfree(kp);
		return -ENOMEM;
	}
	gp_kp=kp;

	kp->circle_flag[0] = 0;
	kp->circle_flag[1] = 0;
	kp->old_x = 0;
	kp->old_y = 0;


	for (i=0; i<SARADC_CHAN_NUM; i++) {
		kp->cur_keycode[i] = 0;
		kp->cur_keycode_status[i] = 0;
		kp->tmp_code[i] = 0;
		kp->count[i] = 0;
		kp->js_flag[i] = 0;
	}

	kp->chan_num = 4;
	kp->chan[0] = CHAN_0;
	kp->chan[1] = CHAN_1;
	kp->chan[2] = CHAN_2; //LEFT, RIGHT
	kp->chan[3] = CHAN_3; //UP, DOWN
	kp->chan[4] = CHAN_4; //KEY_VOLUMEDOWN,KEY_VOLUMEUP, KEY_SPACE,KEY_ENTER


	/************************************************************************************/
	//register keytouch
	kp->input_keytouch = input_allocate_device();
	if (!kp->input_keytouch) {
		printk("---------- allocate input_keytouch fail ------------\n");
		kfree(kp);
		input_free_device(kp->input_keytouch);
		return -ENOMEM;
	}

	set_bit(BTN_TOUCH, kp->input_keytouch->keybit);
	set_bit(EV_REP, kp->input_keytouch->evbit);
	set_bit(EV_KEY, kp->input_keytouch->evbit);
	set_bit(EV_ABS, kp->input_keytouch->evbit);
	set_bit(EV_SYN, kp->input_keytouch->evbit);
	set_bit(ABS_MT_TOUCH_MAJOR, kp->input_keytouch->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, kp->input_keytouch->absbit);
	set_bit(ABS_MT_POSITION_X, kp->input_keytouch->absbit);
	set_bit(ABS_MT_POSITION_Y, kp->input_keytouch->absbit);
	set_bit(ABS_MT_TRACKING_ID, kp->input_keytouch->absbit);
	input_set_abs_params(kp->input_keytouch, ABS_MT_POSITION_X, 0, LCD_SCREEN_X, 0, 0);
	input_set_abs_params(kp->input_keytouch, ABS_MT_POSITION_Y, 0, LCD_SCREEN_Y, 0, 0);
	input_set_abs_params(kp->input_keytouch, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(kp->input_keytouch, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(kp->input_keytouch, ABS_MT_TRACKING_ID, 0, TRACKING_ID, 0, 0);

	sprintf(phys, "input/ts");
	kp->input_keytouch->name = "ADC keytouch";
	kp->input_keytouch->phys = phys;
	kp->input_keytouch->dev.parent = &pdev->dev;
	kp->input_keytouch->id.bustype = BUS_ISA;
	kp->input_keytouch->id.vendor = 0x0001;
	kp->input_keytouch->id.product = 0x0001;
	kp->input_keytouch->id.version = 0x100;
	kp->input_keytouch->rep[REP_DELAY]=0xffffffff;
	kp->input_keytouch->rep[REP_PERIOD]=0xffffffff;
	kp->input_keytouch->keycodesize = sizeof(unsigned short);
	kp->input_keytouch->keycodemax = 0x1ff;

	ret = input_register_device(kp->input_keytouch);
	if (ret < 0) {
		printk(KERN_ERR "register input_keytouch device fail\n");
		kfree(kp);
		input_free_device(kp->input_keytouch);
		return -EINVAL;
	}
	/************************************************************************************/

	/************************************************************************************/
	//register joystick
	kp->input_joystick = input_allocate_device();
	if (!kp->input_joystick) {
		printk("---------- allocate input_joystick fail ------------\n");
		kfree(kp);
		input_free_device(kp->input_joystick);
		return -ENOMEM;
	}

	set_bit(KEY_VOLUMEDOWN, kp->input_joystick->keybit);
	set_bit(KEY_VOLUMEUP, kp->input_joystick->keybit);
	set_bit(BTN_START, kp->input_joystick->keybit);
	set_bit(BTN_SELECT, kp->input_joystick->keybit);
	for (i = 0; i < keynum; i++)
		set_bit(gamekeys[i].code, kp->input_joystick->keybit);
	set_bit(EV_REP, kp->input_joystick->evbit);
	set_bit(EV_KEY, kp->input_joystick->evbit);
	set_bit(EV_ABS, kp->input_joystick->evbit);
	set_bit(EV_SYN, kp->input_joystick->evbit);
	input_set_abs_params(kp->input_joystick, ABS_X, -256, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_Y, -256, 255, 0, 0);
	//input_set_abs_params(kp->input_joystick, ABS_RX, -256, 255, 0, 0);
	//input_set_abs_params(kp->input_joystick, ABS_RY, -256, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_Z, -256, 255, 0, 0);
	input_set_abs_params(kp->input_joystick, ABS_RZ, -256, 255, 0, 0);

	kp->input_joystick->name = "ADC joystick";
	kp->input_joystick->rep[REP_DELAY]=0xffffffff;
	kp->input_joystick->rep[REP_PERIOD]=0xffffffff;
	kp->input_joystick->keycodesize = sizeof(unsigned short);
	kp->input_joystick->keycodemax = 0x1ff;
	ret = input_register_device(kp->input_joystick);
	if (ret < 0) {
		printk(KERN_ERR "register input_joystick device fail\n");
		kfree(kp);
		input_free_device(kp->input_joystick);
		return -EINVAL;
	}
	/************************************************************************************/

	platform_set_drvdata(pdev, kp);

	gpio_init();

	register_keypad_dev(gp_kp);
	struct device *dev = &pdev->dev;
	sysfs_create_group(&dev->kobj, &key_attr_group);

	INIT_WORK(&(kp->work_update), update_work_func);
	setup_timer(&kp->timer, kp_timer_sr, kp) ;
	mod_timer(&kp->timer, jiffies+msecs_to_jiffies(100));

	return 0;
}

static int adc_remove(struct platform_device *pdev)
{
	struct kp *kp = platform_get_drvdata(pdev);

	input_unregister_device(kp->input_keytouch);
	input_unregister_device(kp->input_joystick);
	input_free_device(kp->input_keytouch);
	input_free_device(kp->input_joystick);

	unregister_chrdev(kp->config_major,kp->config_name);
	if(kp->config_class)
	{
		if(kp->config_dev)
			device_destroy(kp->config_class,MKDEV(kp->config_major,0));
		class_destroy(kp->config_class);
	}
	kfree(kp);
	gp_kp=NULL ;
	return 0;
}

static struct platform_driver adc_driver = {
	.probe      = adc_probe,
	.remove     = adc_remove,
	.suspend    = NULL,
	.resume     = NULL,
	.driver     = {
		.name   = "mx-adcjs",
	},
};

static int __devinit adc_init(void)
{
	printk(KERN_INFO "ADC joystick Driver init.\n");
	return platform_driver_register(&adc_driver);
}

static void __exit adc_exit(void)
{
	printk(KERN_INFO "ADC joystick Driver exit.\n");
	platform_driver_unregister(&adc_driver);
}

module_init(adc_init);
module_exit(adc_exit);

MODULE_AUTHOR("LegacyROM");
MODULE_DESCRIPTION("ADC Joystick Driver");
MODULE_LICENSE("GPL");
