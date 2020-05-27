/*
	Copyright 2019 Benjamin Vedder	benjamin@vedder.se

	This file is part of the VESC firmware.

	The VESC firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The VESC firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include "app.h"
#include "ch.h"
#include "hal.h"

// Some useful includes
#include "mc_interface.h"
#include "utils.h"
#include "encoder.h"
#include "terminal.h"
#include "comm_can.h"
#include "hw.h"
#include "commands.h"
#include "timeout.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

#define VALUES_COUNT_ADC 20
#define VALUES_COUNT_RPM 20

// Threads
static THD_FUNCTION(my_thread, arg);
static THD_WORKING_AREA(my_thread_wa, 2048);

// Private functions
static void terminal_test(int argc, const char **argv);

// Private variables
static volatile bool stop_now = true;
static volatile bool is_running = false;
bool was_stepped_on = false;
double values_adc[VALUES_COUNT_ADC];
float values_rpm[VALUES_COUNT_RPM];

int current_value_adc = 0, current_value_rpm = 0;

// Called when the custom application is started. Start our
// threads here and set up callbacks.
void app_custom_start(void) {
	stop_now = false;
	chThdCreateStatic(my_thread_wa, sizeof(my_thread_wa),
			NORMALPRIO, my_thread, NULL);

	// Terminal commands for the VESC Tool terminal can be registered.
	terminal_register_command_callback(
			"custom_cmd",
			"Print the number d",
			"[d]",
			terminal_test);
}

// Called when the custom application is stopped. Stop our threads
// and release callbacks.
void app_custom_stop(void) {
	terminal_unregister_callback(terminal_test);

	stop_now = true;
	while (is_running) {
		chThdSleepMilliseconds(1);
	}
}

void app_custom_configure(app_configuration *conf) {
	(void)conf;
}

float avg_rpm(){
	float rpm = 0;
	for(int i = 0; i < VALUES_COUNT_RPM; i++) rpm += values_rpm[i];
	return rpm / VALUES_COUNT_RPM;
}

double avg_adc(){
	double adc = 0;
	for(int i = 0; i < VALUES_COUNT_ADC; i++) adc += values_adc[i];
	return adc / VALUES_COUNT_ADC;
}

bool is_stepped_on(){
	return avg_adc() < 3.0;
}

void filter_loop(){
	double adc = (double)ADC_VOLTS(ADC_IND_EXT);
	values_adc[current_value_adc] = adc;
	current_value_adc = (current_value_adc + 1) % VALUES_COUNT_ADC;
	
	float rpm = mc_interface_get_rpm();
	values_rpm[current_value_rpm] = rpm;
	current_value_rpm = (current_value_rpm + 1) % VALUES_COUNT_RPM;
	
	
	// commands_printf("adc: %.2f avg: %.2f", adc, avg_adc());
}

static THD_FUNCTION(my_thread, arg) {
	(void)arg;

	chRegSetThreadName("App Custom");

	is_running = true;

	for(;;) {
		// Check if it is time to stop.
		if (stop_now) {
			is_running = false;
			return;
		}

		timeout_reset(); // Reset timeout if everything is OK.

		// Run your logic here. A lot of functionality is available in mc_interface.h.
		
		filter_loop();
		
		bool stepped_on = is_stepped_on();
		
		if(stepped_on != was_stepped_on){
			was_stepped_on = stepped_on;
			if(stepped_on){
				commands_printf("stepped on");
				float rpm = avg_rpm();
				mc_interface_set_pid_speed(rpm);
			}else{
				commands_printf("stepped off");
				mc_interface_release_motor();
			}
		}

		chThdSleepMilliseconds(10);
	}
}


// Callback function for the terminal command with arguments.
static void terminal_test(int argc, const char **argv) {
	if (argc == 2) {
		int d = -1;
		sscanf(argv[1], "%d", &d);

		commands_printf("You have entered %d", d);

		// For example, read the ADC inputs on the COMM header.
		commands_printf("ADC1: %.2f V ADC2: %.2f V",
				(double)ADC_VOLTS(ADC_IND_EXT), (double)ADC_VOLTS(ADC_IND_EXT2));
	} else {
		commands_printf("This command requires one argument.\n");
	}
}
