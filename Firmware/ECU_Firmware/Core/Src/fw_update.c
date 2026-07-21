#include "fw_update.h"
#include "fw_metadata.h"
#include "usart.h"
#include <string.h>

void FW_Update_ConfirmBoot(void)
{
#ifdef BUILD_BANK_B
    fw_meta_t meta;

    FW_Meta_Read(&meta);

    if (meta.bankB_status == BANK_TRIAL)
    {
        meta.bankB_status = BANK_CONFIRMED;
        FW_Meta_Write(&meta);

        const char *msg = "CONFIRM: TRIAL -> CONFIRMED\r\n";
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
    else if (meta.bankB_status == BANK_CONFIRMED)
    {
        const char *msg = "CONFIRM: ALREADY CONFIRMED\r\n";
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
    else
    {
        const char *msg = "CONFIRM: NOT IN TRIAL (unexpected state)\r\n";
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
    }
#endif
}
