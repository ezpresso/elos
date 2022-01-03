#ifndef DRIVERS_DISK_ATA_H
#define DRIVERS_DISK_ATA_H

#include <kern/wait.h>
#include <device/dma.h>

#define ATA_PRDT_MEM PAGE_SZ
#define ATA_PRDT_NUM (ATA_PRDT_MEM / sizeof(ata_prdt_t))

struct ata_chan;

#define ATA_SECT_LG	9
/* logical sector size (might not be the physical sector size )*/
#define ATA_SECT_SZ	(1 << ATA_SECT_LG)

/* default I/O ranges */
#define ATA_IO_PRIM	0x1f0
#define ATA_IO_SEC	0x170
#define ATA_IO_SZ	0x08
#define ATA_CTL_OFF	0x206
#define ATA_CTL_SZ	0x01

/* I/O ports */
#define ATA_DATA	0x00 /* rw, 16 bit long */
#define ATA_FEAT	0x01 /* w, feature register */
#define ATA_ERR		0x01 /* r, error register */
#define		ATA_E_AMNF	(1 << 0) /* data address mark has not been found */
#define		ATA_E_TK0NF	(1 << 1) /* track 0 has not been found */
#define		ATA_E_ABRT	(1 << 2) /* command aborted */
#define		ATA_E_MCR	(1 << 3) /* media change request */
#define		ATA_E_IDNF	(1 << 4) /* ID not found */
#define		ATA_E_MC	(1 << 5) /* media changed */
#define		ATA_E_UNC	(1 << 6) /* uncorrectable data error */
#define		ATA_E_BBK	(1 << 7) /* bad block mark was detected in the ID field */
#define ATA_COUNT	0x02 /* rw, sector count */
#define ATA_SECT	0x03 /* rw, sector number */
#define ATA_CYL_LO	0x04 /* rw, cylinder low */
#define ATA_CYL_HI	0x05 /* rw, cylinder high */
#define ATA_DRIVE	0x06 /* rw, drive/head */
#define		ATA_D_SLAVE	(1 << 4)
#define		ATA_D_LBA	(1 << 6)
#define		ATA_D_IBM	0xA0 /* specs say these bits are 1, bsd calls this IBM */
#define ATA_CMD		0x07 /* w, command */
#define ATA_STA		0x07 /* r, status */
#define 	ATA_S_ERR	(1 << 0) /* if set, ATA_ERR has content */
#define		ATA_S_IDX	(1 << 1) /* toggled from 0 to 1 to 0, once per disc revolution */
#define		ATA_S_CORR	(1 << 2) /* corrected data */
#define		ATA_S_DRQ	(1 << 3) /* data request bit */
#define		ATA_S_DSC	(1 << 4) /* drive seek complete */
#define		ATA_S_DWF	(1 << 5) /* drive write fault */
#define		ATA_S_DRDY	(1 << 6) /* device ready */
#define		ATA_S_BSY	(1 << 7) /* busy */

#define ATA_ALTSTA	0x0 /* r, kinda sae as ATA_STA, but with some different behaviour */
#define ATA_CTL 	0x0 /* w */
#define 	ATA_C_NIEN	(1 << 1)
#define 	ATA_C_SRST	(1 << 2)
#define		ATA_C_HOB	(1 << 7)	

/* Commands */
#define	ATA_CMD_RD	0x20
#define ATA_CMD_RD48	0x24
#define ATA_CMD_DMARD	0xC8
#define ATA_CMD_DMARD48 0x25
#define	ATA_CMD_WR	0x30
#define	ATA_CMD_WR48	0x34
#define	ATA_CMD_DMAWR	0xCA
#define	ATA_CMD_DMAWR48	0x35
#define	ATA_CMD_PKT	0xA0 /* ATAPI packet */
#define ATA_CMD_ID	0xEC /* ATA identify */
#define ATAPI_CMD_ID	0xA1 /* ATAPI identify */
#define	ATA_CMD_RDMUL	0xC4 /* read multiple sectors */
#define	ATA_CMD_WRMUL	0xC5 /* write multiple sectors */

/* Identify command */
#define ATA_ID_NCYL	1
#define ATA_ID_NHEAD	3
#define ATA_ID_NSECT	6
#define ATA_ID_SERIAL	10
#define 	ATA_SERIAL_LEN	10 /* 10 words */
#define ATA_ID_MODEL	27
#define 	ATA_MODEL_LEN	20 /* 20 words */
#define ATA1_DWORDIO	48 /* ATA1 only, I think */
#define ATA_ID_CAPS	49
#define 	ATA_CAPS_DMA	(1 << 8)
#define 	ATA_CAPS_LBA 	(1 << 9)
#define ATA_ID_SECT28	60 /* 2 words */
#define ATA_ID_SUPCF	83 /* Supported Commands and Feature set */
#define		ATA_SUPCF_DWNM	(1 << 0) /* DOWNLOAD MICROCODE command supported */
#define 	ATA_SUPCF_TCQ	(1 << 1) /* TCQ feature set supported */
#define 	ATA_SUPCF_CFA	(1 << 2) /* CFA feature set supported */
#define 	ATA_SUPCF_APM	(1 << 3) /* APM feature set supported */
#define 	ATA_SUPCF_PUIS	(1 << 5) /* PUIS feature set supported */
#define 	ATA_SUPCF_SF	(1 << 6) /* SET FEATURES subcommand required to spin-up after power-up */
#define 	ATA_SUPCF_MAX	(1 << 8) /* SET MAX security extension supported */
#define 	ATA_SUPCF_AAM	(1 << 9) /* AAM feature set supported */
#define 	ATA_SUPCF_LBA48	(1 << 10) /* 48-bit Address feature set supported */
#define 	ATA_SUPCF_DCO	(1 << 11) /* DCO feature set supported */
#define 	ATA_SUPCF_FC 	(1 << 12) /* mandatory FLUSH CACHE command supported */
#define 	ATA_SUPCF_FCE	(1 << 13) /* FLUSH CACHE EXT command supported */
#define ATA_ID_SUPCF2	84
#define ATA_ID_SUPCF3	85
#define ATA_ID_SUPCF4	86
#define ATA_ID_SUPCF5	87
#define ATA_ID_SECT48	100 /* 4 words */
#define ATA_ID_SECTSZ	106 /* bits 0:3 log2 of logical sectors per physical sector */

typedef struct ata_prdt {
	uint32_t addr;
	uint16_t count;
#define ATA_PRDT_EOT	(1 << 15)
	uint16_t flags;
} __packed ata_prdt_t;

#define ATA_CNTLR_DMA	(1 << 0)

typedef struct ata_cntlr {
	int flags;
	void *priv;

	void (*dma_prep)  (struct ata_chan *);
	void (*dma_start) (struct ata_chan *);
	void (*dma_done)  (struct ata_chan *);
} ata_cntlr_t;

#define ATA_DEV_ATAPI	(1 << 0)
#define ATA_DEV_LBA48	(1 << 1) /* dev supports 48 bit lba */
#define ATA_DEV_DMA	(1 << 2) /* dev supports dma */

typedef struct ata_dev {
	struct ata_chan *chan;
	int flags;
	uint8_t drive; /* 0  or ATA_D_SLAVE */
	struct blk_dev *blkdev;

	uint32_t io_max_sect;
} ata_dev_t;

typedef struct ata_chan {
	ata_cntlr_t *cntlr;
	size_t idx;

	struct blk_req_queue *req_queue;
	struct blk_req *request;

	bus_res_t io;
	bus_res_t cntl;
	bus_res_t intr;

	bus_dma_buf_t dma;
	bus_dma_buf_t prdt_dma;
	ata_prdt_t *prdt; /* NULL if dma is not supported */
	bus_addr_t prdt_addr;

	waitqueue_t intrwq;
	uint8_t intr_sta; /* Status read in intr handler */

	ata_dev_t master;
	ata_dev_t slave;

	struct {
		uint8_t dma :1;

#define ATA_DMA_TO_DEV	(0)
#define ATA_DMA_TO_CPU	(1)
		uint8_t dma_dir :1;
		uint8_t dma_err :1;
		uint8_t success :1;

		uint8_t cmd;
		uint32_t *in_buf;
		size_t in_sz;
		uint32_t *out_buf;
		size_t out_sz;
	} tf;
} ata_chan_t;

#endif