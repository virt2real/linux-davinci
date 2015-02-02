#define TIMER_FREQ_HZ 	24000000
#define TIMER_FREQ_kHz	24000
#define TIMER_FREQ_MHz	24

//Definition of timer 3 structure pointer
#define TIMER_PTR ((volatile TIMER_REGS*)(TMR3_BASE))

#define REL12_OFFSET 0x34

/*Definition of timer 3 register values
For example we can access to to TIM12 like TIM12 |= DESIRED_BIT;*/
#define PID12  (*((volatile unsigned long*)(TMR3_BASE+0x00)))
#define TIM12  (*((volatile unsigned long*)(TMR3_BASE+0x10)))
#define PRD12  (*((volatile unsigned long*)(TMR3_BASE+0x18)))
#define TCR    (*((volatile unsigned long*)(TMR3_BASE+0x20)))
#define TGCR   (*((volatile unsigned long*)(TMR3_BASE+0x24)))
#define REL12  (*((volatile unsigned long*)(TMR3_BASE+0x34)))
#define CAP12  (*((volatile unsigned long*)(TMR3_BASE+0x3C)))
#define INTCTL (*((volatile unsigned long*)(TMR3_BASE+0x44)))


//Timer control register bits
#define CAPEVTMODE34MASK  (3<<28)
#define CAPMODE34         (1<<27)
#define READRSTMODE34     (1<<26)
#define CLKSRC34          (1<<24)
#define ENAMODE34OFFSET   (3<<22)
#define MD34_DISABLED     (0)
#define MD34_ONCE         (1<<22)
#define MD34_CONT         (1<<23)
#define MD34_CONT_RELOAD  (3<<22)
#define CAPEVTMODE12MASK  (3<<12)
#define CAPMODE12         (1<<11)
#define READRSTMODE12     (1<<10)
#define CLKSRC12          (1<<8)
#define ENAMODE12OFFSET   (3<<6)
#define MD12_DISABLED     (0)
#define MD12_ONCE         (1<<6)
#define MD12_CONT         (1<<7)
#define MD12_CONT_RELOAD  (3<<6)

//Timer global control register bits
#define TDDR34MASK            (0x0f<<12)
#define PSC34MASK             (0x0f<<8)
#define BW_COMPATIBLE         (1<<4)
#define TIMMODEMASK           (3<<2)
#define TIMMOD_DUAL_UNCHAINED (4)
#define TIM34RS               (2)
#define TIM12RS               (1)

//Timer interrupt and status control register bits
#define SET34                 (1<<31)
#define EVAL34                (1<<30)
#define EVT_INT_STAT34        (1<<19)
#define EVT_INT_EN34          (1<<18)
#define CMP_INT_STAT34        (1<<17)
#define CMP_INT_EN34          (1<<16)
#define SET12                 (1<<15)
#define EVAL12                (1<<14)
#define EVT_INT_STAT12        (1<<3)
#define EVT_INT_EN12          (1<<2)
#define CMP_INT_STAT12        (2)
#define CMP_INT_EN12          (1)


static unsigned long convert_ns_to_timer (unsigned long ns) {
	return TIMER_FREQ_MHz * ns / 1000;
}

static unsigned long convert_ms_to_timer (unsigned long ms) {
	return (unsigned long) TIMER_FREQ_MHz * ms;;
}

static unsigned long convert_timer_to_ns (unsigned long ticks) {
	return ticks * 1000 / TIMER_FREQ_kHz;
}

static unsigned long convert_timer_to_ms (unsigned long ticks) {
	return ticks / TIMER_FREQ_MHz;
}

typedef struct {
	volatile unsigned long pid12;		//offset 0x00
	volatile unsigned long emumgt;		// offset 0x04
	volatile unsigned long res1;		// offset 0x08
	volatile unsigned long res2;		// offset 0x0c
	volatile unsigned long tim12;		//offset 0x10
	volatile unsigned long tim34;		// offset 0x14
	volatile unsigned long prd12;		// offset 0x18
	volatile unsigned long prd34;		// offset 0x1c
	volatile unsigned long tcr;			//offset 0x20
	volatile unsigned long tgcr;		// offset 0x24
	volatile unsigned long wdtcr;		// offset 0x28
	volatile unsigned long res3;		// offset 0x2c
	volatile unsigned long res4;		//offset 0x30
	volatile unsigned long rel12;		// offset 0x34
	volatile unsigned long rel34;		// offset 0x38
	volatile unsigned long cap12;		// offset 0x3c
	volatile unsigned long cap34;		//offset 0x40
	volatile unsigned long intctl_stat;	// offset 0x44
} TIMER_REGS;

typedef struct {
	unsigned long reg;
	unsigned long val;
	unsigned char set;
} REG_VAL;
