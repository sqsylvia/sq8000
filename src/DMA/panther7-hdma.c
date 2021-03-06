#include <type.h>
#include <io.h>
#include <genlib.h>
#include <dma/dma.h>
#include "panther7-hdma-regs.h"
#include "dependency.h"

/*
 *  Read register
 *  */
static inline u32
socle_reg_read(u32 reg, u32 base)
{
	return ioread32(base+reg);
}

/*
 *  Write register
 *  */
static inline void
socle_reg_write(u32 reg, u32 value, u32 base)
{
	iowrite32(value, base+reg);
}

static int panther7_hdma_request(u32 ch, struct socle_dma *dma);
static void panther7_hdma_free(u32 ch, struct socle_dma *dma);
static void panther7_hdma_enable(u32 ch, struct socle_dma *dma);
static void panther7_hdma_disable(u32 ch, struct socle_dma *dma);
static void panther7_hdma_set_page_number(u32 ch, struct socle_dma *dma, u32 num);
static void panther7_hdma_isr(void *dma);
static void panther7_hdma_isr_0(void *dma);
static void panther7_hdma_isr_1(void *dma);
static void panther7_hdma_set_count(struct socle_dma *dma);
static void panther7_hdma_request_irq(u32 irq, void (*isr)(void *data), void *data);
static void panther7_hdma_free_irq(u32 irq, void *data);

static u32 panther7_hdma_channel[] = {0, 1};

struct panther7_hdma_shared_irq {
	struct socle_dma *dma;
	void (*isr)(void *data);
};

struct panther7_hdma_shared_irq panther7_hdma_shared_irq_container[2];

struct socle_dma_ops panther7_hdma_ops = {
	.request = panther7_hdma_request,
	.free = panther7_hdma_free,
	.enable = panther7_hdma_enable,
	.disable = panther7_hdma_disable,
	.set_page_number = panther7_hdma_set_page_number,
};

struct socle_dma panther7_hdma_channel_0 = {
	.dma_name = "PANTHER7 HDMA Channel 0",
	.base_addr = PANTHER7_AHB_0_HDMA_0,
	.irq = PANTHER7_INTC_HDMA_0,
	.private_data = &panther7_hdma_channel[0],
	.ops = &panther7_hdma_ops,
};

struct socle_dma panther7_hdma_channel_1 = {
	.dma_name = "PANTHER7 HDMA Channel 1",
	.base_addr = PANTHER7_AHB_0_HDMA_0,
	.irq = PANTHER7_INTC_HDMA_0,
	.private_data = &panther7_hdma_channel[1],
	.ops = &panther7_hdma_ops,
};

static int
panther7_hdma_request(u32 ch, struct socle_dma *dma)
{
	int ret = 0;
	u32 inter_ch = *((u32 *)dma->private_data);

#if 1
	if (0 == inter_ch) 
		panther7_hdma_request_irq(dma->irq, panther7_hdma_isr_0, dma);
	else if (1 == inter_ch)
		panther7_hdma_request_irq(dma->irq, panther7_hdma_isr_1, dma);
	return ret;
#else
	if (0 == inter_ch) 
		request_irq(dma->irq, panther7_hdma_isr_0, dma);
	else if (1 == inter_ch)
		request_irq(dma->irq, panther7_hdma_isr_1, dma);
	if (ret)
		printf("%s: failed to request interrupt\n", dma->dma_name);
	return ret;
#endif
}

static void 
panther7_hdma_free(u32 ch, struct socle_dma *dma)
{
	panther7_hdma_free_irq(dma->irq, dma);
}

static void 
panther7_hdma_enable(u32 ch, struct socle_dma *dma)
{
	u32 inter_ch = *((u32 *)dma->private_data);
	u32 tmp, data_size;
	u32 conf = PANTHER7_HDMA_SLICE_MODE_DIS |
		PANTHER7_HDMA_CH_EN |
		PANTHER7_HDMA_INT_MODE_INTERRUPT |
		PANTHER7_HDMA_FLY_DIS |
		PANTHER7_HDMA_BURST_SINGLE |
		PANTHER7_HDMA_EXT_HDREQ_SEL(0) |
		PANTHER7_HDMA_DIR_SRC_INC |
		PANTHER7_HDMA_DIR_DST_INC |
		PANTHER7_HDMA_DATA_BYTE |
		PANTHER7_HDMA_SWDMA_OP_NO |
		PANTHER7_HDMA_HWDMA_TRIGGER_DIS;

	if (SOCLE_DMA_MODE_SLICE == dma->mode)
		conf |= PANTHER7_HDMA_SLICE_MODE_EN;
	switch (dma->burst_type) {
	case SOCLE_DMA_BURST_SINGLE:
		conf |= PANTHER7_HDMA_BURST_SINGLE;
		break;
	case SOCLE_DMA_BURST_INCR4:
		conf |= PANTHER7_HDMA_BURST_INCR4;
		break;
	case SOCLE_DMA_BURST_INCR8:
		conf |= PANTHER7_HDMA_BURST_INCR8;
		break;
	case SOCLE_DMA_BURST_INCR16:
		conf |= PANTHER7_HDMA_BURST_INCR16;
		break;
	}
	conf |= PANTHER7_HDMA_EXT_HDREQ_SEL(dma->ext_hdreq);
	if (SOCLE_DMA_DIR_FIXED == dma->src_dir)
		conf |= PANTHER7_HDMA_DIR_SRC_FIXED;
	if (SOCLE_DMA_DIR_FIXED == dma->dst_dir)
		conf |= PANTHER7_HDMA_DIR_DST_FIXED;
	switch (dma->data_size) {
	case SOCLE_DMA_DATA_BYTE:
		data_size = 1;
		conf |= PANTHER7_HDMA_DATA_BYTE;
		break;
	case SOCLE_DMA_DATA_HALFWORD:
		data_size = 2;
		conf |= PANTHER7_HDMA_DATA_HALFWORD;
		break;
	case SOCLE_DMA_DATA_WORD:
		data_size = 4;
		conf |= PANTHER7_HDMA_DATA_WORD;
		break;
	}
	if ((SOCLE_DMA_MODE_SLICE == dma->mode) ||
	    (SOCLE_DMA_MODE_HW == dma->mode))
		conf |= PANTHER7_HDMA_HWDMA_TRIGGER_EN;
	else
		conf |= PANTHER7_HDMA_SWDMA_OP_START;
	switch (inter_ch) {
	case 0:
		/* Unmask channel 0 all interrupt */
		tmp = PANTHER7_HDMA_CH0_INT_MASK | PANTHER7_HDMA_CH0_PAGE_INT_MASK | PANTHER7_HDMA_CH0_PAGE_ACCUM_OVF_INT_MASK;
		socle_reg_write(PANTHER7_HDMA_ISR,
				socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr)&(~tmp),
				dma->base_addr);
    
		socle_reg_write(PANTHER7_HDMA_ISRC0, dma->src_addr , dma->base_addr);
		socle_reg_write(PANTHER7_HDMA_IDST0, dma->dst_addr , dma->base_addr);
		panther7_hdma_set_count(dma);
		if (SOCLE_DMA_MODE_SLICE == dma->mode) {
			socle_reg_write(PANTHER7_HDMA_ISCNT0, dma->slice_cnt-1, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_IADDR_BS0, dma->buf_size-data_size, dma->base_addr);
		}
		socle_reg_write(PANTHER7_HDMA_CON0, conf, dma->base_addr);
		break;
	case 1:
		/* Unmask channel  all interrupt */
		tmp = PANTHER7_HDMA_CH1_INT_MASK | PANTHER7_HDMA_CH1_PAGE_INT_MASK | PANTHER7_HDMA_CH1_PAGE_ACCUM_OVF_INT_MASK;
		socle_reg_write(PANTHER7_HDMA_ISR,
				socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr)&(~tmp),
				dma->base_addr);

		socle_reg_write(PANTHER7_HDMA_ISRC1, dma->src_addr , dma->base_addr);
		socle_reg_write(PANTHER7_HDMA_IDST1, dma->dst_addr , dma->base_addr);
		panther7_hdma_set_count(dma);
		if (SOCLE_DMA_MODE_SLICE == dma->mode) {
			socle_reg_write(PANTHER7_HDMA_ISCNT1, dma->slice_cnt-1, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_IADDR_BS1, dma->buf_size-data_size, dma->base_addr);
		}
		socle_reg_write(PANTHER7_HDMA_CON1, conf, dma->base_addr);
		break;
	default:
		printf("%s: unknown channel number %d\n", dma->dma_name, ch);
	}
}

static void 
panther7_hdma_disable(u32 ch, struct socle_dma *dma)
{
	u32 inter_ch = *((u32 *)dma->private_data);
	u32 tmp;

	switch (inter_ch) {
	case 0:
		/* Clear channel 0 configuration register */
		socle_reg_write(PANTHER7_HDMA_CON0, PANTHER7_HDMA_CPNCNTD_CLR|0x00000000, dma->base_addr);

		/* Clear channel 0 interrupt flag */
		tmp = PANTHER7_HDMA_CH0_INT_ACT | PANTHER7_HDMA_CH0_PAGE_INT_ACT | PANTHER7_HDMA_CH0_PAGE_ACCUM_OVF_INT_ACT;
		socle_reg_write(PANTHER7_HDMA_ISR,
				socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr)&(~tmp),
				dma->base_addr);

		/* Mask channel 0 all interrupt */
		socle_reg_write(PANTHER7_HDMA_ISR,
				socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr) | 
				PANTHER7_HDMA_CH0_INT_MASK |
				PANTHER7_HDMA_CH0_PAGE_INT_MASK |
				PANTHER7_HDMA_CH0_PAGE_ACCUM_OVF_INT_MASK,
				dma->base_addr);

		socle_reg_write(PANTHER7_HDMA_ISRC0, 0, dma->base_addr);
		socle_reg_write(PANTHER7_HDMA_IDST0, 0, dma->base_addr);
		socle_reg_write(PANTHER7_HDMA_ICNT0, 0, dma->base_addr);
		if (SOCLE_DMA_MODE_SLICE == dma->mode) {
			socle_reg_write(PANTHER7_HDMA_ISCNT0, 0, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_IPNCNT0, 0, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_IADDR_BS0, 0, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_PACNT0, 0, dma->base_addr);
		}
		break;
	case 1:
		/* Clear channel 1configuration register */
		socle_reg_write(PANTHER7_HDMA_CON1, PANTHER7_HDMA_CPNCNTD_CLR|0x00000000, dma->base_addr);


		/* Clear channel 1 interrupt flag */
		tmp = PANTHER7_HDMA_CH1_INT_ACT | PANTHER7_HDMA_CH1_PAGE_INT_ACT | PANTHER7_HDMA_CH1_PAGE_ACCUM_OVF_INT_ACT;
		socle_reg_write(PANTHER7_HDMA_ISR,
				socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr)&(~tmp),
				dma->base_addr);

		/* Mask channel 1 all interrupt */
		socle_reg_write(PANTHER7_HDMA_ISR,
				socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr) | 
				PANTHER7_HDMA_CH1_INT_MASK |
				PANTHER7_HDMA_CH1_PAGE_INT_MASK |
				PANTHER7_HDMA_CH1_PAGE_ACCUM_OVF_INT_MASK,
				dma->base_addr);

		socle_reg_write(PANTHER7_HDMA_ISRC1, 0, dma->base_addr);
		socle_reg_write(PANTHER7_HDMA_IDST1, 0, dma->base_addr);
		socle_reg_write(PANTHER7_HDMA_ICNT1, 0, dma->base_addr);
		if (SOCLE_DMA_MODE_SLICE == dma->mode) {
			socle_reg_write(PANTHER7_HDMA_ISCNT1, 0, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_IPNCNT1, 0, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_IADDR_BS1, 0, dma->base_addr);
			socle_reg_write(PANTHER7_HDMA_PACNT1, 0, dma->base_addr);
		}
		break;
	default:
		printf("%s: unknown channel number %d\n", dma->dma_name, ch);
		return;
	}
}

static void 
panther7_hdma_set_page_number(u32 ch, struct socle_dma *dma, u32 num)
{
	u32 inter_ch = *((u32 *)dma->private_data);
	
	switch (inter_ch) {
	case 0:
		socle_reg_write(PANTHER7_HDMA_IPNCNT0, num, dma->base_addr);
		break;
	case 1:
		socle_reg_write(PANTHER7_HDMA_IPNCNT1, num, dma->base_addr);
		break;
	default:
		printf("%s: unknown channel number %d\n", dma->dma_name, ch);
	}
}

static void
panther7_hdma_isr_0(void *_dma)
{
	u32 int_stat;
	struct socle_dma *dma = _dma;

	int_stat = socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr);
	int_stat &= (PANTHER7_HDMA_CH0_INT_ACT | 
		     PANTHER7_HDMA_CH0_PAGE_INT_ACT |
		     PANTHER7_HDMA_CH0_PAGE_ACCUM_OVF_INT_ACT);

	/* Clear the interrupt flag */
	socle_reg_write(PANTHER7_HDMA_ISR,
			socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr) & (~int_stat),
			dma->base_addr);

	if (int_stat & PANTHER7_HDMA_CH0_INT_ACT) {
		if (dma->notifier->complete)
			dma->notifier->complete(dma->notifier->data);
	}
	if (int_stat & PANTHER7_HDMA_CH0_PAGE_INT_ACT) {
		if (dma->notifier->page_interrupt)
			dma->notifier->page_interrupt(dma->notifier->data);
	}
	if (int_stat & PANTHER7_HDMA_CH0_PAGE_ACCUM_OVF_INT_ACT) {
		if (dma->notifier->page_accumulation_overflow)
			dma->notifier->page_accumulation_overflow(dma->notifier->data);
	}
}

static void
panther7_hdma_isr_1(void *_dma)
{
	u32 int_stat;
	struct socle_dma *dma = _dma;

	int_stat = socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr);
	int_stat &= (PANTHER7_HDMA_CH1_INT_ACT | 
		     PANTHER7_HDMA_CH1_PAGE_INT_ACT |
		     PANTHER7_HDMA_CH1_PAGE_ACCUM_OVF_INT_ACT);

	/* Clear the interrupt flag */
	socle_reg_write(PANTHER7_HDMA_ISR,
			socle_reg_read(PANTHER7_HDMA_ISR, dma->base_addr) & (~int_stat),
			dma->base_addr);

	if (int_stat & PANTHER7_HDMA_CH1_INT_ACT) {
		if (dma->notifier->complete)
			dma->notifier->complete(dma->notifier->data);
	}
	if (int_stat & PANTHER7_HDMA_CH1_PAGE_INT_ACT) {
		if (dma->notifier->page_interrupt)
			dma->notifier->page_interrupt(dma->notifier->data);
	}
	if (int_stat & PANTHER7_HDMA_CH1_PAGE_ACCUM_OVF_INT_ACT) {
		if (dma->notifier->page_accumulation_overflow)
			dma->notifier->page_accumulation_overflow(dma->notifier->data);
	}
}

static void 
panther7_hdma_set_count(struct socle_dma *dma)
{
	int burst_val;
	int data_size_val;
	u32 inter_ch = *((u32 *)dma->private_data);

	switch(dma->burst_type) {
	case SOCLE_DMA_BURST_SINGLE:
		burst_val = 1;
		break;
	case SOCLE_DMA_BURST_INCR4:
		burst_val = 4;
		break;
	case SOCLE_DMA_BURST_INCR8:
		burst_val = 8;
		break;
	case SOCLE_DMA_BURST_INCR16:
		burst_val = 16;
		break;
	default:
		printf("%s: burst type (%d) is unknown\n", dma->dma_name, dma->burst_type);
		return;
	}
	switch (dma->data_size) {
	case SOCLE_DMA_DATA_BYTE:
		data_size_val = 1;
		break;
	case SOCLE_DMA_DATA_HALFWORD:
		data_size_val = 2;
		break;
	case SOCLE_DMA_DATA_WORD:
		data_size_val = 4;
		break;
	}
	if (0 == (dma->tx_cnt % (burst_val * data_size_val))) {
		if (0 == inter_ch) 
			socle_reg_write(PANTHER7_HDMA_ICNT0, (dma->tx_cnt/data_size_val)-1, dma->base_addr);
		else
			socle_reg_write(PANTHER7_HDMA_ICNT1, (dma->tx_cnt/data_size_val)-1, dma->base_addr);
	} else
		printf("%s: %d is not a multiple of %d (%d * %d)\n", dma->dma_name, dma->tx_cnt, (burst_val*data_size_val), burst_val,
		       data_size_val);
}

static void
panther7_hdma_request_irq(u32 irq, void (*isr)(void *data), void *data)
{
	struct socle_dma *dma = (struct socle_dma *)data;
	u32 inter_ch = *((u32 *)dma->private_data);

	panther7_hdma_shared_irq_container[inter_ch].dma = dma;
	panther7_hdma_shared_irq_container[inter_ch].isr = isr;
	request_irq(irq, panther7_hdma_isr, dma);
	
}

static void
panther7_hdma_free_irq(u32 irq, void *data)
{
	struct socle_dma *dma = (struct socle_dma *)data;
	u32 inter_ch = *((u32 *)dma->private_data);

	panther7_hdma_shared_irq_container[inter_ch].dma = NULL;
	panther7_hdma_shared_irq_container[inter_ch].isr = NULL;
	free_irq(dma->irq);

}

static void 
panther7_hdma_isr(void *data)
{
	struct socle_dma *dma;

	dma = panther7_hdma_shared_irq_container[0].dma;
	if (dma != NULL)
		panther7_hdma_shared_irq_container[0].isr(dma);
	dma = panther7_hdma_shared_irq_container[1].dma;
	if (dma != NULL)
		panther7_hdma_shared_irq_container[1].isr(dma);
}
