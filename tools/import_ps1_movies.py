"""Import the supplied NTSC-U PS1 raw-sector movies for native playback.
Requires ffmpeg/ffprobe during import only; the game streams decoded BGRA.
"""
from pathlib import Path
import hashlib,json,shutil,struct,subprocess,statistics,math
ROOT=Path(__file__).resolve().parents[1]
def main():
 manifest=json.loads((ROOT/'decompilation/reports/ps1_disc_manifest.json').read_text())
 imported=json.loads((ROOT/'decompilation/reports/ps1_import.json').read_text())
 cue=ROOT/imported['cue'];track=next(t for t in imported['tracks'] if t['mode']=='MODE2/2352');disc=(cue.parent/track['file']).resolve()
 if disc.parent!=cue.parent.resolve():raise ValueError('Unsafe disc path')
 ffmpeg=shutil.which('ffmpeg');ffprobe=shutil.which('ffprobe')
 if not ffmpeg or not ffprobe:raise RuntimeError('FFmpeg and FFprobe are required for movie import')
 out=ROOT/'GameData/PS1/Movies';out.mkdir(exist_ok=True);report=[]
 with disc.open('rb') as source:
  for entry in manifest['entries']:
   if not entry['path'].endswith('.STR'):continue
   name=Path(entry['path']).stem;raw=out/(name+'.STR');source.seek((entry['sector']+track['index01'])*2352);data=source.read(((entry['size']+2047)//2048)*2352)
   if len(data)!=((entry['size']+2047)//2048)*2352:raise ValueError('Truncated movie sectors')
   raw.write_bytes(data)
   probe=json.loads(subprocess.check_output([ffprobe,'-v','error','-show_streams','-of','json',str(raw)]));video=next(s for s in probe['streams'] if s['codec_type']=='video');width,height=video['width'],video['height'];# FFmpeg's STR demuxer defaults to 15 fps, but the Acclaim logo is
   # authored at 30 fps. Recover cadence from first video sectors at CD 2x.
   starts=[];audio_start=None
   for sector in range(len(data)//2352):
    at=sector*2352;kind=data[at+18]&14
    if kind==4 and audio_start is None:audio_start=sector
    if kind in (2,8) and data[at+24:at+28]==b'\x60\x01\x01\x80' and struct.unpack_from('<H',data,at+28)[0]==0:starts.append(sector)
   cadence=round(statistics.median(y-x for x,y in zip(starts,starts[1:])));divisor=math.gcd(150,cadence);num,den=150//divisor,cadence//divisor;rate=f'{num}/{den}' 
   target=out/(name+'.bgra');temp=target.with_suffix('.bgra.tmp')
   subprocess.run([ffmpeg,'-v','error','-y','-i',str(raw),'-map','0:v:0','-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo',str(temp)],check=True)
   size=temp.stat().st_size
   if size%(width*height*4) or not size:raise ValueError('Incomplete decoded movie')
   temp.replace(target);frames=size//(width*height*4);audio=out/(name+'.pcm');audio_temp=audio.with_suffix('.pcm.tmp')
   has_audio=any(s['codec_type']=='audio' for s in probe['streams'])
   if has_audio:subprocess.run([ffmpeg,'-v','error','-y','-i',str(raw),'-map','0:a:0','-ar','44100','-ac','2','-c:a','pcm_s16le','-f','s16le',str(audio_temp)],check=True);audio_temp.replace(audio)
   if has_audio and audio_start:audio.write_bytes(bytes(round(audio_start*44100/150)*4)+audio.read_bytes())
   (out/(name+'.fmv')).write_bytes(b'ATFMV002'+struct.pack('<6I',width,height,num,den,frames,len(data)//2352))
   report.append({'name':name,'source':entry,'raw_sha256':hashlib.sha256(data).hexdigest(),'width':width,'height':height,'fps':rate,'frames':frames,'video_seconds':frames*den/num,'playback_seconds':len(data)/2352/150,'audio_start_sector':audio_start,'audio_seconds':audio.stat().st_size/176400 if has_audio else 0});print(name,frames,flush=True)
 (ROOT/'decompilation/reports/ps1_movies_import.json').write_text(json.dumps({'status':'imported','movies':report,'decoder':'https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/psxstr.c'},indent=2))
if __name__=='__main__':main()
