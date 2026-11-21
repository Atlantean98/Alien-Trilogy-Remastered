# SPDX-License-Identifier: LGPL-2.1-or-later
# PS1 MDEC still decoder. MPEG VLC tables and decoding design adapted from FFmpeg.
# Original FFmpeg MDEC: Copyright (c) 2003 Michael Niedermayer.
# FFmpeg reference sources and COPYING.LGPLv2.1 accompany this module.
import math,struct,functools
QUANT=[8, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83]
VLC=[(3, 2), (4, 4), (5, 5), (6, 7), (38, 8), (33, 8), (10, 10), (29, 12), (24, 12), (19, 12), (16, 12), (26, 13), (25, 13), (24, 13), (23, 13), (31, 14), (30, 14), (29, 14), (28, 14), (27, 14), (26, 14), (25, 14), (24, 14), (23, 14), (22, 14), (21, 14), (20, 14), (19, 14), (18, 14), (17, 14), (16, 14), (24, 15), (23, 15), (22, 15), (21, 15), (20, 15), (19, 15), (18, 15), (17, 15), (16, 15), (3, 3), (6, 6), (37, 8), (12, 10), (27, 12), (22, 13), (21, 13), (31, 15), (30, 15), (29, 15), (28, 15), (27, 15), (26, 15), (25, 15), (19, 16), (18, 16), (17, 16), (16, 16), (5, 4), (4, 7), (11, 10), (20, 12), (20, 13), (7, 5), (36, 8), (28, 12), (19, 13), (6, 5), (15, 10), (18, 12), (7, 6), (9, 10), (18, 13), (5, 6), (30, 12), (20, 16), (4, 6), (21, 12), (7, 7), (17, 12), (5, 7), (17, 13), (39, 8), (16, 13), (35, 8), (26, 16), (34, 8), (25, 16), (32, 8), (24, 16), (14, 10), (23, 16), (13, 10), (22, 16), (8, 10), (21, 16), (31, 12), (26, 12), (25, 12), (23, 12), (22, 12), (31, 13), (30, 13), (29, 13), (28, 13), (27, 13), (31, 16), (30, 16), (29, 16), (28, 16), (27, 16), (1, 6), (2, 2)]
LEVEL=[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 1, 2, 3, 4, 5, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
RUN=[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31]

C=[[.5*(1/math.sqrt(2) if u==0 else 1)*math.cos((2*x+1)*u*math.pi/16) for u in range(8)] for x in range(8)]
ZIG=[]
for diagonal in range(15):
 rows=range(max(0,diagonal-7),min(7,diagonal)+1)
 if diagonal%2==0:rows=reversed(list(rows))
 ZIG.extend(row*8+diagonal-row for row in rows)
DECODE={(length,code):i for i,(code,length) in enumerate(VLC)}
class Bits:
 def __init__(self,data):
  data=bytearray(data)
  for i in range(0,len(data)-1,2):data[i],data[i+1]=data[i+1],data[i]
  self.n=len(data)*8;self.bits=int.from_bytes(data,'big');self.pos=64
 def get(self,n):
  if self.pos+n>self.n:raise ValueError('Truncated MDEC bitstream')
  self.pos+=n;return (self.bits>>(self.n-self.pos))&((1<<n)-1)
 def signed(self,n):
  x=self.get(n);return x-(1<<n) if x&(1<<(n-1)) else x
@functools.lru_cache(maxsize=4096)
def idct(coeff):
 temp=[[sum(coeff[v*8+u]*C[x][u] for u in range(8)) for x in range(8)] for v in range(8)]
 return [max(0,min(255,int(sum(C[y][v]*temp[v][x] for v in range(8))+.5))) for y in range(8) for x in range(8)]
def decode(data,width=320,height=240):
 if len(data)<8:raise ValueError('Missing MDEC header')
 _,magic,q,version=struct.unpack('<4H',data[:8])
 if magic!=0x3800 or version!=2:raise ValueError('Unsupported MDEC still version')
 bits=Bits(data);Y=bytearray(width*height);U=bytearray(width*height//4);V=bytearray(len(U))
 def block():
  out=[0]*64;out[0]=bits.signed(10)*2+1024;i=0
  while True:
   code=0
   for length in range(1,17):
    code=(code<<1)|bits.get(1);token=DECODE.get((length,code))
    if token is not None:break
   else:raise ValueError('Invalid MDEC VLC')
   if token==len(VLC)-1:break
   if token==len(VLC)-2:
    run=bits.get(6)+1;level=bits.signed(10);i+=run
    if i>=64:raise ValueError('MDEC coefficient outside block')
    value=((abs(level)*q*QUANT[ZIG[i]]>>3)-1)|1
    if level<0:value=-value
   else:
    i+=RUN[token]+1
    if i>=64:raise ValueError('MDEC coefficient outside block')
    value=LEVEL[token]*q*QUANT[ZIG[i]]>>3
    if bits.get(1):value=-value
   out[ZIG[i]]=value
  return idct(tuple(out))
 for mx in range(width//16):
  for my in range(height//16):
   for n in (5,4,0,1,2,3):
    pixels=block()
    if n<4:dst=Y;stride=width;x=mx*16+(n&1)*8;y=my*16+(n//2)*8
    else:dst=U if n==4 else V;stride=width//2;x=mx*8;y=my*8
    for row in range(8):dst[(y+row)*stride+x:(y+row)*stride+x+8]=bytes(pixels[row*8:row*8+8])
 rgba=bytearray(width*height*4)
 clamp=lambda n:max(0,min(255,round(n)))
 for y in range(height):
  for x in range(width):
   n=y*width+x;chroma=y//2*(width//2)+x//2;u=U[chroma]-128;v=V[chroma]-128;l=Y[n]
   rgba[n*4:n*4+4]=bytes((clamp(l+1.772*u),clamp(l-.344136*u-.714136*v),clamp(l+1.402*v),255))
 return b'RGBA'+struct.pack('<II',width,height)+rgba
if __name__=='__main__':
 import sys
 from pathlib import Path
 Path(sys.argv[2]).write_bytes(decode(Path(sys.argv[1]).read_bytes()))
