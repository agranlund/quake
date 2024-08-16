#ifndef __VIDEO_NOVA_H__
#define __VIDEO_NOVA_H__

extern long video_nova_exist();
extern long video_nova_init(unsigned short mwidth, unsigned short mheight, unsigned short width, unsigned short height);
extern void video_nova_shutdown(void);
extern void video_nova_setcolors(unsigned char* pal, unsigned short start, unsigned short count);
extern void video_nova_blit(char* buf, short x, short y, short w, short h);

#endif //__VIDEO_NOVA_H__


