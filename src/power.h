/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __POWER_H__
#define __POWER_H__

int power_init(void);
int reset_init(void);
int status_led_init(void);

int power_on(void);
int power_off(void);
bool get_power_state(void);
int power_reset(void);

#endif
