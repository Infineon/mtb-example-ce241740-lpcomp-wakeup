/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the DeepSleep/Hibernate wakeup using
*              a low-power comparator for ModusToolbox.
*
* Related Document: See README.md
*
*
********************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
********************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* Enables code example to wakeup CPU from Hibernate mode:
 * 0U - wakeup from DeepSleep mode (default).
 * 1U - wakeup from Hibernate mode.
 */
#define WAKEUP_FROM_HIBERNATE_ENABLED               (0U)

/* Power mode string definition for printf */
#if (WAKEUP_FROM_HIBERNATE_ENABLED == 1U)
#define POWER_MODE_STRING                           "Hibernate mode"
#else
#define POWER_MODE_STRING                           "DeepSleep mode"
#endif

/* These are the addresses where the core0 and core1 images are located. */
#define CORE0_IMAGE_ADDRESS    CYMEM_CM33_0_S_m33s_ppca0_nvm_C_S_START    //0x12030000
#define CORE1_IMAGE_ADDRESS    CYMEM_CM33_0_S_m33s_ppca1_nvm_C_S_START    //0x12038000
#define PPCA0_IMAGE_SIZE       CYMEM_CM33_0_S_ppca0_code_SIZE
#define PPCA1_IMAGE_SIZE       CYMEM_CM33_0_S_ppca1_code_SIZE

/*******************************************************************************
* Global Variables
********************************************************************************/
/* Debug UART variables */
static cy_stc_scb_uart_context_t    DEBUG_UART_context; /* DEBUG_UART context */
static mtb_hal_uart_t               DEBUG_UART_hal_obj; /* Debug DEBUG_UART HAL object */

/* User low-power comparator context */
cy_stc_lpcomp_context_t user_lpcomp_context;

#if (WAKEUP_FROM_HIBERNATE_ENABLED != 1U)
/* User low-power comparator interrupt configuration structure */
cy_stc_sysint_t user_lpcomp_intr_config =
{
    .intrSrc = USER_LPCOMP_IRQ,
    .intrPriority = 0U,
};
#endif

/*******************************************************************************
* Function Prototypes
********************************************************************************/
#if (WAKEUP_FROM_HIBERNATE_ENABLED != 1U)
/* User low-power comparator interrupt handler */
void user_lpcomp_intr_handler(void);
#endif


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for the CM33 secure core. It monitors the LPComp
* output and controls the device power mode:
*    1. Unfreeze IOs (if returning from Hibernate)
*    2. Initialize UART, LPComp, and (DeepSleep path) LPComp interrupt
*    3. Boot PPCA Core 0 and Core 1
*    4. If LPComp output is high: blink LED3 at 500 ms (active mode)
*    5. If LPComp output is low: illuminate LED3 for 2 s, then enter
*       DeepSleep (WAKEUP_FROM_HIBERNATE_ENABLED=0) or Hibernate (=1)
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/

int main(void)
{
    cy_rslt_t result;
    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* After Hibernate wakeup, IO pins are automatically frozen by hardware to
     * preserve their pre-Hibernate state. Unfreeze them before any GPIO use. */
    if (Cy_SysPm_GetIoFreezeStatus())
    {
        /* Release IO freeze to restore normal GPIO pin operation */
        Cy_SysPm_IoUnfreeze();
    }

    /* Initialize the debug UART */
    result = Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);
    /* UART init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    /* Setup the HAL UART */
    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config, &DEBUG_UART_context, NULL);
    /* HAL UART init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize retarget-io to use the debug UART port */
    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);
    /* retarget-io init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize low-power comparator */
    if (CY_LPCOMP_SUCCESS != Cy_LPComp_Init_Ext(USER_LPCOMP_HW, USER_LPCOMP_CHANNEL, 
                                        &USER_LPCOMP_config, &user_lpcomp_context))
    {
        CY_ASSERT(0);
    }

#if (WAKEUP_FROM_HIBERNATE_ENABLED != 1U)
    /* Configure the LPComp interrupt for DeepSleep wakeup. In Hibernate mode
     * the LPComp asserts a hardware wakeup signal that causes a device reset,
     * so no interrupt handler is needed for that path. */
    Cy_LPComp_SetInterruptMask(USER_LPCOMP_HW, CY_LPCOMP_COMP1);
    Cy_SysInt_Init(&user_lpcomp_intr_config, user_lpcomp_intr_handler);
    NVIC_EnableIRQ(user_lpcomp_intr_config.intrSrc);
#endif

    /* Enable LPCOMP */
    Cy_LPComp_Enable_Ext(USER_LPCOMP_HW, USER_LPCOMP_CHANNEL, &user_lpcomp_context);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");
    printf("************************************************************\r\n");
    printf("PSOC Control C3M/P8: Wakeup from %s using LPCOMP\r\n", POWER_MODE_STRING);
    printf("************************************************************\r\n\n");

    /* Check the reset reason */
    if(CY_SYSLIB_RESET_HIB_WAKEUP == (Cy_SysLib_GetResetReason() & CY_SYSLIB_RESET_HIB_WAKEUP))
    {
        /* The reset has occurred on a wakeup from Hibernate power mode */
        printf("Wakeup from the Hibernate mode\r\n\r\n");
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Boot PPCA Core 0 image from flash */
    Cy_System_Init_CPU0((void*)CORE0_IMAGE_ADDRESS, PPCA0_IMAGE_SIZE);
    /* Boot PPCA Core 1 image from flash */
    Cy_System_Init_CPU1((void*)CORE1_IMAGE_ADDRESS, PPCA1_IMAGE_SIZE);

    for (;;)
    {
        /* If the comparison result is high, toggles user LED every 500ms */
        if(0 != Cy_LPComp_GetCompare(USER_LPCOMP_HW, USER_LPCOMP_CHANNEL))
        {
            /* Toggle User LED1 every 500ms */
            Cy_GPIO_Inv(CYBSP_USER_LED3_PORT, CYBSP_USER_LED3_PIN);
            Cy_SysLib_Delay(500u);
            printf("In CPU Active mode, blinking user LED at 500ms\r\n\r\n");
        }
        else /* If the comparison result is low, goes to the DeepSleep/Hibernate mode */
        {
            /* Turn on user LED for 2 seconds to indicate the MCU entering DeepSleep/Hibernate mode. */
            printf("Turn on the USER LED for 2 seconds, then enter %s\r\n\r\n", POWER_MODE_STRING);

            Cy_GPIO_Write(CYBSP_USER_LED3_PORT, CYBSP_USER_LED3_NUM, CYBSP_LED_STATE_ON);
            Cy_SysLib_Delay(2000u);
            Cy_GPIO_Write(CYBSP_USER_LED3_PORT, CYBSP_USER_LED3_NUM, CYBSP_LED_STATE_OFF);

#if (WAKEUP_FROM_HIBERNATE_ENABLED == 1U)
            /* Deinitialize the UART before entering Hibernate. On Hibernate wakeup
             * the device performs a full reset, so all peripherals will be
             * re-initialized from scratch by cybsp_init() on the next boot. */
            cy_retarget_io_deinit();

            /* Set the low-power comparator as a wake-up source from Hibernate and jump into Hibernate */
            Cy_SysPm_SetHibernateWakeupSource(CY_SYSPM_HIBERNATE_LPCOMP1_HIGH);
            if(CY_SYSPM_SUCCESS != Cy_SysPm_SystemEnterHibernate())
            {
                printf("The CPU did not enter Hibernate mode\r\n\r\n");
                CY_ASSERT(0);
            }
#else /* DeepSleep mode */
            /* Wait for UART traffic to stop */
            while (cy_retarget_io_is_tx_active());

            /* Sets CPU to the Deep Sleep mode */
            if(CY_SYSPM_SUCCESS != Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT))
            {
                printf("The CPU did not enter DeepSleep mode\r\n\r\n");
                CY_ASSERT(0);
            }
            printf("Wake up from DeepSleep mode\r\n\r\n");
            Cy_SysLib_Delay(500u);
#endif
        }
    }
}

#if (WAKEUP_FROM_HIBERNATE_ENABLED != 1U)
/*******************************************************************************
* Function Name: user_lpcomp_intr_handler
********************************************************************************
* Summary:
* This function is the user low-power comparator interrupt handler.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void user_lpcomp_intr_handler(void)
{
    uint32_t intrStatus = Cy_LPComp_GetInterruptStatusMasked(USER_LPCOMP_HW);
    /* Clear interrupt */
    Cy_LPComp_ClearInterrupt(USER_LPCOMP_HW, intrStatus);
}
#endif

/* [] END OF FILE */
