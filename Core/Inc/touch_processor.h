#ifndef TOUCH_PROCESSOR_H
#define TOUCH_PROCESSOR_H

#include <stdint.h>

void TouchProcessor_Init(void);
void TouchProcessor_FeedData(uint8_t* data, uint16_t len);
void TouchProcessor_Task(void);

#endif
