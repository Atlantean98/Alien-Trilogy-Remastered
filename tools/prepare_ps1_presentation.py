"""Decode PS1 MDEC stills using FFmpeg and export the verified music lookup."""
from pathlib import Path
import hashlib,json,re,shutil,struct,subprocess
ROOT=Path(__file__).resolve().parents[1]
def chunk(tag,data):return tag+struct.pack('<I',len(data))+data+(b'\0' if len(data)&1 else b'')
def listing(tag,data):return chunk(b'LIST',tag+data)
def main():
 exe=ROOT/'GameData/PS1/CD/SLUS_000.07';data=exe.read_bytes();digest=hashlib.sha256(data).hexdigest()
 if data[:8]!=b'PS-X EXE':raise ValueError('Unsupported PS1 executable')
 offset=0x63974;table=list(struct.unpack_from('<35H',data,offset))
 # MIPS code at 0x8003f784 loads LH a0,0x3174(at), after indexing
 # the 0x80070000 base by the logical mission number * 2.
 if struct.unpack_from('<I',data,0x2ff84)[0]!=0x84243174:raise ValueError('Music lookup reference differs from analyzed USA executable')
 if not all(2<=t<=26 and t%2==0 for t in table):raise ValueError('Invalid PS1 level-music lookup')
 text=(ROOT/'GameData/PS1/CD/LANGUAGE/MISSIONU.TXT').read_text();codes=[m.replace('.','') for m in re.findall(r'^(\d\.\d\.\d) ',text,re.M)]
 if len(codes)!=35:raise ValueError('Mission order differs from original lookup')
 out=ROOT/'GameData/PS1';(out/'level_music.txt').write_text(''.join(f'{code} {track}\n' for code,track in zip(codes,table)))
 decoded=out/'Decoded';decoded.mkdir(exist_ok=True);ffmpeg=shutil.which('ffmpeg')
 if not ffmpeg:raise ValueError('FFmpeg is required for MDEC still decoding')
 images=[]
 for src in sorted((out/'CD/GFX').glob('*.BS')):
  packet=src.read_bytes();width,height=320,240
  if packet[2:4]!=b'\x00\x38':raise ValueError('Unexpected MDEC still header')
  avih=struct.pack('<14I',40000,0,0,0,1,0,1,len(packet),width,height,0,0,0,0)
  stream=struct.pack('<4s4sIHH8I4h',b'vids',b'MDEC',0,0,0,0,1,25,0,1,len(packet),0xffffffff,0,0,0,width,height)
  fmt=struct.pack('<IiiHH4sI4i',40,width,height,1,24,b'MDEC',len(packet),0,0,0,0)
  avi=decoded/(src.stem+'.avi');avi.write_bytes(chunk(b'RIFF',b'AVI '+listing(b'hdrl',chunk(b'avih',avih)+listing(b'strl',chunk(b'strh',stream)+chunk(b'strf',fmt)))+listing(b'movi',chunk(b'00dc',packet))))
  raw=decoded/(src.stem+'.bgra');subprocess.run([ffmpeg,'-v','error','-y','-c:v','mdec','-i',str(avi),'-frames:v','1','-pix_fmt','bgra','-f','rawvideo',str(raw)],check=True)
  pixels=raw.read_bytes()
  if len(pixels)!=width*height*4:raise ValueError('Unexpected decoded image extent')
  dest=decoded/(src.stem+'.rgba');dest.write_bytes(b'RGBA'+struct.pack('<II',width,height)+pixels)
  subprocess.run([ffmpeg,'-v','error','-y','-c:v','mdec','-i',str(avi),'-frames:v','1',str(decoded/(src.stem+'.png'))],check=True)
  images.append({'source':str(src.relative_to(ROOT)),'width':width,'height':height,'sha256':hashlib.sha256(packet).hexdigest()})
 report={'status':'prepared','exe_sha256':digest,'music_table_file_offset':hex(offset),'music_table_runtime_address':'0x80073174','music_reference_instruction':'0x8003f784: lh a0,0x3174(at)','music_mapping':dict(zip(codes,table)),'images':images,'decoder_source':'https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/mdec.c'}
 (ROOT/'decompilation/reports/ps1_presentation_import.json').write_text(json.dumps(report,indent=2));print(f'Prepared {len(images)} PS1 stills and {len(table)} verified level music assignments')
if __name__=='__main__':main()
