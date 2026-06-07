#ifndef TASK_NETMGMT_TELNET_H
#define TASK_NETMGMT_TELNET_H

#include "w5500_driver.h"

void NetMgmtTelnetTask_Init(W5500_Context_t *w5500_ctx);
void vNetMgmtTelnetTask(void *argument);

#endif /* TASK_NETMGMT_TELNET_H */
