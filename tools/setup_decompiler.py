"""Download portable official tools into this workspace; no system installation."""
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools' / 'vendor'


def request(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'AlienTrilogyResearch/1.0'}), timeout=120)


def install(url, name, checksum=None):
    TOOLS.mkdir(parents=True, exist_ok=True)
    archive = TOOLS / name
    if not archive.exists():
        partial = archive.with_suffix('.partial')
        print('Downloading ' + name, flush=True)
        with request(url) as source, partial.open('wb') as target:
            while block := source.read(1024 * 1024):
                target.write(block)
        partial.replace(archive)
    digest = hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()
    if checksum and digest.lower() != checksum.lower():
        raise ValueError('Checksum mismatch: ' + name)
    print(name + ' SHA256 ' + digest, flush=True)
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            destination = (TOOLS / member.filename).resolve()
            if not destination.is_relative_to(TOOLS.resolve()):
                raise ValueError('Unsafe archive member')
        z.extractall(TOOLS)
    return {'url': url, 'archive': name, 'sha256': digest}


if __name__ == '__main__':
    release = json.load(request('https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/tags/Ghidra_12.2_build'))
    asset = next(a for a in release['assets'] if a['name'].endswith('.zip'))
    java = json.load(request('https://api.adoptium.net/v3/assets/latest/25/hotspot?architecture=x64&image_type=jdk&os=windows'))[0]['binary']['package']
    records = []
    records.append(install(java['link'], java['name'], java['checksum']))
    digest = asset.get('digest', '')
    records.append(install(asset['browser_download_url'], asset['name'], digest.removeprefix('sha256:') or None))
    (TOOLS / 'downloads.json').write_text(json.dumps(records, indent=2) + '\n')
    print('Portable tools ready.', flush=True)
