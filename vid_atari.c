/*
 * vid_atari.c -- video handling for Atari Falcon 060
 *
 * Copyright (c) 2006 Miro Kropacek; miro.kropacek@gmail.com
 * 
 * This file is part of the Atari Quake project, 3D shooter game by ID Software,
 * for Atari Falcon 060 computers.
 *
 * Atari Quake is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari Quake is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari Quake; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <mint/falcon.h>

#include "quakedef.h"
#include "d_local.h"

#include "vid_atari_asm.h"

//#define NO_ATARI_VIDEO
//#define NO_ATARI_NOVA

extern viddef_t	vid;				// global video state

#define	BASEWIDTH	320
#define	BASEHEIGHT	200

//byte	vid_buffer[BASEWIDTH*BASEHEIGHT];
//short	zbuffer[BASEWIDTH*BASEHEIGHT];
//byte	surfcache[256*1024];
//byte	surfcache[4*1024*1024];

char* screen1 = NULL;	// physical screen

static qboolean isVideoInited = false;
static qboolean isSvPresent = false;
static qboolean isNovaPresent = false;

static char* screen2 = NULL;	// logical screen
static char* screen3 = NULL;	// temp screen
#define screen	screen2

static qboolean vga = false;
static unsigned int screen_width = 0;
static unsigned int screen_height = 0;
static unsigned int screen_offset = 0;
static unsigned int screen_pitch = 0;

unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];

static qboolean CheckCookie( long cookie, long* value )
{
	typedef struct
	{
		long cookie;
		long value;
	} COOKIE;
	COOKIE* pCookie;

	pCookie = *( (COOKIE **)0x5A0L );

	if( pCookie != NULL)
	{
		do
		{
			if( pCookie->cookie == cookie )
			{
                if ( value )
                {
                    *value = pCookie->value;
                }
                return true;
			}

		} while( (pCookie++)->cookie != 0L );
	}

    return false;
}

static long CheckSvPresence( void )
{
    isSvPresent = CheckCookie( 0x53757056 /*'SupV'*/, NULL );
    return (long) isSvPresent;
}

void	VID_SetPalette (unsigned char *palette)
{
#ifndef NO_ATARI_VIDEO
    if ( isNovaPresent )
    {
        video_nova_setcolors( palette, 0, 256 );
    }
    else
    {
        video_atari_set_palette( palette );
    }
#endif
}

void	VID_ShiftPalette (unsigned char *palette)
{
	VID_SetPalette( palette );
}

void	VID_Init (unsigned char *palette)
{
	Supexec( CheckSvPresence );
    isNovaPresent = video_nova_exist();

    if ( !isNovaPresent )   //todo: check for Falcon instead
    {
	    vga = VgetMonitor() == MON_VGA;
    }

	vid.maxwarpwidth = vid.width = vid.conwidth = BASEWIDTH;
	vid.maxwarpheight = vid.height = vid.conheight = BASEHEIGHT;
	vid.aspect = 1.0;
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.buffer = vid.conbuffer = (byte*)malloc( BASEWIDTH*BASEHEIGHT + 15 );	//vid_buffer;
	vid.rowbytes = vid.conrowbytes = BASEWIDTH * sizeof( pixel_t );
	
	d_pzbuffer = (short*)malloc( BASEWIDTH*BASEHEIGHT * sizeof(short) + 15 );	//zbuffer;
	
	byte* surfcache = (byte*)malloc( 4*1024*1024 + 15 );
	
	if( vid.buffer == NULL || d_pzbuffer == NULL || surfcache == NULL )
	{
		Sys_Error( "Not enough memory to buffers!\n" );
		return;
	}
	
	// we can't release these buffers anymore
	vid.buffer = vid.conbuffer = (byte*)( ( (long)vid.buffer + 15 ) & 0xfffffff0 );
	d_pzbuffer = (short*)( ( (long)d_pzbuffer + 15 ) & 0xfffffff0 );
	surfcache = (byte*)( ( (long)surfcache + 15 ) & 0xfffffff0 );
	
	D_InitCaches (surfcache, /*sizeof(surfcache)*/ 4*1024*1024 );

	#ifndef NO_ATARI_VIDEO
    if (isNovaPresent )
    {
        if( !video_nova_init( 320, 240, 320, 200 ) )
        {
    		Sys_Error( "Failed to initialize Nova!\n" );
            return;
        }
        isVideoInited = true;
    }
    else
    {
    	// alloc triplebuffer
        screen_width = vid.width;
        screen_height = vga ? 240 : vid.height;
        screen_pitch = 0;
        screen_offset =  ( ( ( screen_height - vid.height ) / 2 ) * ( screen_pitch ? screen_pitch : vid.rowbytes ) ) + ( ( ( screen_width - vid.width) * sizeof( pixel_t ) ) / 2 );

        screen1 = (char*)Mxalloc( 3 * ( screen_width * screen_height * sizeof( pixel_t ) ) + 15, MX_STRAM );
        if( screen1 == NULL )
        {
            Sys_Error( "Not enough memory to allocate screens!\n" );
            return;
        }
        
        // align on 16 bytes & assign the rest of pointers
        screen1 = (char*)( ( (long)screen1 + 15 ) & 0xfffffff0 );
        screen2 = (char*)( (long)screen1 + screen_width * screen_height * sizeof( pixel_t ) );
        screen3 = (char*)( (long)screen2 + screen_width * screen_height * sizeof( pixel_t ) );

        Q_memset( screen1, 0, screen_width * screen_height * sizeof( pixel_t ) );
        Q_memset( screen2, 0, screen_width * screen_height * sizeof( pixel_t ) );
        Q_memset( screen3, 0, screen_width * screen_height * sizeof( pixel_t ) );
	
        video_atari_init( screen1 );
        if( vga )
        {
            if( isSvPresent )
            {
                #define BPS8C 0x0007
                VsetMode( BPS8C | COL40 | VGA | VERTFLAG );
            }
            else
            {
                VsetMode( BPS8 | COL40 | VGA | VERTFLAG );
            }
        }
        else
        {
            VsetMode( BPS8 | COL40 | TV | PAL );
        }
        isVideoInited = true;
    }
	#endif
}

void	VID_Shutdown (void)
{
	if( isVideoInited == true )
	{
		#ifndef NO_ATARI_VIDEO
        if ( isNovaPresent )
        {
            video_nova_shutdown();
        }
        else
        {
		    video_atari_shutdown();
        }
		#endif
	}
}

void	VID_Update (vrect_t *rects)
{
    static unsigned int framecount = 0;
	char* temp, *tempsrc;
    short i;

	#ifndef NO_ATARI_VIDEO
    if ( isNovaPresent )
    {
        while (rects)
        {
            video_nova_blit( vid.buffer, rects->x, rects->y, rects->width, rects->height );
            rects = rects->pnext;
        }
    }
    else
    {
        if( isSvPresent && vga )
        {
            temp = screen + 0xA0000000;
            video_atari_move16( vid.buffer, temp + screen_offset, vid.width * vid.height * sizeof( pixel_t ) );
        }
        else
        {
            video_atari_c2p( vid.buffer, screen + screen_offset, vid.width * vid.height * sizeof( pixel_t ) );
        }

        // cycle 3 screens
        temp	= screen1;
        screen1	= screen2;	// now physical screen = logical screen (i.e. "screen")
        screen2	= screen3;
        screen3 = temp;
    }
	#endif
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
	int i, j;
	byte* buffer;
	
	if( isVideoInited == true )
	{
		buffer = (byte*)vid.buffer;
		buffer += vid.rowbytes * y + x;
		
		for( j = 0; j < height; j++ )
		{
			for( i = 0; i < width; i++ )
			{
				*buffer++ = *pbitmap++;
			}
			
			buffer += ( vid.width - width );
		}
		
		VID_Update( NULL );
	}
}


/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
	if( isVideoInited == true )
	{
		//VID_Update( NULL );
	}
}
