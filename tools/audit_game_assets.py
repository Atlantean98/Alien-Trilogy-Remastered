"""Inventory original assets and losslessly extract supported texture/map chunks.

Container bounds and byte round trips are validated. Map field names and RGB555
preview colors are format hypotheses, not original-loader equivalence claims.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'GameData/CD'
OUTPUT=ROOT/'decompilation/assets'
REPORTS=ROOT/'decompilation/reports'

def digest(data):return hashlib.sha256(data).hexdigest()
def save(path,data):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    assert path.read_bytes()==data,('round trip',str(path))
    return path.relative_to(ROOT).as_posix()
def png_chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
def indexed_png(pixels,palette):
    colors=bytearray()
    for value in struct.unpack('<256H',palette[4:516]):
        colors.extend([(value&31)<<3,((value>>5)&31)<<3,((value>>10)&31)<<3])
    raw=b''.join(b'\0'+pixels[y*256:(y+1)*256] for y in range(256))
    encoded=zlib.compress(raw,9);assert zlib.decompress(encoded)==raw
    return b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('>IIBBBBB',256,256,8,3,0,0,0))+png_chunk(b'PLTE',bytes(colors))+png_chunk(b'IDAT',encoded)+png_chunk(b'IEND',b'')
def form_chunks(data):
    issues=[];chunks=[]
    if len(data)<12:return [],['FORM header truncated']
    end=int.from_bytes(data[4:8],'big')+8
    if end!=len(data):issues.append(f'FORM extent {end} differs from file length {len(data)}')
    end=min(end,len(data));p=12
    while p+8<=end:
        size=int.from_bytes(data[p+4:p+8],'big');start=p+8
        if start+size>end:issues.append(f'chunk at {p} exceeds FORM extent');break
        tag=data[p:p+4].decode('ascii',errors='backslashreplace')
        chunks.append(dict(tag=tag,header_offset=p,payload_offset=start,size=size,sha256=digest(data[start:start+size])))
        p=start+size+(size&1)
    if p!=end:issues.append(f'unparsed FORM tail begins at {p}, extent {end}')
    return chunks,issues

files=[];textures=[];maps=[];chunk_totals=Counter();extensions=Counter()
for path in sorted(SOURCE.rglob('*')):
    if not path.is_file():continue
    data=path.read_bytes();relative=path.relative_to(SOURCE);extensions[path.suffix.upper()]+=1
    record=dict(path=path.relative_to(ROOT).as_posix(),size=len(data),sha256=digest(data))
    if not data:record['status']='empty_input'
    elif data.startswith(b'FORM'):
        chunks,issues=form_chunks(data);record.update(status='container_issues' if issues else 'container_valid',form_type=data[8:12].decode('ascii',errors='backslashreplace'),chunks=chunks,issues=issues)
        chunk_totals.update(c['tag'] for c in chunks)
        # Preserve the relative source filename in the export directory, so
        # distinct .BIN/.BND/.B16 names cannot overwrite each other's data.
        target=OUTPUT/relative.parent/relative.name
        for number,c in enumerate(chunks):
            payload=data[c['payload_offset']:c['payload_offset']+c['size']]
            if c['tag'].startswith('TP'):
                suffix=c['tag'][2:];pal=next((v for v in chunks if v['tag']=='CL'+suffix),None)
                entry=dict(source=record['path'],chunk=c['tag'],offset=c['payload_offset'],size=len(payload),sha256=c['sha256'])
                entry['indices']=save(target/f'{number:03}_{c["tag"]}.indices.bin',payload)
                if pal:
                    raw_palette=data[pal['payload_offset']:pal['payload_offset']+pal['size']]
                    entry['palette_chunk']=save(target/f'{number:03}_{pal["tag"]}.bin',raw_palette)
                    if len(payload)==65536 and len(raw_palette)>=516:
                        entry['preview']=save(target/f'{number:03}_{c["tag"]}.png',indexed_png(payload,raw_palette))
                        entry['preview_interpretation']='256x256 indexed image, candidate little-endian RGB555 palette; no transparency assumed'
                textures.append(entry)
            elif c['tag']=='MAP0':
                entry=dict(source=record['path'],offset=c['payload_offset'],size=len(payload),sha256=c['sha256'],payload=save(target/f'{number:03}_MAP0.bin',payload))
                if len(payload)>=36:
                    vertices,faces,length,width=struct.unpack_from('<4H',payload)
                    geometry_end=36+vertices*8+faces*20;grid_end=geometry_end+length*width*16
                    entry['candidate_layout']=dict(vertices=vertices,faces=faces,grid_length=length,grid_width=width,geometry_end=geometry_end,grid_end=grid_end,regions_fit=grid_end<=len(payload))
                    if grid_end<=len(payload):
                        invalid=[];sentinels=0
                        for i in range(faces):
                            indices=struct.unpack_from('<4i',payload,36+vertices*8+i*20)
                            if indices[0]==-1:sentinels+=1;continue
                            if any(n<0 or n>=vertices for n in indices[:3]) or not (-1<=indices[3]<vertices):invalid.append(i)
                        entry['candidate_layout'].update(invalid_face_indices=invalid,sentinel_faces=sentinels,first_face_indices=list(struct.unpack_from('<4i',payload,36+vertices*8)) if faces else [])
                maps.append(entry)
    else:record['status']='non_form_data'
    # The audit only reads source files; confirm each source still matches
    # its recorded identity after exports have been written.
    assert digest(path.read_bytes())==record['sha256']
    files.append(record)

result=dict(status='completed',source_files=len(files),empty_inputs=sum(f['size']==0 for f in files),extensions=dict(extensions),
    container_issues=sum(f['status']=='container_issues' for f in files),chunk_counts=dict(sorted(chunk_totals.items())),
    extracted_texture_chunks=len(textures),indexed_previews=sum('preview' in t for t in textures),extracted_map_chunks=len(maps),
    limitations=['Bounds and lossless chunk exports are verified; candidate map semantics and preview RGB interpretation still require original-loader checks.', 'Empty supplied files are inventoried, not treated as recoverable content.', 'This does not yet decode every entity/script/animation/audio format or count as additional recovered executable routines.'],
    files=files,textures=textures,maps=maps)
REPORTS.mkdir(parents=True,exist_ok=True)
(REPORTS/'asset_inventory.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ['files','textures','maps','chunk_counts']},indent=2))
