"""Read-only import of the user's PS1 MODE2/2352 + CDDA CUE/BIN dump.
Outputs are isolated under GameData/PS1; DOS assets are never overwritten.
"""
from pathlib import Path
import argparse,array,hashlib,json,re,struct,sys,wave
ROOT=Path(__file__).resolve().parents[1]
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--audio',action='store_true');args=parser.parse_args()
 cues=sorted((ROOT/'Alien Trilogy PS1').glob('*.cue'))
 if len(cues)!=1:raise ValueError('Expected one PS1 cue sheet')
 cue=cues[0];tracks=[];filename=None
 for line in cue.read_text().splitlines():
  if match:=re.match(r'\s*FILE "([^"]+)" BINARY',line):filename=match[1]
  elif match:=re.match(r'\s*TRACK (\d+) (\S+)',line):tracks.append({'track':int(match[1]),'mode':match[2],'file':filename,'index01':0})
  elif match:=re.match(r'\s*INDEX 01 (\d+):(\d+):(\d+)',line):tracks[-1]['index01']=(int(match[1])*60+int(match[2]))*75+int(match[3])
 output=ROOT/'GameData/PS1';output.mkdir(parents=True,exist_ok=True)
 data=next(t for t in tracks if t['mode']=='MODE2/2352');source=(cue.parent/data['file']).resolve()
 if source.parent!=cue.parent.resolve():raise ValueError('Track path escaped dump directory')
 entries=[];seen=set()
 with source.open('rb') as disc:
  def read(lba,length):
   out=bytearray()
   while length:
    disc.seek((lba+data['index01'])*2352);sector=disc.read(2352)
    if len(sector)!=2352 or sector[15]!=2 or sector[18]&32:raise ValueError('Unsupported/truncated data sector')
    count=min(2048,length);out+=sector[24:24+count];lba+=1;length-=count
   return bytes(out)
  pvd=read(16,2048)
  if pvd[1:6]!=b'CD001':raise ValueError('Missing ISO9660 primary descriptor')
  def scan(lba,length,prefix=''):
   if lba in seen:return
   seen.add(lba);buf=read(lba,length);p=0
   while p<len(buf):
    size=buf[p]
    if not size:p=(p//2048+1)*2048;continue
    record=buf[p:p+size];p+=size
    if len(record)<34:raise ValueError('Truncated directory record')
    name=record[33:33+record[32]]
    if name in (b'\0',b'\1'):continue
    name=name.decode('ascii').split(';')[0]
    if name in ('.','..') or '/' in name or '\\' in name:raise ValueError('Unsafe ISO path')
    path=prefix+name;sector=int.from_bytes(record[2:6],'little');length=int.from_bytes(record[10:14],'little');directory=bool(record[25]&2)
    entry={'path':path,'sector':sector,'size':length,'directory':directory};entries.append(entry)
    if directory:scan(sector,length,path+'/')
  record=pvd[156:190];scan(int.from_bytes(record[2:6],'little'),int.from_bytes(record[10:14],'little'))
  imported=[]
  for e in entries:
   if e['directory']:continue
   path=e['path']
   if not(path.startswith(('GFX/','LANGUAGE/','SECT','SFX/','SFX2/')) or '/' not in path):continue
   if path.endswith(('.STR','.XA')):continue
   content=read(e['sector'],e['size']);dest=output/'CD'/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(content);imported.append({**e,'sha256':hashlib.sha256(content).hexdigest()})
 audio=[]
 if args.audio:
  for t in tracks:
   if t['mode']!='AUDIO':continue
   path=(cue.parent/t['file']).resolve()
   if path.parent!=cue.parent.resolve():raise ValueError('Unsafe audio track path')
   with path.open('rb') as f:
    f.seek(t['index01']*2352);raw=f.read()
   if len(raw)%4:raise ValueError('Misaligned CDDA frames')
   # Pick the byte order by measured adjacent-sample continuity. Store both
   # measurements rather than assuming a BIN/WAV byte-order convention.
   sample=raw[:min(len(raw),44100*4*3)];scores=[]
   for endian in ('little','big'):
    values=[int.from_bytes(sample[i:i+2],endian,signed=True) for i in range(0,len(sample),2)]
    rms=sum(v*v for v in values)/max(1,len(values));delta=sum((values[i]-values[i-2])**2 for i in range(2,len(values)))/max(1,len(values)-2);scores.append(delta/max(1,rms))
   endian='little' if scores[0]<=scores[1] else 'big'
   if endian=='big':a=array.array('h');a.frombytes(raw);a.byteswap();raw=a.tobytes()
   dest=output/'Music'/f'track{t["track"]:02}.wav';dest.parent.mkdir(parents=True,exist_ok=True)
   with wave.open(str(dest),'wb') as wav:wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(44100);wav.writeframes(raw)
   audio.append({**t,'seconds':len(raw)/176400,'sample_byte_order':endian,'continuity_le_be':scores,'output':str(dest.relative_to(ROOT))})
 manifest={'volume':pvd[40:72].decode().strip(),'cue':str(cue.relative_to(ROOT)),'imported':imported,'tracks':tracks,'audio':audio}
 (ROOT/'decompilation/reports/ps1_import.json').write_text(json.dumps(manifest,indent=2));print(f'Imported {len(imported)} PS1 files and {len(audio)} CDDA tracks')
if __name__=='__main__':main()
