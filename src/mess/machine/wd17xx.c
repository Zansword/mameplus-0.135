/***************************************************************************

    wd17xx.c

    Implementations of the Western Digital 17xx and 27xx families of
    floppy disk controllers


    Models:

              DAL   DD   Side   Clock       Remark
      ---------------------------------------------------------
      FD1771                    1 or 2 MHz  First model
      FD1781         x          1 or 2 MHz
      FD1791         x          1 or 2 MHz
      FD1792                    1 or 2 MHz
      FD1793   x     x          1 or 2 MHz
      FD1794   x                1 or 2 MHz
      FD1795         x     x    1 or 2 MHz
      FD1797   x     x     x    1 or 2 MHz
      FD1761         x          1 MHz
      FD1762                    1 MHz       ?
      FD1763   x     x          1 MHz
      FD1764   x                1 MHz       ?
      FD1765         x     x    1 MHz
      FD1767   x     x     x    1 MHz
      WD2791         x          1 or 2 MHz  Internal data separator
      WD2793   x     x          1 or 2 MHz  Internal data separator
      WD2795         x     x    1 or 2 MHz  Internal data separator
      WD2797   x     x     x    1 or 2 MHz  Internal data separator
      WD1770         x          8 MHz       Motor On signal
      WD1772         x          8 MHz       Motor On signal, Faster stepping rates
      WD1773         x          8 MHz       Enable precomp line

      Notes: - In general, later models include all features of earlier models
             - DAL: Data access lines, x = TRUE; otherwise inverted
             - DD: Double density support
             - Side: Has side select support
             - ?: Unknown if it really exists

    Clones:

      - SMC FD179x
      - Fujitsu MB8876 -> FD1791, MB8877 -> FD1793
      - VLSI VL177x


    Changelog:

    Kevin Thacker
        - Removed disk image code and replaced it with floppy drive functions.
          Any disc image is now useable with this code.
        - Fixed write protect

    2005-Apr-16 P.Harvey-Smith:
        - Increased the delay in wd17xx_timed_read_sector_request and
          wd17xx_timed_write_sector_request, to 40us to more closely match
          what happens on the real hardware, this has fixed the NitrOS9 boot
          problems.

    2007-Nov-01 Wilbert Pol:
        Needed these changes to get the MB8877 for Osborne-1 to work:
        - Added support for multiple record read
        - Changed the wd17xx_read_id to not return after DATADONEDELAY, but
          the host should read the id data through the data register. This
          was accomplished by making this change in the wd17xx_read_id
          function:
            -               wd17xx_complete_command(device, DELAY_DATADONE);
            +               wd17xx_set_data_request();

    2009-May-10 Robbbert:
        Further change to get the Kaypro II to work
        - When wd17xx_read_id has generated the 6 data bytes, it should make
          an IRQ and turn off the busy status. The timing for Osborne is
          critical, it must be between 300 and 700 usec, I've settled on 400.
          The Kaypro doesn't care timewise, but the busy flag must turn off
          sometime or it hangs.
            -       w->status |= STA_2_BUSY;
            +       wd17xx_set_busy(device, ATTOTIME_IN_USEC(400));

    2009-June-4 Roberto Lavarone:
        - Initial support for wd1771
        - Added simulation of head loaded feedback from drive
        - Bugfix: busy flag was cleared too early

    2009-June-21 Robbbert:
    - The Bugfix above, while valid, caused the Osborne1 to fail. This
      is because the delay must not exceed 14usec (found by extensive testing).
    - The minimum delay is 1usec, need by z80netf while formatting a disk.
    - http://www.bannister.org/forums/ubbthreads.php?ubb=showflat&Number=50889#Post50889
      explains the problems, testing done, and the test procedure for the z80netf.
    - Thus, the delay is set to 10usec, and all the disks I have (not many)
      seem to work.
    - Note to anyone who wants to change something: Make sure that the
      Osborne1 boots up! It is extremely sensitive to timing!
    - For testing only: The osborne1 rom can be patched to make it much
      more stable, by changing the byte at 0x0da7 from 0x28 to 0x18.

    2009-June-25 Robbbert:
    - Turns out kayproii not working, 10usec is too short.. but 11usec is ok.
      Setting it to 12usec.
      Really, this whole thing needs a complete rewrite.

    2009-July-08 Roberto Lavarone:
    - Fixed a bug in head load flag handling: einstein and samcoupe now working again

    2009-September-30 Robbbert:
    - Modified what status flags are returned after a Forced Interrupt,
      to allow Microbee to boot CP/M.

    TODO:
        - Multiple record write
        - What happens if a track is read that doesn't have any id's on it?
         (e.g. unformatted disc)

***************************************************************************/


#include "driver.h"
#include "machine/wd17xx.h"
#include "devices/flopdrv.h"


/***************************************************************************
    CONSTANTS
***************************************************************************/

#define VERBOSE			0	/* General logging */
#define VERBOSE_DATA	0	/* Logging of each byte during read and write */

#define DELAY_ERROR		3
#define DELAY_NOTREADY	1
#define DELAY_DATADONE	3

#define TYPE_I			1
#define TYPE_II 		2
#define TYPE_III		3
#define TYPE_IV 		4

#define FDC_STEP_RATE   0x03    /* Type I additional flags */
#define FDC_STEP_VERIFY 0x04	/* verify track number */
#define FDC_STEP_HDLOAD 0x08	/* load head */
#define FDC_STEP_UPDATE 0x10	/* update track register */

#define FDC_RESTORE 	0x00	/* Type I commands */
#define FDC_SEEK		0x10
#define FDC_STEP		0x20
#define FDC_STEP_IN 	0x40
#define FDC_STEP_OUT	0x60

#define FDC_MASK_TYPE_I 		(FDC_STEP_HDLOAD|FDC_STEP_VERIFY|FDC_STEP_RATE)

/* Type I commands status */
#define STA_1_BUSY		0x01	/* controller is busy */
#define STA_1_IPL		0x02	/* index pulse */
#define STA_1_TRACK0	0x04	/* track 0 detected */
#define STA_1_CRC_ERR	0x08	/* CRC error */
#define STA_1_SEEK_ERR	0x10	/* seek error */
#define STA_1_HD_LOADED 0x20	/* head loaded */
#define STA_1_WRITE_PRO 0x40	/* floppy is write protected */
#define STA_1_NOT_READY 0x80	/* controller not ready */

/* Type II and III additional flags */
#define FDC_DELETED_AM	0x01	/* read/write deleted address mark */
#define FDC_SIDE_CMP_T	0x02	/* side compare track data */
#define FDC_15MS_DELAY	0x04	/* delay 15ms before command */
#define FDC_SIDE_CMP_S	0x08	/* side compare sector data */
#define FDC_MULTI_REC	0x10	/* only for type II commands */

/* Type II commands */
#define FDC_READ_SEC	0x80	/* read sector */
#define FDC_WRITE_SEC	0xA0	/* write sector */

#define FDC_MASK_TYPE_II		(FDC_MULTI_REC|FDC_SIDE_CMP_S|FDC_15MS_DELAY|FDC_SIDE_CMP_T|FDC_DELETED_AM)

/* Type II commands status */
#define STA_2_BUSY		0x01
#define STA_2_DRQ		0x02
#define STA_2_LOST_DAT	0x04
#define STA_2_CRC_ERR	0x08
#define STA_2_REC_N_FND 0x10
#define STA_2_REC_TYPE	0x20
#define STA_2_WRITE_PRO 0x40
#define STA_2_NOT_READY 0x80

#define FDC_MASK_TYPE_III		(FDC_SIDE_CMP_S|FDC_15MS_DELAY|FDC_SIDE_CMP_T|FDC_DELETED_AM)

/* Type III commands */
#define FDC_READ_DAM	0xc0	/* read data address mark */
#define FDC_READ_TRK	0xe0	/* read track */
#define FDC_WRITE_TRK	0xf0	/* write track (format) */

/* Type IV additional flags */
#define FDC_IM0 		0x01	/* interrupt mode 0 */
#define FDC_IM1 		0x02	/* interrupt mode 1 */
#define FDC_IM2 		0x04	/* interrupt mode 2 */
#define FDC_IM3 		0x08	/* interrupt mode 3 */

#define FDC_MASK_TYPE_IV		(FDC_IM3|FDC_IM2|FDC_IM1|FDC_IM0)

/* Type IV commands */
#define FDC_FORCE_INT	0xd0	/* force interrupt */

/* enumeration to specify the type of FDC; there are subtle differences */
typedef enum
{
	WD_TYPE_1770,
	WD_TYPE_1771,
	WD_TYPE_1772,
	WD_TYPE_1773,
	WD_TYPE_179X,
	WD_TYPE_1793,
	WD_TYPE_2793,

	/* duplicate constants */
	WD_TYPE_177X = WD_TYPE_1770,
	WD_TYPE_MB8877 = WD_TYPE_179X
} wd17xx_type_t;

/* structure describing a double density track */
#define TRKSIZE_DD		6144
#if 0
static const UINT8 track_DD[][2] = {
	{16, 0x4e}, 	/* 16 * 4E (track lead in)               */
	{ 8, 0x00}, 	/*  8 * 00 (pre DAM)                     */
	{ 3, 0xf5}, 	/*  3 * F5 (clear CRC)                   */

	{ 1, 0xfe}, 	/* *** sector *** FE (DAM)               */
	{ 1, 0x80}, 	/*  4 bytes track,head,sector,seclen     */
	{ 1, 0xf7}, 	/*  1 * F7 (CRC)                         */
	{22, 0x4e}, 	/* 22 * 4E (sector lead in)              */
	{12, 0x00}, 	/* 12 * 00 (pre AM)                      */
	{ 3, 0xf5}, 	/*  3 * F5 (clear CRC)                   */
	{ 1, 0xfb}, 	/*  1 * FB (AM)                          */
	{ 1, 0x81}, 	/*  x bytes sector data                  */
	{ 1, 0xf7}, 	/*  1 * F7 (CRC)                         */
	{16, 0x4e}, 	/* 16 * 4E (sector lead out)             */
	{ 8, 0x00}, 	/*  8 * 00 (post sector)                 */
	{ 0, 0x00}, 	/* end of data                           */
};
#endif

/* structure describing a single density track */
#define TRKSIZE_SD		3172
#if 0
static const UINT8 track_SD[][2] = {
	{16, 0xff}, 	/* 16 * FF (track lead in)               */
	{ 8, 0x00}, 	/*  8 * 00 (pre DAM)                     */
	{ 1, 0xfc}, 	/*  1 * FC (clear CRC)                   */

	{11, 0xff}, 	/* *** sector *** 11 * FF                */
	{ 6, 0x00}, 	/*  6 * 00 (pre DAM)                     */
	{ 1, 0xfe}, 	/*  1 * FE (DAM)                         */
	{ 1, 0x80}, 	/*  4 bytes track,head,sector,seclen     */
	{ 1, 0xf7}, 	/*  1 * F7 (CRC)                         */
	{10, 0xff}, 	/* 10 * FF (sector lead in)              */
	{ 4, 0x00}, 	/*  4 * 00 (pre AM)                      */
	{ 1, 0xfb}, 	/*  1 * FB (AM)                          */
	{ 1, 0x81}, 	/*  x bytes sector data                  */
	{ 1, 0xf7}, 	/*  1 * F7 (CRC)                         */
	{ 0, 0x00}, 	/* end of data                           */
};
#endif

enum
{
	MISCCALLBACK_COMMAND = 0,
	MISCCALLBACK_DATA
};


/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _wd1770_state wd1770_state;
struct _wd1770_state
{
	/* callbacks */
	devcb_resolved_write_line out_intrq_func;
	devcb_resolved_write_line out_drq_func;

	/* input lines */
	int mr;            /* master reset */
	int rdy;           /* ready, enable precomp */
	int tr00;          /* track 00 */
	int idx;           /* index */
	int wprt;          /* write protect */
	int dden;          /* double density */

	/* output lines */
	int mo;            /* motor on */
	int dirc;          /* direction */
	int drq;           /* data request */
	int intrq;         /* interrupt request */

	/* register */
	UINT8 data_shift;
	UINT8 data;
	UINT8 track;
	UINT8 sector;
	UINT8 command;
	UINT8 status;

	int stepping_rate[4];  /* track stepping rate in ms */


	DENSITY   density;				/* FM/MFM, single / double density */
	wd17xx_type_t type;
	UINT8	track_reg;				/* value of track register */
	UINT8	command_type;			/* last command type */
	UINT8	head;					/* current head # */

	UINT8	read_cmd;				/* last read command issued */
	UINT8	write_cmd;				/* last write command issued */
	INT8	direction;				/* last step direction */

	UINT8	status_drq; 			/* status register data request bit */
	UINT8	status_ipl; 			/* status register toggle index pulse bit */
	UINT8	busy_count; 			/* how long to keep busy bit set */

	UINT8	buffer[6144];			/* I/O buffer (holds up to a whole track) */
	UINT32	data_offset;			/* offset into I/O buffer */
	INT32	data_count; 			/* transfer count from/into I/O buffer */

	UINT8	*fmt_sector_data[256];	/* pointer to data after formatting a track */

	UINT8	dam_list[256][4];		/* list of data address marks while formatting */
	int 	dam_data[256];			/* offset to data inside buffer while formatting */
	int 	dam_cnt;				/* valid number of entries in the dam_list */
	UINT16	sector_length;			/* sector length (byte) */

	UINT8	ddam;					/* ddam of sector found - used when reading */
	UINT8	sector_data_id;
	emu_timer	*timer, *timer_rs, *timer_ws, *timer_rid;
	int		data_direction;

	UINT8   ipl;					/* index pulse */
	int     hld_count;				/* head loaded counter */

	emu_timer *busy_timer;

	/* this is the drive currently selected */
	const device_config *drive;

	/* this is the head currently selected */
	UINT8 hd;

	/* pause time when writing/reading sector */
	int pause_time;

	/* Pointer to interface */
	const wd17xx_interface *intf;
};


/***************************************************************************
    DEFAULT INTERFACES
***************************************************************************/

const wd17xx_interface default_wd17xx_interface =
{
	DEVCB_NULL, DEVCB_NULL, { FLOPPY_0, FLOPPY_1, FLOPPY_2, FLOPPY_3}
};

const wd17xx_interface default_wd17xx_interface_2_drives =
{
	DEVCB_NULL, DEVCB_NULL, { FLOPPY_0, FLOPPY_1, NULL, NULL}
};


/***************************************************************************
    PROTOTYPES
***************************************************************************/

static void wd17xx_complete_command(const device_config *device, int delay);
static void wd17xx_timed_data_request(const device_config *device);
static TIMER_CALLBACK(wd17xx_misc_timer_callback);
static TIMER_CALLBACK(wd17xx_read_sector_callback);
static TIMER_CALLBACK(wd17xx_write_sector_callback);
static void wd17xx_index_pulse_callback(const device_config *controller,const device_config *img, int state);


/*****************************************************************************
    INLINE FUNCTIONS
*****************************************************************************/

INLINE wd1770_state *get_safe_token(const device_config *device)
{
	assert(device != NULL);
	assert(device->token != NULL);

	return (wd1770_state *)device->token;
}


/***************************************************************************
    HELPER FUNCTIONS
***************************************************************************/

/* calculate CRC for data address marks or sector data */
static void calc_crc(UINT16 *crc, UINT8 value)
{
	UINT8 l, h;

	l = value ^ (*crc >> 8);
	*crc = (*crc & 0xff) | (l << 8);
	l >>= 4;
	l ^= (*crc >> 8);
	*crc <<= 8;
	*crc = (*crc & 0xff00) | l;
	l = (l << 4) | (l >> 4);
	h = l;
	l = (l << 2) | (l >> 6);
	l &= 0x1f;
	*crc = *crc ^ (l << 8);
	l = h & 0xf0;
	*crc = *crc ^ (l << 8);
	l = (h << 1) | (h >> 7);
	l &= 0xe0;
	*crc = *crc ^ l;
}

static int wd17xx_has_side_select(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);
	return (w->type == WD_TYPE_1773) || (w->type == WD_TYPE_1793) || (w->type == WD_TYPE_2793);
}

static int wd17xx_get_datarate_in_us(DENSITY density)
{
	switch (density)
	{
		case DEN_FM_LO:  return 128; break;
		case DEN_FM_HI:	 return  64; break;
		default:
		case DEN_MFM_LO: return  32; break;
		case DEN_MFM_HI: return  16; break;
	}
}


/***************************************************************************
    IMPLEMENTATION
***************************************************************************/

/* clear a data request */
static void wd17xx_clear_drq(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);

	w->status &= ~STA_2_DRQ;

	w->drq = CLEAR_LINE;
	devcb_call_write_line(&w->out_drq_func, w->drq);
}

/* set data request */
static void wd17xx_set_drq(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);

	if (w->status & STA_2_DRQ)
		w->status |= STA_2_LOST_DAT;

	w->status |= STA_2_DRQ;

	w->drq = ASSERT_LINE;
	devcb_call_write_line(&w->out_drq_func, w->drq);
}

/* clear interrupt request */
static void	wd17xx_clear_intrq(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);

	w->intrq = CLEAR_LINE;
	devcb_call_write_line(&w->out_intrq_func, w->intrq);
}

/* set interrupt request */
static void	wd17xx_set_intrq(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);

	w->status &= ~STA_2_BUSY;

	w->intrq = ASSERT_LINE;
	devcb_call_write_line(&w->out_intrq_func, w->intrq);
}



static TIMER_CALLBACK( wd17xx_busy_callback )
{
	const device_config *device = ptr;
	wd1770_state *w = get_safe_token(device);
	wd17xx_set_intrq(device);
	timer_reset(w->busy_timer, attotime_never);
}



static void wd17xx_set_busy(const device_config *device, attotime duration)
{
	wd1770_state *w = get_safe_token(device);

	w->status |= STA_1_BUSY;
	timer_adjust_oneshot(w->busy_timer, duration, 0);
}



/* BUSY COUNT DOESN'T WORK PROPERLY! */

static void wd17xx_command_restore(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);
	UINT8 step_counter;

	if (w->drive == NULL)
		return;

	step_counter = 255;

#if 0
	w->status |= STA_1_BUSY;
#endif

	/* setup step direction */
	w->direction = -1;

	w->command_type = TYPE_I;

	/* reset busy count */
	w->busy_count = 0;

	if (image_slotexists(w->drive))
	{
		/* keep stepping until track 0 is received or 255 steps have been done */
		while (!(floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_HEAD_AT_TRACK_0)) && (step_counter!=0))
		{
			/* update time to simulate seek time busy signal */
			w->busy_count++;
			floppy_drive_seek(w->drive, w->direction);
			step_counter--;
		}
	}

	/* update track reg */
	w->track = 0;
#if 0
	/* simulate seek time busy signal */
	w->busy_count = 0;	//w->busy_count * ((w->data & FDC_STEP_RATE) + 1);

	/* when command completes set irq */
	wd17xx_set_intrq(device);
#endif
	wd17xx_set_busy(device, ATTOTIME_IN_USEC(100));
}


/* track writing, converted to format commands */
static void write_track(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);
	int i;
	for (i=0;i+4<w->data_offset;)
	{
		if (w->buffer[i]==0xfe)
		{
			/* got address mark */
			int track   = w->buffer[i+1];
			int side    = w->buffer[i+2];
			int sector  = w->buffer[i+3];
			//int len     = w->buffer[i+4];
			int filler  = 0xe5; /* IBM and Thomson */
			int density = w->density;
			floppy_drive_format_sector(w->drive,side,sector,track,
						w->hd,sector,density?1:0,filler);
			i += 128; /* at least... */
		}
		else
			i++;
	}
}


/* read an entire track */
static void read_track(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);
#if 0
	UINT8 *psrc;		/* pointer to track format structure */
	UINT8 *pdst;		/* pointer to track buffer */
	int cnt;			/* number of bytes to fill in */
	UINT16 crc; 		/* id or data CRC */
	UINT8 d;			/* data */
	UINT8 t = w->track; /* track of DAM */
	UINT8 h = w->head;	/* head of DAM */
	UINT8 s = w->sector_dam;		/* sector of DAM */
	UINT16 l = w->sector_length;	/* sector length of DAM */
	int i;

	for (i = 0; i < w->sec_per_track; i++)
	{
		w->dam_list[i][0] = t;
		w->dam_list[i][1] = h;
		w->dam_list[i][2] = i;
		w->dam_list[i][3] = l >> 8;
	}

	pdst = w->buffer;

	if (w->density)
	{
		psrc = track_DD[0];    /* double density track format */
		cnt = TRKSIZE_DD;
	}
	else
	{
		psrc = track_SD[0];    /* single density track format */
		cnt = TRKSIZE_SD;
	}

	while (cnt > 0)
	{
		if (psrc[0] == 0)	   /* no more track format info ? */
		{
			if (w->dam_cnt < w->sec_per_track) /* but more DAM info ? */
			{
				if (w->density)/* DD track ? */
					psrc = track_DD[3];
				else
					psrc = track_SD[3];
			}
		}

		if (psrc[0] != 0)	   /* more track format info ? */
		{
			cnt -= psrc[0];    /* subtract size */
			d = psrc[1];

			if (d == 0xf5)	   /* clear CRC ? */
			{
				crc = 0xffff;
				d = 0xa1;	   /* store A1 */
			}

			for (i = 0; i < *psrc; i++)
				*pdst++ = d;   /* fill data */

			if (d == 0xf7)	   /* store CRC ? */
			{
				pdst--; 	   /* go back one byte */
				*pdst++ = crc & 255;	/* put CRC low */
				*pdst++ = crc / 256;	/* put CRC high */
				cnt -= 1;	   /* count one more byte */
			}
			else if (d == 0xfe)/* address mark ? */
			{
				crc = 0xffff;	/* reset CRC */
			}
			else if (d == 0x80)/* sector ID ? */
			{
				pdst--; 	   /* go back one byte */
				t = *pdst++ = w->dam_list[w->dam_cnt][0]; /* track number */
				h = *pdst++ = w->dam_list[w->dam_cnt][1]; /* head number */
				s = *pdst++ = w->dam_list[w->dam_cnt][2]; /* sector number */
				l = *pdst++ = w->dam_list[w->dam_cnt][3]; /* sector length code */
				w->dam_cnt++;
				calc_crc(&crc, t);	/* build CRC */
				calc_crc(&crc, h);	/* build CRC */
				calc_crc(&crc, s);	/* build CRC */
				calc_crc(&crc, l);	/* build CRC */
				l = (l == 0) ? 128 : l << 8;
			}
			else if (d == 0xfb)// data address mark ?
			{
				crc = 0xffff;	// reset CRC
			}
			else if (d == 0x81)// sector DATA ?
			{
				pdst--; 	   /* go back one byte */
				if (seek(w, t, h, s) == 0)
				{
					if (mame_fread(w->image_file, pdst, l) != l)
					{
						w->status = STA_2_CRC_ERR;
						return;
					}
				}
				else
				{
					w->status = STA_2_REC_N_FND;
					return;
				}
				for (i = 0; i < l; i++) // build CRC of all data
					calc_crc(&crc, *pdst++);
				cnt -= l;
			}
			psrc += 2;
		}
		else
		{
			*pdst++ = 0xff;    /* fill track */
			cnt--;			   /* until end */
		}
	}
#endif

	if (w->type == WD_TYPE_1771)
		w->data_count = TRKSIZE_SD;
	else
		w->data_count = (w->density) ? TRKSIZE_DD : TRKSIZE_SD;

	floppy_drive_read_track_data_info_buffer( w->drive, w->hd, (char *)w->buffer, &(w->data_count) );

	w->data_offset = 0;

	wd17xx_set_drq(device);
	w->status |= STA_2_BUSY;
	w->busy_count = 0;
}


/* read the next data address mark */
static void wd17xx_read_id(const device_config *device)
{
	chrn_id id;
	wd1770_state *w = get_safe_token(device);

	w->status &= ~(STA_2_CRC_ERR | STA_2_REC_N_FND);

	/* get next id from disc */
	if (floppy_drive_get_next_id(w->drive, w->hd, &id))
	{
		UINT16 crc = 0xffff;

		w->data_offset = 0;
		w->data_count = 6;

		/* for MFM */
		/* crc includes 3x0x0a1, and 1x0x0fe (id mark) */
		calc_crc(&crc,0x0a1);
		calc_crc(&crc,0x0a1);
		calc_crc(&crc,0x0a1);
		calc_crc(&crc,0x0fe);

		w->buffer[0] = id.C;
		w->buffer[1] = id.H;
		w->buffer[2] = id.R;
		w->buffer[3] = id.N;
		calc_crc(&crc, w->buffer[0]);
		calc_crc(&crc, w->buffer[1]);
		calc_crc(&crc, w->buffer[2]);
		calc_crc(&crc, w->buffer[3]);
		/* crc is stored hi-byte followed by lo-byte */
		w->buffer[4] = crc>>8;
		w->buffer[5] = crc & 255;

		w->sector = id.C;
		wd17xx_set_busy(device, ATTOTIME_IN_USEC(400));
		w->busy_count = 0;

		wd17xx_set_drq(device);

		if (VERBOSE)
			logerror("read id succeeded.\n");
	}
	else
	{
		/* record not found */
		w->status |= STA_2_REC_N_FND;
		//w->sector = w->track;
		if (VERBOSE)
			logerror("read id failed\n");

		wd17xx_complete_command(device, DELAY_ERROR);
	}
}



static void wd17xx_index_pulse_callback(const device_config *controller, const device_config *img, int state)
{
	wd1770_state *w = get_safe_token(controller);

	if (img != w->drive)
		return;

	w->ipl = state;

	if (w->hld_count)
		w->hld_count--;
}



static int wd17xx_locate_sector(const device_config *device)
{
	UINT8 revolution_count;
	chrn_id id;
	wd1770_state *w = get_safe_token(device);

	revolution_count = 0;

	w->status &= ~STA_2_REC_N_FND;

	while (revolution_count!=4)
	{
		if (floppy_drive_get_next_id(w->drive, w->hd, &id))
		{
			/* compare track */
			if (id.C == w->track)
			{
				/* compare head, if we were asked to */
				if (!wd17xx_has_side_select(device) || (id.H == w->head) || (w->head == (UINT8) ~0))
				{
					/* compare id */
					if (id.R == w->sector)
					{
						w->sector_length = 1<<(id.N+7);
						w->sector_data_id = id.data_id;
						/* get ddam status */
						w->ddam = id.flags & ID_FLAG_DELETED_DATA;
						/* got record type here */
						if (VERBOSE)
							logerror("sector found! C:$%02x H:$%02x R:$%02x N:$%02x%s\n", id.C, id.H, id.R, id.N, w->ddam ? " DDAM" : "");
						return 1;
					}
				}
			}
		}

		 /* index set? */
		if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_INDEX))
		{
			/* update revolution count */
			revolution_count++;
		}
	}
	return 0;
}


static int wd17xx_find_sector(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);
	if ( wd17xx_locate_sector(device) )
	{
		return 1;
	}

	/* record not found */
	w->status |= STA_2_REC_N_FND;

	if (VERBOSE)
		logerror("track %d sector %d not found!\n", w->track, w->sector);

	wd17xx_complete_command(device, DELAY_ERROR);

	return 0;
}



/* read a sector */
static void wd17xx_read_sector(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);
	w->data_offset = 0;

	/* side compare? */
	if (w->read_cmd & 0x02)
		w->head = (w->read_cmd & 0x08) ? 1 : 0;
	else
		w->head = ~0;

	if (wd17xx_find_sector(device))
	{
		w->data_count = w->sector_length;

		/* read data */
		floppy_drive_read_sector_data(w->drive, w->hd, w->sector_data_id, (char *)w->buffer, w->sector_length);

		wd17xx_timed_data_request(device);

		w->status |= STA_2_BUSY;
		w->busy_count = 0;
	}
}



static TIMER_CALLBACK(wd17xx_misc_timer_callback)
{
	const device_config *device = ptr;
	int callback_type = param;
	wd1770_state *w = get_safe_token(device);

	switch(callback_type) {
	case MISCCALLBACK_COMMAND:
		/* command callback */
		wd17xx_set_intrq(device);
		break;

	case MISCCALLBACK_DATA:
		/* data callback */
		/* ok, trigger data request now */
		wd17xx_set_drq(device);
		break;
	}

	/* stop it, but don't allow it to be free'd */
	timer_reset(w->timer, attotime_never);
}


/* called on error, or when command is actually completed */
/* KT - I have used a timer for systems that use interrupt driven transfers.
A interrupt occurs after the last byte has been read. If it occurs at the time
when the last byte has been read it causes problems - same byte read again
or bytes missed */
/* TJL - I have add a parameter to allow the emulation to specify the delay
*/
static void wd17xx_complete_command(const device_config *device, int delay)
{
	int usecs;
	wd1770_state *w = get_safe_token(device);

	w->data_count = 0;

	w->hld_count = 2;

#if 0
	/* clear busy bit */
	/* RL - removed, busy bit must be on until wd17xx_misc_timer_callback() is fired */
	/* Robbbert - adjusted delay value (see notes above) to fix the osborne1 */
	w->status &= ~STA_2_BUSY;

	usecs = wd17xx_get_datarate_in_us(w->density);
	usecs *= delay;
#endif

	usecs = 12;

	/* set new timer */
	timer_adjust_oneshot(w->timer, ATTOTIME_IN_USEC(usecs), MISCCALLBACK_COMMAND);
}



static void wd17xx_write_sector(const device_config *device)
{
	wd1770_state *w = get_safe_token(device);
	/* at this point, the disc is write enabled, and data
     * has been transfered into our buffer - now write it to
     * the disc image or to the real disc
     */

	/* side compare? */
	if (w->write_cmd & 0x02)
		w->head = (w->write_cmd & 0x08) ? 1 : 0;
	else
		w->head = ~0;

	/* find sector */
	if (wd17xx_find_sector(device))
	{
		w->data_count = w->sector_length;

		/* write data */
		floppy_drive_write_sector_data(w->drive, w->hd, w->sector_data_id, (char *)w->buffer, w->sector_length, w->write_cmd & 0x01);
	}
}



/* verify the seek operation by looking for a id that has a matching track value */
static void wd17xx_verify_seek(const device_config *device)
{
	UINT8 revolution_count;
	chrn_id id;
	wd1770_state *w = get_safe_token(device);

	revolution_count = 0;

	if (VERBOSE)
		logerror("doing seek verify\n");

	w->status &= ~STA_1_SEEK_ERR;

	/* must be found within 5 revolutions otherwise error */
	while (revolution_count!=5)
	{
		if (floppy_drive_get_next_id(w->drive, w->hd, &id))
		{
			/* compare track */
			if (id.C == w->track)
			{
				if (VERBOSE)
					logerror("seek verify succeeded!\n");
				return;
			}
		}

		 /* index set? */
		if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_INDEX))
		{
			/* update revolution count */
			revolution_count++;
		}
	}

	w->status |= STA_1_SEEK_ERR;

	if (VERBOSE)
		logerror("failed seek verify!\n");
}



/* callback to initiate read sector */
static TIMER_CALLBACK( wd17xx_read_sector_callback )
{
	const device_config *device = ptr;
	wd1770_state *w = get_safe_token(device);

	/* ok, start that read! */

	if (VERBOSE)
		logerror("wd179x: Read Sector callback.\n");

	if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
		wd17xx_complete_command(device, DELAY_NOTREADY);
	else
		wd17xx_read_sector(device);

	/* stop it, but don't allow it to be free'd */
	timer_reset(w->timer_rs, attotime_never);
}



/* callback to initiate write sector */
static TIMER_CALLBACK(wd17xx_write_sector_callback)
{
	const device_config *device = ptr;
	wd1770_state *w = get_safe_token(device);

	/* ok, start that write! */

	if (VERBOSE)
		logerror("wd179x: Write Sector callback.\n");

	if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
		wd17xx_complete_command(device, DELAY_NOTREADY);
	else
	{

		/* drive write protected? */
		if (floppy_drive_get_flag_state(w->drive,FLOPPY_DRIVE_DISK_WRITE_PROTECTED))
		{
			w->status |= STA_2_WRITE_PRO;

			wd17xx_complete_command(device, DELAY_ERROR);
		}
		else
		{
			/* side compare? */
			if (w->write_cmd & 0x02)
				w->head = (w->write_cmd & 0x08) ? 1 : 0;
			else
				w->head = ~0;

			/* attempt to find it first before getting data from cpu */
			if (wd17xx_find_sector(device))
			{
				/* request data */
				w->data_offset = 0;
				w->data_count = w->sector_length;

				wd17xx_set_drq(device);

				w->status |= STA_2_BUSY;
				w->busy_count = 0;
			}
		}
	}

	/* stop it, but don't allow it to be free'd */
	timer_reset(w->timer_ws, attotime_never);
}



/* setup a timed data request - data request will be triggered in a few usecs time */
static void wd17xx_timed_data_request(const device_config *device)
{
	int usecs;
	wd1770_state *w = get_safe_token(device);

	usecs = wd17xx_get_datarate_in_us(w->density);

	/* set new timer */
	timer_adjust_oneshot(w->timer, ATTOTIME_IN_USEC(usecs), MISCCALLBACK_DATA);
}



/* setup a timed read sector - read sector will be triggered in a few usecs time */
static void wd17xx_timed_read_sector_request(const device_config *device)
{
	int usecs;
	wd1770_state *w = get_safe_token(device);

	usecs = w->pause_time; /* How long should we wait? How about 40 micro seconds? */

	/* set new timer */
	timer_reset(w->timer_rs, ATTOTIME_IN_USEC(usecs));
}



/* setup a timed write sector - write sector will be triggered in a few usecs time */
static void wd17xx_timed_write_sector_request(const device_config *device)
{
	int usecs;
	wd1770_state *w = get_safe_token(device);

	usecs = w->pause_time; /* How long should we wait? How about 40 micro seconds? */

	/* set new timer */
	timer_reset(w->timer_ws, ATTOTIME_IN_USEC(usecs));
}


/***************************************************************************
    INTERFACE
***************************************************************************/

/* use this to determine which drive is controlled by WD */
void wd17xx_set_drive(const device_config *device, UINT8 drive)
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
		logerror("wd17xx_set_drive: $%02x\n", drive);

	if (w->intf->floppy_drive_tags[drive] != NULL)
		w->drive = devtag_get_device(device->machine, w->intf->floppy_drive_tags[drive]);
}

void wd17xx_set_side(const device_config *device, UINT8 head)
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
	{
		if (head != w->hd)
			logerror("wd17xx_set_side: $%02x\n", head);
	}

	w->hd = head;
}

void wd17xx_set_density(const device_config *device, DENSITY density)
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
	{
		if (w->density != density)
			logerror("wd17xx_set_density: $%02x\n", density);
	}

	w->density = density;
}

void wd17xx_set_pause_time(const device_config *device, int usec)
{
	wd1770_state *w = get_safe_token(device);
	w->pause_time = usec;
}


/***************************************************************************
    DEVICE HANDLERS
***************************************************************************/

/* master reset */
WRITE_LINE_DEVICE_HANDLER( wd17xx_mr_w )
{
	wd1770_state *w = get_safe_token(device);

	/* reset device when going from high to low */
	if (w->mr && state == CLEAR_LINE)
	{
		w->command = 0x03;
		w->status &= ~STA_1_NOT_READY; /* ? */
	}

	/* execute restore command when going from low to high */
	if (w->mr == CLEAR_LINE && state)
	{
		wd17xx_command_restore(device);
		w->sector = 0x01;
	}

	w->mr = state;
}

/* ready and enable precomp (1773 only) */
WRITE_LINE_DEVICE_HANDLER( wd17xx_rdy_w )
{
	wd1770_state *w = get_safe_token(device);
	w->rdy = state;
}

/* motor on, 1770 and 1772 only */
READ_LINE_DEVICE_HANDLER( wd17xx_mo_r )
{
	wd1770_state *w = get_safe_token(device);
	return w->mo;
}

/* track zero */
WRITE_LINE_DEVICE_HANDLER( wd17xx_tr00_w )
{
	wd1770_state *w = get_safe_token(device);
	w->tr00 = state;
}

/* index pulse */
WRITE_LINE_DEVICE_HANDLER( wd17xx_idx_w )
{
	wd1770_state *w = get_safe_token(device);
	w->idx = state;
}

/* write protect status */
WRITE_LINE_DEVICE_HANDLER( wd17xx_wprt_w )
{
	wd1770_state *w = get_safe_token(device);
	w->wprt = state;
}

/* data request */
READ_LINE_DEVICE_HANDLER( wd17xx_drq_r )
{
	wd1770_state *w = get_safe_token(device);
	return w->drq;
}

/* interrupt request */
READ_LINE_DEVICE_HANDLER( wd17xx_intrq_r )
{
	wd1770_state *w = get_safe_token(device);
	return w->intrq;
}

/* read the FDC status register. This clears IRQ line too */
READ8_DEVICE_HANDLER( wd17xx_status_r )
{
	wd1770_state *w = get_safe_token(device);
	int result = w->status;

	wd17xx_clear_intrq(device);

	/* type 1 command or force int command? */
	if ((w->command_type==TYPE_I) || (w->command_type==TYPE_IV))
	{
		/* toggle index pulse */
		result &= ~STA_1_IPL;
		if (w->ipl) result |= STA_1_IPL;

		/* set track 0 state */
		result &=~STA_1_TRACK0;
		if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_HEAD_AT_TRACK_0))
			result |= STA_1_TRACK0;

	//  floppy_drive_set_ready_state(w->drive, 1,1);
		w->status &= ~STA_1_NOT_READY;

		/* TODO: What is this?  We need some more info on this */
		if ((w->type == WD_TYPE_179X) || (w->type == WD_TYPE_1793) || (w->type == WD_TYPE_1773) || (w->type == WD_TYPE_1771))
		{
			if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
				w->status |= STA_1_NOT_READY;
		}
		else
		{
			if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
				w->status |= STA_1_NOT_READY;
		}

		if (w->command_type==TYPE_I)
		{
			if (w->hld_count)
				w->status |= STA_1_HD_LOADED;
			else
				w->status &= ~ STA_1_HD_LOADED;
		}
		if (w->command_type==TYPE_IV) result &= 0x63; /* to allow microbee to boot up */
	}

	/* eventually set data request bit */
//  w->status |= w->status_drq;

	if (VERBOSE)
	{
		if (w->data_count < 4)
			logerror("wd17xx_status_r: $%02X (data_count %d)\n", result, w->data_count);
	}

	return result;
}

/* read the FDC track register */
READ8_DEVICE_HANDLER( wd17xx_track_r )
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
		logerror("wd17xx_track_r: $%02X\n", w->track);

	return w->track;
}

/* read the FDC sector register */
READ8_DEVICE_HANDLER( wd17xx_sector_r )
{
	wd1770_state *w = get_safe_token(device);

	if (VERBOSE)
		logerror("wd17xx_sector_r: $%02X\n", w->sector);

	return w->sector;
}

/* read the FDC data register */
READ8_DEVICE_HANDLER( wd17xx_data_r )
{
	wd1770_state *w = get_safe_token(device);

	if (w->data_count >= 1)
	{
		/* clear data request */
		wd17xx_clear_drq(device);

		/* yes */
		w->data = w->buffer[w->data_offset++];

		if (VERBOSE_DATA)
			logerror("wd17xx_data_r: $%02X (data_count %d)\n", w->data, w->data_count);

		/* any bytes remaining? */
		if (--w->data_count < 1)
		{
			/* no */
			w->data_offset = 0;

			/* clear ddam type */
			w->status &=~STA_2_REC_TYPE;
			/* read a sector with ddam set? */
			if (w->command_type == TYPE_II && w->ddam != 0)
			{
				/* set it */
				w->status |= STA_2_REC_TYPE;
			}

			/* Check we should handle the next sector for a multi record read */
			if ( w->command_type == TYPE_II && w->command == FDC_READ_SEC && ( w->read_cmd & 0x10 ) ) {
				w->sector++;
				if (wd17xx_locate_sector(device))
				{
					w->data_count = w->sector_length;

					/* read data */
					floppy_drive_read_sector_data(w->drive, w->hd, w->sector_data_id, (char *)w->buffer, w->sector_length);

					wd17xx_timed_data_request(device);

					w->status |= STA_2_BUSY;
					w->busy_count = 0;
				}
				else
				{
					wd17xx_complete_command(device, DELAY_DATADONE);

					if (VERBOSE)
						logerror("wd17xx_data_r(): multi data read completed\n");
				}
			}
			else
			{
				/* Delay the INTRQ 3 byte times because we need to read two CRC bytes and
                   compare them with a calculated CRC */
				wd17xx_complete_command(device, DELAY_DATADONE);

				if (VERBOSE)
					logerror("wd17xx_data_r(): data read completed\n");
			}
		}
		else
		{
			/* issue a timed data request */
			wd17xx_timed_data_request(device);
		}
	}
	else
	{
		logerror("wd17xx_data_r: (no new data) $%02X (data_count %d)\n", w->data, w->data_count);
	}
	return w->data;
}

/* write the FDC command register */
WRITE8_DEVICE_HANDLER( wd17xx_command_w )
{
	wd1770_state *w = get_safe_token(device);

	floppy_drive_set_motor_state(w->drive, 1);
	floppy_drive_set_ready_state(w->drive, 1,0);

	/* also cleared by writing command */
	wd17xx_clear_intrq(device);

	/* clear write protected. On read sector, read track and read dam, write protected bit is clear */
	w->status &= ~((1<<6) | (1<<5) | (1<<4));

	if ((data & ~FDC_MASK_TYPE_IV) == FDC_FORCE_INT)
	{
		if (VERBOSE)
			logerror("wd17xx_command_w $%02X FORCE_INT (data_count %d)\n", data, w->data_count);

		w->data_count = 0;
		w->data_offset = 0;
		w->status &= ~STA_2_BUSY;
		w->status &= ~STA_2_LOST_DAT;

		wd17xx_clear_drq(device);

		if (data & 0x0f)
		{



		}


//      w->status_ipl = STA_1_IPL;
/*      w->status_ipl = 0; */

		w->busy_count = 0;
		w->command_type = TYPE_IV;
		return;
	}

	if (data & 0x80)
	{
		/*w->status_ipl = 0;*/

		if ((data & ~FDC_MASK_TYPE_II) == FDC_READ_SEC)
		{
			if (VERBOSE)
				logerror("wd17xx_command_w $%02X READ_SEC\n", data);

			w->read_cmd = data;
			w->command = data & ~FDC_MASK_TYPE_II;
			w->command_type = TYPE_II;
			w->status &= ~STA_2_LOST_DAT;
			w->status |= STA_2_BUSY;
			wd17xx_clear_drq(device);

			wd17xx_timed_read_sector_request(device);

			return;
		}

		if ((data & ~FDC_MASK_TYPE_II) == FDC_WRITE_SEC)
		{
			if (VERBOSE)
				logerror("wd17xx_command_w $%02X WRITE_SEC\n", data);

			w->write_cmd = data;
			w->command = data & ~FDC_MASK_TYPE_II;
			w->command_type = TYPE_II;
			w->status &= ~STA_2_LOST_DAT;
			w->status |= STA_2_BUSY;
			wd17xx_clear_drq(device);

			wd17xx_timed_write_sector_request(device);

			return;
		}

		if ((data & ~FDC_MASK_TYPE_III) == FDC_READ_TRK)
		{
			if (VERBOSE)
				logerror("wd17xx_command_w $%02X READ_TRK\n", data);

			w->command = data & ~FDC_MASK_TYPE_III;
			w->command_type = TYPE_III;
			w->status &= ~STA_2_LOST_DAT;
			wd17xx_clear_drq(device);
#if 1
//          w->status = seek(w, w->track, w->head, w->sector);
			if (w->status == 0)
				read_track(device);
#endif
			return;
		}

		if ((data & ~FDC_MASK_TYPE_III) == FDC_WRITE_TRK)
		{
			if (VERBOSE)
				logerror("wd17xx_command_w $%02X WRITE_TRK\n", data);

			w->command_type = TYPE_III;
			w->status &= ~STA_2_LOST_DAT;
			wd17xx_clear_drq(device);

			if (!floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
			{
				wd17xx_complete_command(device, DELAY_NOTREADY);
			}
			else
			{

				/* drive write protected? */
				if (floppy_drive_get_flag_state(w->drive,FLOPPY_DRIVE_DISK_WRITE_PROTECTED))
				{
				/* yes */
					w->status |= STA_2_WRITE_PRO;
				/* quit command */
					wd17xx_complete_command(device, DELAY_ERROR);
				}
				else
				{
				w->command = data & ~FDC_MASK_TYPE_III;
				w->data_offset = 0;
				if (w->type == WD_TYPE_1771)
					w->data_count = TRKSIZE_SD;
				else
					w->data_count = (w->density) ? TRKSIZE_DD : TRKSIZE_SD;
				wd17xx_set_drq(device);
				w->status |= STA_2_BUSY;
				w->busy_count = 0;
				}
			}
			return;
		}

		if ((data & ~FDC_MASK_TYPE_III) == FDC_READ_DAM)
		{
			if (VERBOSE)
				logerror("wd17xx_command_w $%02X READ_DAM\n", data);

			w->command_type = TYPE_III;
			w->status &= ~STA_2_LOST_DAT;
  			wd17xx_clear_drq(device);

			if (floppy_drive_get_flag_state(w->drive, FLOPPY_DRIVE_READY))
				wd17xx_read_id(device);
			else
				wd17xx_complete_command(device, DELAY_NOTREADY);

			return;
		}

		if (VERBOSE)
			logerror("wd17xx_command_w $%02X unknown\n", data);

		return;
	}

	w->status |= STA_1_BUSY;

	/* clear CRC error */
	w->status &=~STA_1_CRC_ERR;

	if ((data & ~FDC_MASK_TYPE_I) == FDC_RESTORE)
	{
		if (VERBOSE)
			logerror("wd17xx_command_w $%02X RESTORE\n", data);

		wd17xx_command_restore(device);
	}

	if ((data & ~FDC_MASK_TYPE_I) == FDC_SEEK)
	{
		UINT8 newtrack;

		if (VERBOSE)
			logerror("old track: $%02x new track: $%02x\n", w->track, w->data);
		w->command_type = TYPE_I;

		/* setup step direction */
		if (w->track < w->data)
		{
			if (VERBOSE)
				logerror("direction: +1\n");

			w->direction = 1;
		}
		else if (w->track > w->data)
		{
			if (VERBOSE)
				logerror("direction: -1\n");

			w->direction = -1;
		}

		newtrack = w->data;
		if (VERBOSE)
			logerror("wd17xx_command_w $%02X SEEK (data_reg is $%02X)\n", data, newtrack);

		/* reset busy count */
		w->busy_count = 0;

		/* keep stepping until reached track programmed */
		while (w->track != newtrack)
		{
			/* update time to simulate seek time busy signal */
			w->busy_count++;

			/* update track reg */
			w->track += w->direction;

			floppy_drive_seek(w->drive, w->direction);
		}

		/* simulate seek time busy signal */
		w->busy_count = 0;	//w->busy_count * ((data & FDC_STEP_RATE) + 1);
#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, ATTOTIME_IN_USEC(100));

	}

	if ((data & ~(FDC_STEP_UPDATE | FDC_MASK_TYPE_I)) == FDC_STEP)
	{
		if (VERBOSE)
			logerror("wd17xx_command_w $%02X STEP dir %+d\n", data, w->direction);

		w->command_type = TYPE_I;
        /* if it is a real floppy, issue a step command */
		/* simulate seek time busy signal */
		w->busy_count = 0;	//((data & FDC_STEP_RATE) + 1);

		floppy_drive_seek(w->drive, w->direction);

		if (data & FDC_STEP_UPDATE)
			w->track += w->direction;

#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, ATTOTIME_IN_USEC(100));


	}

	if ((data & ~(FDC_STEP_UPDATE | FDC_MASK_TYPE_I)) == FDC_STEP_IN)
	{
		if (VERBOSE)
			logerror("wd17xx_command_w $%02X STEP_IN\n", data);

		w->command_type = TYPE_I;
		w->direction = +1;
		/* simulate seek time busy signal */
		w->busy_count = 0;	//((data & FDC_STEP_RATE) + 1);

		floppy_drive_seek(w->drive, w->direction);

		if (data & FDC_STEP_UPDATE)
			w->track += w->direction;
#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, ATTOTIME_IN_USEC(100));

	}

	if ((data & ~(FDC_STEP_UPDATE | FDC_MASK_TYPE_I)) == FDC_STEP_OUT)
	{
		if (VERBOSE)
			logerror("wd17xx_command_w $%02X STEP_OUT\n", data);

		w->command_type = TYPE_I;
		w->direction = -1;
		/* simulate seek time busy signal */
		w->busy_count = 0;	//((data & FDC_STEP_RATE) + 1);

		/* for now only allows a single drive to be selected */
		floppy_drive_seek(w->drive, w->direction);

		if (data & FDC_STEP_UPDATE)
			w->track += w->direction;

#if 0
		wd17xx_set_intrq(device);
#endif
		wd17xx_set_busy(device, ATTOTIME_IN_USEC(100));
	}

	if (w->command_type == TYPE_I)
	{
		/* 0 is enable spin up sequence, 1 is disable spin up sequence */
		if ((data & FDC_STEP_HDLOAD)==0)
		{
			w->status |= STA_1_HD_LOADED;
			w->hld_count = 2;
		}
		else
			w->status &= ~STA_1_HD_LOADED;

		if (data & FDC_STEP_VERIFY)
		{
			/* verify seek */
			wd17xx_verify_seek(device);
		}
	}
}

/* write the FDC track register */
WRITE8_DEVICE_HANDLER( wd17xx_track_w )
{
	wd1770_state *w = get_safe_token(device);

	w->track = data;

	if (VERBOSE)
		logerror("wd17xx_track_w $%02X\n", data);
}

/* write the FDC sector register */
WRITE8_DEVICE_HANDLER( wd17xx_sector_w )
{
	wd1770_state *w = get_safe_token(device);

	w->sector = data;

	if (VERBOSE)
		logerror("wd17xx_sector_w $%02X\n", data);
}

/* write the FDC data register */
WRITE8_DEVICE_HANDLER( wd17xx_data_w )
{
	wd1770_state *w = get_safe_token(device);

	if (w->data_count > 0)
	{
		wd17xx_clear_drq(device);

		/* put byte into buffer */
		if (VERBOSE_DATA)
			logerror("wd17xx_info buffered data: $%02X at offset %d.\n", data, w->data_offset);

		w->buffer[w->data_offset++] = data;

		if (--w->data_count < 1)
		{
			if (w->command == FDC_WRITE_TRK)
				write_track(device);
			else
				wd17xx_write_sector(device);

			w->data_offset = 0;

			wd17xx_complete_command(device, DELAY_DATADONE);
		}
		else
		{
			/* yes... setup a timed data request */
			wd17xx_timed_data_request(device);
		}

	}
	else
	{
		if (VERBOSE)
			logerror("wd17xx_data_w $%02X\n", data);
	}
	w->data = data;
}

READ8_DEVICE_HANDLER( wd17xx_r )
{
	switch (offset & 0x03)
	{
	case 0: return wd17xx_status_r(device, 0);
	case 1:	return wd17xx_track_r(device, 0);
	case 2:	return wd17xx_sector_r(device, 0);
	case 3:	return wd17xx_data_r(device, 0);
	}

	return 0;
}

WRITE8_DEVICE_HANDLER( wd17xx_w )
{
	switch (offset & 0x03)
	{
	case 0: wd17xx_command_w(device, 0, data); break;
	case 1:	wd17xx_track_w(device, 0, data);   break;
	case 2: wd17xx_sector_w(device, 0, data);  break;
	case 3: wd17xx_data_w(device, 0, data);    break;
	}
}


/***************************************************************************
    MAME DEVICE INTERFACE
***************************************************************************/

static DEVICE_START( wd1770 )
{
	wd1770_state *w = get_safe_token(device);

	assert(device->static_config != NULL);

	w->intf = device->static_config;

	w->status = STA_1_TRACK0;
	w->density = DEN_MFM_LO;
	w->busy_timer = timer_alloc(device->machine, wd17xx_busy_callback, (void*)device);
	w->timer = timer_alloc(device->machine, wd17xx_misc_timer_callback, (void*)device);
	w->timer_rs = timer_alloc(device->machine, wd17xx_read_sector_callback, (void*)device);
	w->timer_ws = timer_alloc(device->machine, wd17xx_write_sector_callback, (void*)device);
	w->pause_time = 40;

	/* resolve callbacks */
	devcb_resolve_write_line(&w->out_intrq_func, &w->intf->out_intrq_func, device);
	devcb_resolve_write_line(&w->out_drq_func, &w->intf->out_drq_func, device);

	/* model specific values */
	w->type = WD_TYPE_1770;

	/* stepping rate depends on the clock */
	w->stepping_rate[0] = 6;
	w->stepping_rate[1] = 12;
	w->stepping_rate[2] = 20;
	w->stepping_rate[3] = 30;
}

static DEVICE_START( wd1771 )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_1771;
}

static DEVICE_START( wd1772 )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_1772;

	/* the 1772 has faster track stepping rates */
	w->stepping_rate[0] = 6;
	w->stepping_rate[1] = 12;
	w->stepping_rate[2] = 2;
	w->stepping_rate[3] = 3;
}

static DEVICE_START( wd1773 )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_1773;
}

static DEVICE_START( wd179x )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_179X;
}

static DEVICE_START( wd1793 )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_1793;
}

static DEVICE_START( wd2793 )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_2793;
}

static DEVICE_START( wd177x )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_177X;
}

static DEVICE_START( mb8877 )
{
	wd1770_state *w = get_safe_token(device);

	DEVICE_START_CALL(wd1770);

	w->type = WD_TYPE_MB8877;
}

static DEVICE_RESET( wd1770 )
{
	wd1770_state *w = get_safe_token(device);
	int i;

	/* set the default state of some input lines */
	w->mr = ASSERT_LINE;
	w->wprt = ASSERT_LINE;
	w->dden = ASSERT_LINE;

	for (i = 0; i < 4; i++)
	{
		if(w->intf->floppy_drive_tags[i]!=NULL) {
			const device_config *img = devtag_get_device(device->machine,w->intf->floppy_drive_tags[i]);
			if (img!=NULL) {
				floppy_drive_set_controller(img,device);
				floppy_drive_set_index_pulse_callback(img, wd17xx_index_pulse_callback);
				floppy_drive_set_rpm( img, 300.);
			}
		}
	}

	wd17xx_set_drive(device, 0);

	w->hd = 0;
	w->hld_count = 0;

	wd17xx_command_restore(device);
}

void wd17xx_reset(const device_config *device)
{
	DEVICE_RESET_CALL( wd1770 );
}


/***************************************************************************
    DEVICE GETINFO
***************************************************************************/

static const char DEVTEMPLATE_SOURCE[] = __FILE__;

#define DEVTEMPLATE_ID(p,s)				p##wd1770##s
#define DEVTEMPLATE_FEATURES			DT_HAS_START | DT_HAS_RESET
#define DEVTEMPLATE_NAME				"WD1770"
#define DEVTEMPLATE_FAMILY				"WD17xx"
#define DEVTEMPLATE_VERSION				"1.0"
#define DEVTEMPLATE_CREDITS				"Copyright MESS Team"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##wd1771##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"WD1771"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##wd1772##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"WD1772"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##wd1773##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"WD1773"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##wd179x##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"WD179x"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##wd1793##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"WD1793"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##wd2793##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"WD2793"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##wd177x##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"WD179x"
#include "devtempl.h"

#define DEVTEMPLATE_DERIVED_ID(p,s)		p##mb8877##s
#define DEVTEMPLATE_DERIVED_FEATURES	DT_HAS_START
#define DEVTEMPLATE_DERIVED_NAME		"MB8877"
#include "devtempl.h"
