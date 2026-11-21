"""Extract original PS1 permanent and level SFX banks and decode SPU ADPCM at authored pitch."""
from pathlib import Path
import re,struct,wave,array,json,hashlib
ROOT=Path(__file__).resolve().parents[1]
NAMES={5:'pulse_reload',6:'pulse_fire',7:'pulse_grenade',8:'grenade_blast',9:'seismic_blast',10:'flame_start_stop',11:'flame_loop',12:'pistol_reload',13:'pistol_fire',14:'pistol_clip',15:'shotgun_reload',16:'shotgun_fire',17:'shotgun_clip',18:'smartgun_reload',19:'smartgun_fire',20:'smartgun_clip'}
def banks_from_disc():
 cue=next((ROOT/'Alien Trilogy PS1').glob('*.cue'));filename=re.search(r'FILE "([^"]+)" BINARY',cue.read_text())[1];path=(cue.parent/filename).resolve()
 if path.parent!=cue.parent.resolve():raise ValueError('Track outside supplied dump')
 with path.open('rb') as disc:
  def read(lba,size):
   out=bytearray()
   while size:
    disc.seek(lba*2352);sector=disc.read(2352)
    if len(sector)!=2352 or sector[15]!=2 or sector[18]&32:raise ValueError('Invalid data sector')
    n=min(size,2048);out+=sector[24:24+n];lba+=1;size-=n
   return bytes(out)
  pvd=read(16,2048)
  if pvd[1:6]!=b'CD001':raise ValueError('ISO9660 missing')
  seen=set();found={}
  def scan(lba,size,prefix=''):
   if lba in seen:return
   seen.add(lba);data=read(lba,size);p=0
   while p<len(data):
    n=data[p]
    if not n:p=(p//2048+1)*2048;continue
    r=data[p:p+n];p+=n;name=r[33:33+r[32]].split(b';')[0]
    if name in (b'\0',b'\1'):continue
    name=name.decode();sector=int.from_bytes(r[2:6],'little');length=int.from_bytes(r[10:14],'little');full=prefix+name
    if r[25]&2:scan(sector,length,full+'/')
    elif full.startswith(('SFX/','SFX2/')) and full.endswith('.BIN'):
     raw=read(sector,length);dest=ROOT/'GameData/PS1/CD'/full;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(raw);found[full]=raw
  r=pvd[156:190];scan(int.from_bytes(r[2:6],'little'),int.from_bytes(r[10:14],'little'))
  if 'SFX/PERMSFX.BIN' not in found:raise ValueError('Permanent sound bank missing')
  return found

def decode(data):
 out=[];history1=history2=0;loops=[];ended=False;coefficients=[(0,0),(60,0),(115,-52),(98,-55),(122,-60)]
 for block in range(0,len(data),16):
  h,flags=data[block:block+2];shift=h&15;f=h>>4
  if shift>12 or f>4:raise ValueError('Invalid SPU ADPCM predictor')
  if flags&4:loops.append(len(out))
  a,b=coefficients[f]
  for byte in data[block+2:block+16]:
   for nibble in (byte&15,byte>>4):
    value=((nibble-16 if nibble&8 else nibble)<<12)>>shift;value+=((history1*a+history2*b+32)>>6);value=max(-32768,min(32767,value));out.append(value);history2=history1;history1=value
  if flags&1:ended=True;break
 if not ended:raise ValueError('SPU sample lacks end marker')
 return out,loops,flags

def export_level_sfx_lookup(banks):
 exe=ROOT/'GameData/PS1/CD/SLUS_000.07';data=exe.read_bytes()
 if data[:8]!=b'PS-X EXE':raise ValueError('Unsupported PS1 executable')
 # MIPS code at 0x8003d2c4 (file offset 0x2dac4) executes LH s0,0x312c(at)
 # to look up the CD file table index (0x71..0x8f) for mission 0..34, then
 # calls 0x8003d034(s0, 0x29) -> 0x80043ecc(41, buffer) for slots 41..110.
 if struct.unpack_from('<I',data,0x2dac4)[0]!=0x8430312c:raise ValueError('SFX lookup reference differs from analyzed USA executable')
 indices=list(struct.unpack_from('<35H',data,0x6392c))
 def cd_path(idx):
  ptr=struct.unpack_from('<I',data,0x5adf8+idx*36)[0];off=ptr-0x80010000+0x800;end=data.index(0,off)
  return data[off:end].decode('ascii').lstrip('\\').split(';')[0].replace('\\','/')
 if cd_path(0x70)!='SFX/PERMSFX.BIN':raise ValueError('Permanent SFX CD table entry mismatch')
 text=(ROOT/'GameData/PS1/CD/LANGUAGE/MISSIONU.TXT').read_text();codes=[m.replace('.','') for m in re.findall(r'^(\d\.\d\.\d) ',text,re.M)]
 if len(codes)!=35:raise ValueError('Mission order differs from original lookup')
 mapping={}
 for code,idx in zip(codes,indices):
  rel=cd_path(idx)
  if rel not in banks:raise ValueError(f'Missing PS1 level SFX bank {rel}')
  mapping[code]=rel
 (ROOT/'GameData/PS1/level_sfx.txt').write_text(''.join(f'{code} {rel}\n' for code,rel in mapping.items()))
 return mapping

def main():
 banks=banks_from_disc();mapping=export_level_sfx_lookup(banks)
 bank=banks['SFX/PERMSFX.BIN'];count,size=struct.unpack_from('<II',bank);start=8+count*56
 if count!=41 or start+size!=len(bank):raise ValueError('Permanent sound bank extent mismatch')
 records=[struct.unpack_from('<14I',bank,8+i*56) for i in range(count)];rows=[];perm_rows=[];folder=ROOT/'GameData/PS1/SFX';folder.mkdir(parents=True,exist_ok=True)
 for slot,r in enumerate(records):
  offset=r[2];compressed=bank[start+offset:];samples,loops,flags=decode(compressed);pitch=r[5];rate=round(44100*pitch/4096);perm_dest=folder/f'perm-slot{slot:02}.wav'
  with wave.open(str(perm_dest),'wb') as wav:wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(rate);wav.writeframes(array.array('h',samples).tobytes())
  entry=dict(slot=slot,offset=offset,spu_pitch=pitch,rate=rate,frames=len(samples),seconds=len(samples)/rate,loop_starts=loops,end_flags=flags,wav=str(perm_dest.relative_to(ROOT)))
  perm_rows.append(entry)
  if slot in NAMES:
   dest=folder/f'weapon-slot{slot:02}.wav'
   with wave.open(str(dest),'wb') as wav:wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(rate);wav.writeframes(array.array('h',samples).tobytes())
   rows.append(dict(slot=slot,name=NAMES[slot],offset=offset,spu_pitch=pitch,rate=rate,frames=len(samples),seconds=len(samples)/rate,loop_starts=loops,end_flags=flags,sha256=hashlib.sha256(compressed[:len(samples)//28*16]).hexdigest(),wav=str(dest.relative_to(ROOT))))
 level_summary=[]
 for rel,lbank in sorted(banks.items()):
  if rel in ('SFX/PERMSFX.BIN','SFX2/STARTSFX.BIN'):continue
  lcount,lsize=struct.unpack_from('<II',lbank);lstart=8+lcount*56
  if lcount!=70 or lstart+lsize!=len(lbank):raise ValueError(f'Level bank extent mismatch: {rel}')
  lrecords=[struct.unpack_from('<14I',lbank,8+i*56) for i in range(lcount)];active=0
  for i,r in enumerate(lrecords):
   samples,_,_=decode(lbank[lstart+r[2]:])
   if (r[1]&0xffff)>0 and r[5]>1 and any(samples):active+=1
  level_summary.append(dict(bank=rel,sha256=hashlib.sha256(lbank).hexdigest(),slots=lcount,active_slots=active))
 report=dict(source='SFX/PERMSFX.BIN',source_sha256=hashlib.sha256(bank).hexdigest(),records=count,decoded_slots=len(rows),weapons=rows,scope='Original SPU samples, authored pitch; hardware ADSR/reverb not emulated. Existing mixer scheduling retained.')
 (ROOT/'decompilation/reports/ps1_weapon_sfx.json').write_text(json.dumps(report,indent=2)+'\n')
 full_report=dict(status='passed',permanent_bank='SFX/PERMSFX.BIN',permanent_slots=count,permanent_entries=perm_rows,sfx_table_file_offset='0x6392c',sfx_table_runtime_address='0x8007312c',sfx_reference_instruction='0x8003d2c4: lh s0,0x312c(at)',mission_sfx_mapping=mapping,level_banks=level_summary)
 (ROOT/'decompilation/reports/ps1_sfx_banks.json').write_text(json.dumps(full_report,indent=2)+'\n')
 print(f'Decoded {count} permanent PS1 SFX slots and verified {len(level_summary)} PS1 level SFX banks ({len(mapping)} mission assignments)')
if __name__=='__main__':main()
