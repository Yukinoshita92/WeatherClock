/**
 * @file    st7789.c
 * @brief   ST7789 SPI 液晶屏驱动（240x320）
 * @details 提供屏幕初始化、区域填色、字符串与图片绘制，并实现 SPI 发送完成回调：
 *            - 绘制流程：先划定显示窗口（0x2A/0x2B），再经 DMA 批量写入像素；
 *            - 数据宽度：写指令用 8bit，写像素用 16bit，由驱动内部自动切换；
 *            - 传输同步：每块传输完成后由信号量同步，不阻塞等待 TC 标志。
 * @note    注意事项：
 *            - 需先在 CubeMX 中配置好 SPI2 与对应 DMA 通道、并使能中断；
 *            - 像素缓冲为静态分配，单个汉字最大支持 76x76 字号；
 *            - 使用了 DMA 与信号量，必须在 FreeRTOS 调度器启动后调用。
 */
#include "st7789.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "gpio.h"
#include "spi.h"

/* 分级日志框架：本模块 TAG=ST7789 */
#define LOG_TAG "ST7789"
#include "log.h"

/* DMA 句柄 */
extern DMA_HandleTypeDef hdma_spi2_tx;

/* 信号量：DMA 传输完成（TC）后由中断释放 */
static SemaphoreHandle_t st7789_DMA_Semaphore_Handle;

static void st7789_lcd_init(void);

/* 封装需要手动调整电平的引脚操作函数 */
static void write_CS(int Bit)/* 片选：选中 st7789，每次传输前拉低，传输结束后拉高 */
{
	HAL_GPIO_WritePin(St7789_CS_GPIO_Port,St7789_CS_Pin,(GPIO_PinState)Bit);
}
static void write_DC(int Bit) /* 指令线：发指令前拉低，发数据前拉高 */
{
	HAL_GPIO_WritePin(St7789_DC_GPIO_Port,St7789_DC_Pin,(GPIO_PinState)Bit);
}
static void write_BL(int Bit)/* 背光：高电平点亮 */
{
	HAL_GPIO_WritePin(St7789_BL_GPIO_Port,St7789_BL_Pin,(GPIO_PinState)Bit);
}
static void write_RST(int Bit)/* 复位线 */
{
	HAL_GPIO_WritePin(St7789_RST_GPIO_Port,St7789_RST_Pin,(GPIO_PinState)Bit);
}
/* 初始化 */
/* st7789 驱动端初始化，顺带完成屏幕参数配置 */
void st7789_init(void)
{
	/* 必须赶在首次 DMA 中断之前创建信号量，二值信号量即可 */
	st7789_DMA_Semaphore_Handle=xSemaphoreCreateBinary();
	configASSERT(st7789_DMA_Semaphore_Handle);   /* 创建失败（NULL）时触发断言 */
	st7789_lcd_init();
}
/* 设置本次传输使用 16 位还是 8 位数据宽度 */
static void st7789_spi_set_16bit(uint8_t en16)
{
    /* 同步 HAL 句柄中的 Init 字段，保持与寄存器一致 */
    uint32_t want = en16 ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
    if (hspi2.Init.DataSize == want)
        return;                       /* 状态未变，直接返回 */

    hspi2.Init.DataSize = want;

    while (SPI_CHECK_FLAG(SPI2->SR, SPI_FLAG_BSY) != RESET);  /* 等最后一 bit 发完 */
    __HAL_SPI_DISABLE(&hspi2);        /* SPE=0，满足“关断时才能写 CR1”的规定 */
    if (en16)
        SPI2->CR1 |=  SPI_CR1_DFF;
    else
        SPI2->CR1 &= ~SPI_CR1_DFF;
    __HAL_SPI_ENABLE(&hspi2);
}
/* 封装“写指令 + 写数据”的发送流程 */
static void st7789_write_register(uint8_t cmd,uint8_t data[],uint16_t num)
{
	/* 写指令固定按 8 bit 传输，先切换数据宽度 */
	st7789_spi_set_16bit(0);

	/* 拉低片选，选中从机 st7789 */
	write_CS(0);
	
	/* 拉低 DC，本次发送的是指令 */
	write_DC(0);
	HAL_SPI_Transmit(&hspi2,&cmd,1,1000);
	while(SPI_CHECK_FLAG(SPI2->SR,SPI_FLAG_TXE)==RESET);   /* 等待 TXE、BUSY 标志位 */
	while(SPI_CHECK_FLAG(SPI2->SR,SPI_FLAG_BSY)!=RESET);

	/* 拉高 DC，以下发送的是数据 */
	write_DC(1);
	for(int i=0;i<num;i++)
	{
		HAL_SPI_Transmit(&hspi2,&data[i],1,1000);
		while(SPI_CHECK_FLAG(SPI2->SR,SPI_FLAG_TXE)==RESET);
	}
	while(SPI_CHECK_FLAG(SPI2->SR,SPI_FLAG_BSY)!=RESET);   /* 注意：BUSY 需要在传输结束后才等待 */
	/* 拉高片选，本次传输结束 */
	write_CS(1);
}

/* 复位时序 */
static void st7789_reset(void)
{
	/* 复位时序参数结合参考代码与手册确定 */
	write_RST(0);
	vTaskDelay(pdMS_TO_TICKS(100));
	write_RST(1);
	vTaskDelay(pdMS_TO_TICKS(120));
}

/* LCD 屏幕参数配置（初始化序列照搬参考代码） */
static void st7789_lcd_init(void)
{
	st7789_reset();

	st7789_write_register(0x3A,(uint8_t[]){0x05},1);		/* 65k mode */
	st7789_write_register(0xC5,(uint8_t[]){0x1A},1);		/* VCOM */
	st7789_write_register(0xC5,(uint8_t[]){0x1A},1);
	st7789_write_register(0x36,(uint8_t[]){0x00},1);	/* 屏幕显示方向设置：正常竖显 */
	st7789_write_register(0xb2,(uint8_t[]){0x05,0x05,0x00,0x33,0x33},5);/* Porch Setting */
	st7789_write_register(0xb7,(uint8_t[]){0x05},1);		/* Gate Control		12.2v   -10.43v */
	st7789_write_register(0xBB,(uint8_t[]){0x3F},1);		/* VCOM */
	st7789_write_register(0xC0,(uint8_t[]){0x2C},1);		/* Power control */
	st7789_write_register(0xC2,(uint8_t[]){0x01},1);        /* VDV and VRH Command Enable */
	st7789_write_register(0xC3,(uint8_t[]){0x0F},1);		/* VRH Set：4.3+( vcom+vcom offset+vdv) */
	st7789_write_register(0xC4,(uint8_t[]){0x20},1);			/* VDV Set：0v */
	st7789_write_register(0xC6,(uint8_t[]){0x01},1);			/* Frame Rate Control in Normal Mode：111Hz */
	st7789_write_register(0xD0,(uint8_t[]){0xA4,0xA1},2);	/* Power Control 1 */
	st7789_write_register(0xE8,(uint8_t[]){0x03},1);				/* Power Control 1 */
	st7789_write_register(0xE9,(uint8_t[]){0x09,0x09,0x08},3);		/* Equalize time control */
	st7789_write_register(0xE0,(uint8_t[]){0xD0,0x05,0x09,0x09,
								0x08,0x14,0x28,0x33,0x3F,0x07,
								0x13,0x14,0x28,0x30},14);	/* Set Gamma */
	st7789_write_register(0xE1,(uint8_t[]){0xD0,0x05,0x09,0x09,
								0x08,0x03,0x24,0x32,0x32,0x3B,
								0x14,0x13,0x28,0x2F},14);		/* Set Gamma */
	st7789_write_register(0x20,NULL,0);/* 关闭显示反转（INVOFF） */
	                                    		
	st7789_write_register(0x11,NULL,0);/* 退出睡眠模式 */
	vTaskDelay(pdMS_TO_TICKS(120));								
	st7789_write_register(0x29,NULL,0);/* 开显示 */
}

/* 划定显示区域：在区域内 SPI 收到的数据自动写入 GRAM，地址自动自增 */
static bool st7789_scan_window(uint16_t x1,uint16_t y1, uint16_t x2,uint16_t y2)
{
	/* 判断地址是否合法 */
    if(x1>LCD_WIDTH-1 || x2>LCD_WIDTH-1)
	{
		LOG_W("over xrange\r\n");   /* 超出 x 地址范围 */
		return false;
	}
    if (y1>LCD_HEIGHT-1 || y2>LCD_HEIGHT-1)
	{
		LOG_W("over yrange\r\n");   /* 超出 y 地址范围 */
		return false;
	}
	if(x1>x2 || y1>y2) 
	{
		LOG_W("range false\r\n");
		return false;
	}
	st7789_write_register(0x2A,(uint8_t[]){((x1>>8)&0xFF),(x1&0xff),((x2>>8)&0xFF),(x2&0xff)},4);/* 窗口地址横向 */
	st7789_write_register(0x2B,(uint8_t[]){((y1>>8)&0xFF),(y1&0xff),((y2>>8)&0xFF),(y2&0xff)},4);/* 窗口地址纵向 */
	st7789_write_register(0x2C,NULL,0);/* 写入数据 */
	return true;	
}	

/* 
	写入 GRAM 的核心函数：用 DMA 代替逐字节 SPI 发送，省去等待 TXE 的时间；
	配合信号量等待 TC 标志位，既避免死等，也符合同步互斥语义。
*/
static void st7789_write_gram(uint8_t data[],uint32_t length,bool multi_color)
{
	/* 颜色/像素数据固定按 16 bit 传输，先切换数据宽度 */
	st7789_spi_set_16bit(1);

	write_CS(0);
	write_DC(1);/* 传输数据 */
	
	length >>=1;   /* 入参为字节长度，每次传输 16bit，故次数减半 */
	
	/* DMA 单次传输上限为 65535 个数据单元，超长则分批 */
	do{
		uint32_t buff_size=length < 65535 ? length : 65535;	
		/* 单一颜色无需自增，每批都读取同一个数据源 */
		/* 仅改句柄再重新初始化并不可靠，仍需直接改寄存器；句柄同步修改以保持一致性 */
		hdma_spi2_tx.Init.MemInc=multi_color?DMA_MINC_ENABLE:DMA_MINC_DISABLE;
		if(multi_color)
			DMA1_Stream4->CR|=DMA_SxCR_MINC; /* 置位 MINC：源地址自增 */
		else
			DMA1_Stream4->CR&=(~DMA_SxCR_MINC);/* 清零 MINC：源地址固定 */
		
		HAL_SPI_Transmit_DMA(&hspi2,data,buff_size);
		/* 阻塞等待信号量，替代死等 TC；TC 标志由中断内部清除 */
		xSemaphoreTake(st7789_DMA_Semaphore_Handle,portMAX_DELAY);
		
		length-=buff_size;
		if(multi_color)
			data+=buff_size*2;   /* 每批传完后手动偏移源地址，仅多色数据需要 */
		
	}while(length>0);
	/* 数据只是写入了 DR，是否真正写入 GRAM 还需等待 BUSY 标志位 */
	while(SPI_CHECK_FLAG(SPI2->SR,SPI_FLAG_BSY)!=RESET);
	write_CS(1);
}  
/* 核心板块1 刷色块：区域颜色全填充 */
bool st7789_print_color(uint16_t x1,uint16_t y1, uint16_t x2,uint16_t y2,uint16_t color)
{
	if(!st7789_scan_window(x1,y1,x2,y2))
	{
		LOG_W("range false\r\n");   /* 超出显示区域 */
		return false;
	}
	uint32_t pixel_num=(y2-y1+1)*(x2-x1+1);   /* 像素数需 +1，例如 (2,2)~(10,10) 为 9*9 */
	
	/* 单次传输 16bit 数据时不需要拆分颜色：直接取 color 的地址，
	由于内存连续，等价于把 color 拆成一个 u8 数组，可直接复用写 GRAM 的函数 */
	st7789_write_gram((uint8_t*)&color,pixel_num*2,false);
	return true;
}

/* 封装字模打印：把字模按像素展开成 RGB565 数据后整体送入 GRAM */
static void st7789_draw_font(uint16_t height,uint16_t width,uint16_t colour,uint16_t bg_colour,const uint8_t* model)
{
	uint16_t bytes_per_row=(width+7)/8;
	/* 最大字号为 76，用一个缓冲区整体存放一次打印的所有像素（每像素 2 字节） */
	static uint8_t buff[76*76*2];/* 配合 DMA 整体传输，避免逐次发送；用 p 指针计算偏移，同时避免传输空白区域 */
	
	uint8_t *pbuff=buff;/* 1.修改 buff 的内容（对应位置颜色值） 2.以 buff 首地址为基准记录数据长度 */
	
	for(uint8_t row=0;row<height;row++)
	{
		const uint8_t *roll_data=model+row*bytes_per_row;
		for(uint8_t col=0;col<width;col++)
		{
			uint8_t pixel=roll_data[col/8]&(0x01<<(7-col%8));
			uint16_t pixel_color=pixel?colour:bg_colour;
			*pbuff++=(pixel_color)&0xff;/* 低位颜色数据 */
			*pbuff++=(pixel_color>>8)&0xff;/* 高位颜色数据 */
		}
	}
	uint32_t length=pbuff-buff;/* 偏移量即数据长度；缓冲区元素是字节而非像素，无需 *2 */
	st7789_write_gram(buff,length,true);
}	

/* 取出字符所在字模数组的首地址 */
static const uint8_t* st7789_get_ch_map(char ch,const font_t *font)
{
	/* 英文 ASCII：宽为高的一半 */
	uint16_t fheight=font->fSize,fwidth=font->fSize/2;
	uint16_t bytes_per_row=(fwidth+7)/8;
	uint16_t bytes_per_char=fheight*bytes_per_row;
	/* 若该字体的 ASCII 字模是精简版（定义了 ascii_map） */
	if(font->ascii_map)
	{
		const char *map=font->ascii_map;
		do
		{
			if(*map==ch)
			{
				return font->model+(map-font->ascii_map)*bytes_per_char;
			}
		}while(*(++map)!='\0');
		return NULL;/* 匹配失败 */
	}else/* 未精简：按完整字模输出，需减去空格偏移 */
	{
		return font->model+(ch-' ')*bytes_per_char;
	}
}
/* 核心板块2 字符显示：按字模字节值选色，在字符区域内打印 */
static bool st7789_write_ascii(uint16_t x,uint16_t y,char ch,uint16_t colour,uint16_t bg_colour,const font_t *font)
{
	/* 判断是否在显示区域之内 */
	uint16_t fheight=font->fSize,fwidth=font->fSize/2;
	if(!st7789_scan_window(x,y,x+fwidth-1,y+fheight-1)){return false;}
	if (ch == NULL || font == NULL) return false;
	
	const uint8_t *model=st7789_get_ch_map(ch,font);
	
	if(model)
		st7789_draw_font(fheight,fwidth,colour,bg_colour,model);
	
	return true;
}
/* 核心板块3 中文显示：按字模字节值选色，在汉字区域内打印 */
static bool st7789_write_Chinese(uint16_t x,uint16_t y,char *ch/* 待显示汉字的字符串首地址 */,uint16_t colour,uint16_t bg_colour,const font_t *font)
{
	/* 判断是否在显示区域之内 */
	uint16_t fheight=font->fSize,fwidth=font->fSize;
	if(!st7789_scan_window(x,y,x+fwidth-1,y+fheight-1)){return false;}
	if (ch == NULL || font == NULL) return false;
	
	const Chinese_font *C=font->Chinese;/* 指向中文字模表，利用该指针访问字模 */
	for(;C->name!=NULL;C++)
	{
		if(strcmp(C->name,ch)==0){break;}/* 名称与输入字符串一致，即得到对应字模指针 */
	}
	if(C->Cmodel)
		st7789_draw_font(fheight,fwidth,colour,bg_colour,C->Cmodel);
	return true;
}
/* 判断是否为 GB2312 编码字符（占两个字节） */
static bool is_gb2312(char ch)
{
	return ((uint8_t)ch>=0xA1 && (uint8_t)ch<=0xF7);/* GB2312 字符范围 */
}
/* 输出字符串：逐字符判断是英文还是中文 */
bool st7789_write_string(uint16_t x,uint16_t y,const char *str,uint16_t colour,uint16_t bg_colour,const font_t *font)
{
	/* 输入校验 */
	if (str == NULL || font == NULL) return false;
	while(*str!='\0')
	{
		int len=is_gb2312(*str)?2:1;/* 非 GB2312（中文）即普通 ASCII */
		/* 取当前字符：中文占 2 字节、ASCII 占 1 字节 */
		if(len==0){str++;continue;}
		/* 为 0 就跳过并继续遍历，判断下一个字符 */
		else if(len==1)
		{
			if(!st7789_write_ascii(x,y,*str,colour,bg_colour,font)){return false;}
			x+=font->fSize/2;
			str+=len;
		}
		else
		{
			char ch[5];/* 存放单个汉字，GB2312 最长 2 字节，此处留足余量 */
			strncpy(ch,str,len);/* 固定长度复制：只要编码类型匹配，取出的就是一个完整汉字 */
			
			if(!st7789_write_Chinese(x,y,ch,colour,bg_colour,font)){return false;}
			x+=font->fSize;
			str+=len;			
		}
	}
	return true;
}

/* 核心板块4：显示图片，直接按图片字模数组输出 */
bool st7789_print_image(uint16_t x,uint16_t y,const image_t *image)
{
	/* 判断是否在显示区域之内 */
	uint16_t Iheight=image->height,Iwidth=image->width;
	if(!st7789_scan_window(x,y,x+Iwidth-1,y+Iheight-1)){return false;}
	if (image==NULL) return false;
	st7789_write_gram((uint8_t*)image->model,Iheight*Iwidth*2,true);
	return true;
}
/* 
   DMA TC 中断：检测到 TC 标志位后触发中断，释放信号量。
*/
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
	if(hspi->Instance==SPI2)
	{
		BaseType_t HPTask_Woken;
		/* 释放信号量 */
		xSemaphoreGiveFromISR(st7789_DMA_Semaphore_Handle,&HPTask_Woken);

		portYIELD_FROM_ISR(HPTask_Woken);
	}
}
