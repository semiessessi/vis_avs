/*
  LICENSE
  -------
Copyright 2005 Nullsoft, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer. 

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution. 

  * Neither the name of Nullsoft nor the names of its contributors may be used to 
    endorse or promote products derived from this software without specific prior written permission. 
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR 
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND 
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR 
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT 
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
// alphachannel safe 11/21/99
#include "c_contrast.h"
#include "r_defs.h"
#include "timing.h"


#ifndef LASER

#define PUT_INT(y) data[pos]=(y)&255; data[pos+1]=(y>>8)&255; data[pos+2]=(y>>16)&255; data[pos+3]=(y>>24)&255
#define GET_INT() (data[pos]|(data[pos+1]<<8)|(data[pos+2]<<16)|(data[pos+3]<<24))
void C_THISCLASS::load_config(unsigned char *data, int len)
{
	int pos=0;
	if (len-pos >= 4) { enabled=GET_INT(); pos+=4; }
	if (len-pos >= 4) { color_clip=GET_INT(); pos+=4; }
	if (len-pos >= 4) { color_clip_out=GET_INT(); pos+=4; }
  else color_clip_out=color_clip;
	if (len-pos >= 4) { color_dist=GET_INT(); pos+=4; }
}
int  C_THISCLASS::save_config(unsigned char *data)
{
	int pos=0;
	PUT_INT(enabled); pos+=4;
  PUT_INT(color_clip); pos+=4;
  PUT_INT(color_clip_out); pos+=4;
  PUT_INT(color_dist); pos+=4;
	return pos;
}

C_THISCLASS::C_THISCLASS()
{
  enabled=1;
  color_clip=RGB(32,32,32);
  color_clip_out=RGB(32,32,32);
  color_dist=10;
}

C_THISCLASS::~C_THISCLASS()
{
}
	
int C_THISCLASS::render(char[2][2][576], int isBeat, int *framebuffer, int*, int w, int h)
{
  if (!enabled) return 0;
  if (isBeat&0x80000000) return 0;

  int *f = framebuffer;
  int fs_r,fs_g,fs_b;
  int x=w*h;
  int l=color_dist*2;


  l=l*l;

  fs_b=(color_clip&0xff0000);
  fs_g=(color_clip&0xff00);
  fs_r=(color_clip&0xff);

  if (enabled==1) while (x--)
  {
    int a=f[0];
    if ((a&0xff) <= fs_r && (a&0xff00) <= fs_g && (a&0xff0000) <= fs_b)
      f[0]=(a&0xff000000)|color_clip_out;
    f++;
  }
  else if (enabled==2) while (x--)
  {
    int a=f[0];
    if ((a&0xff) >= fs_r && (a&0xff00) >= fs_g && (a&0xff0000) >= fs_b)
      f[0]=(a&0xff000000)|color_clip_out;
    f++;
  }
  else 
  {
    fs_b>>=16;
    fs_g>>=8;
    while (x--)
    {
      int a=f[0];
      int r=a&255;
      int g=(a>>8)&255;
      int b=(a>>16)&255;
      r-=fs_r; g-=fs_g; b-=fs_b;
      if (r*r+g*g+b*b <= l) f[0]=(a&0xff000000)|color_clip_out;
      f++;
    }
  }
  return 0;
}

C_RBASE *R_ColorClip(char *desc)
{
	if (desc) { strcpy(desc,MOD_NAME); return NULL; }
	return (C_RBASE *) new C_THISCLASS();
}

#else
C_RBASE *R_ColorClip(char *desc) { return NULL; }
#endif
