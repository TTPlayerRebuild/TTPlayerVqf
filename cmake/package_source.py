"""Create a separate corresponding-source attachment, without private tests."""
import argparse,pathlib,zipfile,hashlib
p=argparse.ArgumentParser();p.add_argument('--project',type=pathlib.Path,required=True);p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--version',required=True);a=p.parse_args()
r=a.project.resolve();b=a.build.resolve();name='ttp_vqf';out=b/'Release'/f'{name}-{a.version}-source.zip'
upstream=pathlib.Path((b/'ffmpeg-source-path.txt').read_text(encoding='utf-8').strip())
cache=pathlib.Path((b/'ffmpeg-build-path.txt').read_text(encoding='utf-8').strip())
make=cache/'make-4.4.1.tar.gz'
if not make.exists():make=r/'third_party/make-4.4.1.tar.gz'
if hashlib.sha256(make.read_bytes()).hexdigest()!='dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3':raise RuntimeError('GNU make source digest mismatch')
def files(base):
 for f in sorted(base.rglob('*')):
  if f.is_file() and not any(x in ('.git','__pycache__','WinRel','WinDebug') for x in f.relative_to(base).parts) and f.suffix.lower() not in ('.pyc','.pdb','.obj','.exe','.dll','.lib'):
   yield f
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for entry in ('CMakeLists.txt','build.ps1','README.md','LICENSE','.gitignore','cmake','src','include','docs','LICENSES','.github'):
  path=r/entry
  for f in files(path) if path.is_dir() else [path]:z.write(f,name+'/'+f.relative_to(r).as_posix())
 for f in files(upstream):z.write(f,name+'/third_party/ffmpeg/'+f.relative_to(upstream).as_posix())
 z.write(make,name+'/third_party/make-4.4.1.tar.gz')
 z.writestr(name+'/BUILD_VERSION',a.version+'\n')
print(out)
