// vid_atari_nova.c - very simple Nova helper

#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <mint/cookie.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vid_atari_nova.h"

#if 0
#include <stdio.h>
#define dbg_printf(...)  { printf(__VA_ARGS__); }
#else
#define dbg_printf(...)  { }
#endif

// -----------------------------------------------------------------

// nova resolution descriptor
typedef struct {
	unsigned char	name[33];
	unsigned char	dummy1;
	unsigned short	mode;		// 2 = 8bpp
	unsigned short	pitch;		// bpp>=8: bytes/line
	unsigned short	planes;		// Bits per pixel
	unsigned short	colors;		// Number of colours
	unsigned short	hc_mode;
	unsigned short	max_x;		// Max x,y coordinates, values-1
	unsigned short	max_y;
	unsigned short	real_x;		// Real max x,y coordinates, values-1
	unsigned short	real_y;
	unsigned short	freq;
	unsigned char	freq2;
	unsigned char	low_res;
	unsigned char	r_3c2;
	unsigned char	r_3d4[25];
	unsigned char	extended[3];
	unsigned char	dummy2;
} nova_resolution_t;

// nova cookie interface
typedef struct {
	unsigned char	version[4];
	unsigned char	resolution;	    // current resolution number
	unsigned char	blnk_time;	    // Time before blanking
	unsigned char	ms_speed;       // Mouse speed
	unsigned char	old_res;
	void			(*p_chres)(nova_resolution_t *nova_res, unsigned long offset);
	unsigned short	mode;
	unsigned short	pitch;
	unsigned short	planes;
	unsigned short	colours;
	unsigned short	hc;
	unsigned short	max_x, max_y;
	unsigned short	rmn_x, rmx_x;
	unsigned short	rmn_y, rmx_y;
	unsigned short	v_top, v_bottom;
	unsigned short	v_left, v_right;
	void			(*p_setcol)(unsigned short index, unsigned char *colors);	
	void			(*chng_vrt)(unsigned short x, unsigned short y);
	void			(*inst_xbios)(unsigned short on);
	void			(*pic_on)(unsigned short on);
	void			(*chng_pos)(nova_resolution_t *nova_res, unsigned short direction, unsigned short offset);
	void			(*p_setscr)(void *adr);	    // Pointer to routine to change screen address
	void			*base;		                // Address of screen #0 in video RAM
	void			*scr_base;	                // Adress of video RAM
	unsigned short	scrn_cnt;	                // Number of possible screens in video RAM
	unsigned long	scrn_sze;	                // Size of a screen
	void			*reg_base;	                // Video card I/O registers base
	void			(*p_vsync)(void);	        // Pointer to routine to vsync
	unsigned char	name[36];	                // Video mode name
	unsigned long	mem_size;	                // Global size of video memory
} nova_xcb_t;

// -----------------------------------------------------------------

static nova_xcb_t* nova = 0;
static nova_resolution_t* modes = 0;
static unsigned int num_modes = 0;

static unsigned short sav_pal[256 * 3];
static unsigned char sav_res = 0;
static unsigned int sav_scrsize = 0;
static char* sav_scrbuf = 0;
static char* sav_scrptr = 0;

static unsigned char cur_res = 0;
static unsigned int cur_scrsize = 0;
static unsigned int cur_scroffs = 0;
static char* cur_scrbuf = 0;

static unsigned short src_width = 0;
static unsigned short src_height = 0;
static unsigned short mon_width = 0;
static unsigned short mon_height = 0;


// -----------------------------------------------------------------

static long NovaGetCookie() {
    long result = 0;
    if (Getcookie(C_NOVA, &result) == C_FOUND) {
        return result;
    }
    return 0;
}

static long NovaLoadModes() {
	int handle, length;
    char filename[32] = "c:\\auto\\sta_vdi.bib";
    filename[0] = 'a'+ *((volatile unsigned short *)0x446);

	handle = open(filename, 0);
	if (handle < 0) {
        return 0;
    }

	length = lseek(handle, 0, SEEK_END);
	lseek(handle, 0, SEEK_SET);
    modes = malloc(length);
    if (modes == NULL) {
        return 0;
    }

	read(handle, modes, length);
	close(handle);

    num_modes = length / sizeof(nova_resolution_t);
    return num_modes;
}

static unsigned char NovaFindMode(unsigned short width, unsigned short height, unsigned short accept_larger)
{
    int i, bestw, besth, diffw, diffh;
    short targetw = width - 1;
    short targeth = height - 1;
    unsigned char bestm = num_modes;
    bestw = besth = 1000000;
    for (i = 0; i < num_modes; i++)
    {
        if ((modes[i].mode == 2) && (modes[i].real_x >= targetw) && (modes[i].real_y >= targeth))
        {
            diffw = modes[i].real_x - targetw;
            diffh = modes[i].real_y - targeth;
            if (diffw < bestw)            
            {
                bestm = (unsigned char) i;
                bestw = diffw;
                besth = diffh;
            }
            else if ((diffw == bestw) && (diffh < besth))
            {
                bestm = (unsigned char) i;
                besth = diffh;
            }
        }
    }
    return (accept_larger || ((bestw == 0) && (besth == 0))) ? bestm : num_modes;
}

static long NovaInit()
{
    // get nova details
    nova = (nova_xcb_t*) NovaGetCookie();
    if (!nova) {
        dbg_printf("Failed to get Nova cookie!\r\n");
        return 0;
    }

    if (!NovaLoadModes()) {
        dbg_printf("Failed to load Nova modes!");
        nova = 0;
        return 0;
    }

    // try prefered resolution
    cur_res = NovaFindMode(mon_width, mon_height, 0);

    // accept any size larger or equal to active area
    if (cur_res == num_modes)
        cur_res = NovaFindMode(src_width, src_height, 1);

    if (cur_res >= num_modes)
    {
        dbg_printf("Failed to find suitable Nova mode!");
        free(modes);
        modes = 0;
        num_modes = 0;
        nova = 0;
        return 0;
    }

    // cursor and mouse off
	__asm__ __volatile__ (
        "movew  #0,-(sp)\n\t"
        "movew  #0,-(sp)\n\t"
        "movew  #21,-(sp)\n\t"
        "trap   #14\n\t"
        "addql  #6,sp\n\t"
        "dc.w   0xA00A\n\t"
        : : : "d0", "d1", "d2", "a0", "a1", "a2", "cc", "memory"
	);

    cur_scrbuf = nova->base;
    cur_scrsize = (modes[cur_res].real_x + 1) * (modes[cur_res].real_y + 1);
    cur_scroffs = (((modes[cur_res].real_x + 1) - src_width) / 2) + ((((modes[cur_res].real_y + 1) - src_height) / 2) * modes[cur_res].pitch);
    dbg_printf("Nova mode: %d : %dx%d : %08x\n", cur_res, modes[cur_res].real_x+1, modes[cur_res].real_y+1, cur_scrbuf);

    // save current state
    sav_res = nova->resolution;

    sav_scrptr = nova->base;
    sav_scrsize = nova->scrn_sze;
    sav_scrbuf = malloc(sav_scrsize);
    memcpy(sav_scrbuf, sav_scrptr, sav_scrsize);
    // todo: sav_pal

    // change resolution
	__asm__ __volatile__ (
        "moveql	#0,%%d0\n\t"
		"movel	%0,%%a0\n\t"
		"movel	%1,%%a1\n\t"
		"jsr	%%a1@"
		: :	"g"(&modes[cur_res]), "g"(nova->p_chres) : "d0", "d1", "d2", "a0", "a1", "cc", "memory"
    );

    // vsync
	__asm__ __volatile__ (
		"movel	%0,%%a0\n\t"
		"jsr	%%a0@"
		: : "g"(nova->p_vsync) : "d0", "d1", "d2", "a0", "a1", "cc", "memory"
	);

    // set screenptr
	__asm__ __volatile__ (
        "movel  %0,%%a0\n\t"
		"movel	%1,%%a1\n\t"
		"jsr	%%a1@"
		: : "g"(cur_scrbuf), "g"(nova->p_setscr) : "d0", "d1", "d2", "a0", "a1", "cc", "memory"
	);

    // vsync
	__asm__ __volatile__ (
		"movel	%0,%%a0\n\t"
		"jsr	%%a0@"
		: : "g"(nova->p_vsync) : "d0", "d1", "d2", "a0", "a1", "cc", "memory"
	);

    memset(cur_scrbuf, 0, cur_scrsize);
    return (long) (cur_scrbuf + cur_scroffs);
}

static long NovaRelease()
{
    if (nova == 0)
        return 1;

    // todo: sav_res
    // todo: sav_scr
    // todo: sav_pal

    if (sav_scrbuf) {
        free(sav_scrbuf);
        sav_scrbuf = 0;
    }

    if (modes) {
        free(modes);
        modes = 0;
        num_modes = 0;
    }

    return 1;
}

static void NovaSetColor(int idx, unsigned char* col)
{
    if (!nova)
        return;

	void* sp = (void *)Super(NULL);
	__asm__ __volatile__ (
		"movel	%0,%%d0\n\t"
		"movel	%1,%%a0\n\t"
		"movel	%2,%%a1\n\t"
		"jsr	%%a1@"
		: : "g"(idx), "g"(col), "g"(nova->p_setcol) : "d0", "d1", "d2", "a0", "a1", "cc", "memory"
	);
	SuperToUser(sp);
}

// -----------------------------------------------------------------

long video_nova_exist() {
    return Supexec(NovaGetCookie);
}

long video_nova_init(unsigned short mwidth, unsigned short mheight, unsigned short width, unsigned short height) {
    src_width = width;
    src_height = height;
    mon_width = mwidth;
    mon_height = mheight;
    return Supexec(NovaInit);
};

void video_nova_shutdown() {
    Supexec(NovaRelease);
}

void video_nova_setcolors(unsigned char* pal, unsigned short start, unsigned short count) {
    short i;
    if (nova) {
        for (i = start; i < (start + count); i++) {
            NovaSetColor(i, &pal[i*3]);
        }
    }
}

void video_nova_blit(char* buf, short x, short y, short w, short h) {
    if (nova) {
        short pitch = modes[cur_res].pitch;
        char* src = buf + x + (y * src_width);
        char* dst = cur_scrbuf + cur_scroffs + x + (y * pitch);
        short i;

        if (w == pitch)
        {
            memcpy(dst, src, w * h);
        }
        else
        {
            for (i = 0; i < h; i++)
            {
                memcpy(dst, src, w);
                dst += pitch;
                src += w;
            }
        }
    }
}



