/**
 * @file    task_networkmgmt.h
 * @brief   Header for combined NetworkMgmt (DHCP/ARP + SNMP) task
 */
#ifndef TASK_NETWORKMGMT_H
#define TASK_NETWORKMGMT_H

#include "w5500_driver.h"

void NetworkMgmtTask_Init(W5500_Context_t *w5500_ctx);
void vNetworkMgmtTask(void *argument);

#endif /* TASK_NETWORKMGMT_H */
