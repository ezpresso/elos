#ifndef DISK_SCSI_H
#define DISK_SCSI_H

/* Control byte */
#define SCSI_CTRL_LINK	(1 << 0)
#define SCSI_CTRL_NACA	(1 << 2)

/* Sense data */
#define SCSI_RESP_CE	0x72 /* current errors */
#define SCSI_RESP_DE	0x73 /* deferred errors */
/* TODO */

/* Sense data descriptor */
/* TODO */

/* SCSI Commands */
#define SCSI_CMD_CHGDEF		0x40 /* CHANGE DEFINITION command, obsolete */
#define SCSI_CMD_FORMAT		0x04 /* FORMAT UNIT command */
#define SCSI_CMD_INQUIRY	0x12 /* INQUIRY command */
#define SCSI_CMD_MSEL6		0x15 /* MODE SELECT(6) command */
#define SCSI_CMD_MSENSE6	0x1a /* MODE SENSE(6) command */
#define SCSI_CMD_MSEL10		0x55 /* MODE SELECT(10) command */
#define SCSI_CMD_MSENSE10	0x5a /* MODE SENSE(10) command */
#define SCSI_CMD_LOGSEL		0x4c /* LOG SELECT command */
#define SCSI_CMD_LOGSENSE	0x4d /* LOG SENSE command */
#define SCSI_CMD_PRI		0x5e /* PERSISTENT RESERVE IN command */
#define SCSI_CMD_PRO		0x5f /* PERSISTENT RESERVE OUT command */
#define SCSI_CMD_READ6		0x08 /* READ (6) command */
#define SCSI_CMD_READ10		0x28 /* READ (10) command */
#define SCSI_CMD_READ12		0xa8 /* READ (12) command */
#define SCSI_CMD_READ16		0x88 /* READ (16) command */
#define SCSI_CMD_READ32		0x7f /* READ (32) command */
#define SCSI_CMD_READBUF	0x3c /* READ BUFFER command */
#define SCSI_CMD_READCAP10	0x25 /* READ CAPACITY (10) command */	
#define SCSI_CMD_READCAP16	0x9e /* READ CAPACITY (10) command */	
#define SCSI_CMD_READDEF10	0x37 /* READ DEFECT DATA (10) */
#define SCSI_CMD_READDEF12	0xb7 /* READ DEFECT DATA (10) */
#define SCSI_CMD_REASSIGN	0x07 /* REASSIGN BLOCKS command */
#define SCSI_CMD_RECVDIAG	0x1c /* RECEIVE DIAGNOSTIC RESULTS command */
#define SCSI_CMD_RELEASE6	0x17 /* RELEASE(6) Command, obsolete */
#define SCSI_CMD_RELEASE10	0x57 /* RELEASE(10) Command, obsolete */
#define SCSI_CMD_DEVID		0xa3 /* REPORT DEVICE IDENTIFIER command */
#define SCSI_CMD_LUNS		0xa0 /* REPORT LUNS command */
#define SCSI_CMD_REQSENSE	0x03 /* REQUEST SENSE command */
#define SCSI_CMD_RESERVE6	0x16 /* RESERVE(6) command, obsolete */
#define SCSI_CMD_RESERVE10	0x56 /* RESERVE(10) command, obsolete */
#define SCSI_CMD_REZERO		0x01 /* REZERO UNIT command, obsolete */
#define SCSI_CMD_SEEK		0x0b /* SEEK command, obsolete */
#define SCSI_CMD_SEEKEXT	0x0b /* SEEK EXTENDED command */
#define SCSI_CMD_SENDDIAG	0x1d /* SEND DIAGNOSTIC command */
#define SCSI_CMD_SETDEVID	0xa4 /* SET DEVICE IDENTIFIER command */
#define SCSI_CMD_STARTSTOP	0x1b /* START STOP UNIT command */
#define SCSI_CMD_SYNCC10	0x35 /* SYNCHRONIZE CACHE (10) command */
#define SCSI_CMD_SYNCC16	0x91 /* SYNCHRONIZE CACHE (16) command */
#define SCSI_CMD_TUR		0x00 /* TEST UNIT READY */
#define SCSI_CMD_VERIFY10	0x2f /* VERIFY (10) command */
#define SCSI_CMD_VERIFY12	0xaf /* VERIFY (12) command */
#define SCSI_CMD_VERIFY16	0x8f /* VERIFY (16) command */
#define SCSI_CMD_VERIFY32	0x7f /* VERIFY (32) command */
#define SCSI_CMD_WRITE6		0x0a /* WRITE (6) command */
#define SCSI_CMD_WRITE10	0x2a /* WRITE (10) command */
#define SCSI_CMD_WRITE12	0xaa /* WRITE (12) command */
#define SCSI_CMD_WRITE16	0x8a /* WRITE (16) command */
#define SCSI_CMD_WRITE32	0x7f /* WRITE (32) command */
#define SCSI_CMD_WRITEAV10	0x2e /* WRITE AND VERIFY (10) command */
#define SCSI_CMD_WRITEAV12	0xae /* WRITE AND VERIFY (12) command */
#define SCSI_CMD_WRITEAV16	0x8e /* WRITE AND VERIFY (16) command */
#define SCSI_CMD_WRITEAV32	0x7f /* WRITE AND VERIFY (32) command */
#define SCSI_CMD_WRITEBUF	0x3b /* WRITE BUFFER command */
#define SCSI_CMD_WRITEL10	0x3f /* WRITE LONG (10) */
#define SCSI_CMD_WRITEL16	0x9f /* WRITE LONG (16) command */
#define SCSI_CMD_WRITES10	0x41 /* WRITE SAME (10) command */
#define SCSI_CMD_WRITES16	0x93 /* WRITE SAME (16) command */
#define SCSI_CMD_WRITES32	0x7f /* WRITE SAME (32) command */

#endif