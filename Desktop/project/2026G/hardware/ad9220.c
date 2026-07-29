#include "ad9220.h"
#include "tim.h"
#include "stdio.h"

uint16_t ad9220_buffer[AD9220_BUF_SIZE];
volatile uint8_t ad9220_data_ready = 0;
volatile uint8_t ad9220_dma_error   = 0;

static DMA_HandleTypeDef hdma_tim1_up;
static const uint32_t tier_arr[AD9220_TIER_COUNT] = {
    839,   // Low:  168MHz / 840  = 200 kSPS
    83,    // Mid:  168MHz / 84   = 2 MSPS
    16     // High: 168MHz / 17   ≈ 9.88 MSPS
};

/* ================================================================ */

void AD9220_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_tim1_up.Instance                 = DMA2_Stream5;
    hdma_tim1_up.Init.Channel             = DMA_CHANNEL_6;
    hdma_tim1_up.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_tim1_up.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_tim1_up.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_tim1_up.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_tim1_up.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_tim1_up.Init.Mode                = DMA_NORMAL;
    hdma_tim1_up.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    hdma_tim1_up.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    hdma_tim1_up.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_HALFFULL;
    hdma_tim1_up.Init.MemBurst            = DMA_MBURST_SINGLE;
    hdma_tim1_up.Init.PeriphBurst         = DMA_PBURST_SINGLE;

    HAL_DMA_Init(&hdma_tim1_up);

    __HAL_LINKDMA(&htim1, hdma[TIM_DMA_ID_UPDATE], hdma_tim1_up);

    HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
}

/* ================================================================ */

void AD9220_Start(AD9220_Tier tier)
{
    uint32_t arr = tier_arr[tier];

    /* Stop timer if running */
    TIM1->CR1  &= ~TIM_CR1_CEN;
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    TIM1->DIER &= ~TIM_DIER_UDE;

    /* Set period */
    TIM1->ARR  = arr;

    /* Generate UEV to load shadow registers (ARR, PSC, etc.), then clear flag */
    TIM1->EGR  = TIM_EGR_UG;
    TIM1->SR   = 0;   /* clear all pending flags */

    /* Configure CH1 as PWM1, 50% duty, no preload */
    TIM1->CCMR1 &= ~(TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE);
    TIM1->CCMR1 |= TIM_OCMODE_PWM1 | TIM_OCFAST_ENABLE;
    TIM1->CCR1  = (arr + 1) / 2;
    TIM1->CCER  |= TIM_CCER_CC1E;

    ad9220_data_ready = 0;
    ad9220_dma_error  = 0;

    /* Clear any pending DMA flags for Stream 5 */
    DMA2->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5
                | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5;

    DMA2_Stream5->PAR  = (uint32_t)&GPIOE->IDR;
    DMA2_Stream5->M0AR = (uint32_t)ad9220_buffer;
    DMA2_Stream5->NDTR = AD9220_BUF_SIZE;

    /* Enable DMA stream with TC + error interrupts.
       CR was already configured (CHSEL=6, DIR=P2M, PSIZE=MSIZE=16bit) by HAL_DMA_Init */
    DMA2_Stream5->CR |= DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_EN;

    /* Enable TIM1 update → DMA request, start counter, enable main output */
    TIM1->DIER |= TIM_DIER_UDE;
    TIM1->CR1  |= TIM_CR1_CEN;
    TIM1->BDTR |= TIM_BDTR_MOE;
}

/* ================================================================ */

void AD9220_Stop(void)
{
    TIM1->DIER &= ~TIM_DIER_UDE;
    TIM1->CR1  &= ~TIM_CR1_CEN;
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    DMA2_Stream5->CR &= ~DMA_SxCR_EN;
}

uint8_t AD9220_DataReady(void)  { return ad9220_data_ready; }
void   AD9220_ClearReady(void)  { ad9220_data_ready = 0; }

/* ================================================================ */

void AD9220_DebugDump(void)
{
    printf("DMA  S5CR=0x%08lX NDTR=%lu PAR=0x%08lX\r\n",
           (unsigned long)DMA2_Stream5->CR,  (unsigned long)DMA2_Stream5->NDTR,
           (unsigned long)DMA2_Stream5->PAR);
    printf("TIM1 CR1=0x%08lX DIER=0x%08lX SR=0x%08lX BDTR=0x%08lX\r\n",
           (unsigned long)TIM1->CR1,  (unsigned long)TIM1->DIER,
           (unsigned long)TIM1->SR,   (unsigned long)TIM1->BDTR);
    printf("DMA  HISR=0x%08lX err=%d ready=%d\r\n",
           (unsigned long)DMA2->HISR, (int)ad9220_dma_error, (int)ad9220_data_ready);
}

/* ================================================================ */

void DMA2_Stream5_IRQHandler(void)
{
    uint32_t isr = DMA2->HISR;

    if (isr & DMA_HISR_TCIF5) {
        /* Clear TC flag */
        DMA2->HIFCR = DMA_HIFCR_CTCIF5;

        /* Disable DMA stream */
        DMA2_Stream5->CR &= ~(DMA_SxCR_EN | DMA_SxCR_TCIE);
        DMA2_Stream5->NDTR = 0;

        /* Stop timer — direct register writes, no HAL in ISR */
        TIM1->DIER &= ~TIM_DIER_UDE;
        TIM1->CR1  &= ~TIM_CR1_CEN;
        TIM1->BDTR &= ~TIM_BDTR_MOE;

        ad9220_data_ready = 1;
    }

    if (isr & DMA_HISR_TEIF5) {
        DMA2->HIFCR = DMA_HIFCR_CTEIF5;
        DMA2_Stream5->CR &= ~(DMA_SxCR_EN | DMA_SxCR_TEIE);
        TIM1->DIER &= ~TIM_DIER_UDE;
        TIM1->CR1  &= ~TIM_CR1_CEN;
        TIM1->BDTR &= ~TIM_BDTR_MOE;
        ad9220_dma_error = 1;
    }
}
