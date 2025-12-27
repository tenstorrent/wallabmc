/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __RTC_H__
#define __RTC_H__

#if defined(CONFIG_RTC)
int rtc_init(void);
#else /* defined(CONFIG_RTC) */
static inline int rtc_init(void) { return 0; }
#endif /* defined(CONFIG_RTC) */

#endif /* __RTC_H__ */
