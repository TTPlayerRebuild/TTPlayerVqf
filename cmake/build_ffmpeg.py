"""Build a pinned, minimal FFmpeg static backend with MSVC and the XP CRT.

Sources are downloaded at build time. GNU make is bootstrapped from official
source in this build tree; no package manager or system configuration is changed.
"""
import argparse, hashlib, json, os, pathlib, shutil, subprocess, tarfile, urllib.request

FFMPEG_VERSION = '9.0.2'
FFMPEG_SHA256 = '8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e'
MAKE_SHA256 = 'dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3'

def download(url, dest, digest):
    if not dest.exists() or hashlib.sha256(dest.read_bytes()).hexdigest()!=digest:
        urllib.request.urlretrieve(url, dest)
    if hashlib.sha256(dest.read_bytes()).hexdigest()!=digest:
        raise RuntimeError('Dependency SHA256 mismatch: '+str(dest))

def extract(archive, dest):
    with tarfile.open(archive) as t:
        t.extractall(dest,filter='data')

def vc_environment(vcvars, arch, folder):
    script=folder/('env-'+arch+'.cmd')
    script.write_text('@echo off\ncall "'+str(vcvars)+'" '+arch+' >nul\nset\n',encoding='utf-8')
    r=subprocess.run(['cmd','/d','/c',str(script)],capture_output=True,check=True)
    env=os.environ.copy()
    for line in r.stdout.decode('mbcs').splitlines():
        if '=' in line and not line.startswith('='):
            k,v=line.split('=',1);env[k.upper()]=v
    # Windows environment keys are case-insensitive, Python dict keys are not.
    return {k.upper():v for k,v in env.items()}

def posix(path):
    p=str(pathlib.Path(path).resolve()).replace('\\','/')
    return '/'+p[0].lower()+p[2:] if len(p)>1 and p[1]==':' else p

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--build',type=pathlib.Path,required=True)
    ap.add_argument('--vs',type=pathlib.Path,required=True)
    ap.add_argument('--ltl',type=pathlib.Path,required=True)
    ap.add_argument('--kind',choices=['rm','vqf','tak','asf'],required=True)
    ap.add_argument('--bash',type=pathlib.Path)
    ap.add_argument('--source',type=pathlib.Path)
    ap.add_argument('--make-archive',type=pathlib.Path)
    a=ap.parse_args();b=a.build.resolve();b.mkdir(parents=True,exist_ok=True)
    git=shutil.which('git')
    bash=a.bash or (pathlib.Path(git).resolve().parent.parent/'bin/bash.exe' if git else None)
    if not bash or not bash.is_file():
        raise RuntimeError('Git for Windows bash.exe is required; set TTP_FFMPEG_BASH.')
    vcvars=a.vs/'VC/Auxiliary/Build/vcvarsall.bat'
    source=a.source.resolve() if a.source else b/('ffmpeg-'+FFMPEG_VERSION)
    make_source=b/'make-4.4.1'
    if not source.exists():
        tar=b/('ffmpeg-'+FFMPEG_VERSION+'.tar.xz')
        download('https://ffmpeg.org/releases/'+tar.name,tar,FFMPEG_SHA256);extract(tar,b)
    twin=source/'libavcodec/twinvq.c'
    twin_source=twin.read_text(encoding='utf-8')
    marker='    /* get output buffer */\n'
    compatibility='''#if defined(TTP_TWINVQ_KEEP_LEADING_FRAMES)
    /* TTPlayer 5.7.9 returns the first two TwinVQ synthesis frames too. */
    if (avctx->codec_id == AV_CODEC_ID_TWINVQ)
        tctx->discarded_packets = 2;
#endif

'''
    if 'TTP_TWINVQ_KEEP_LEADING_FRAMES' not in twin_source:
        if twin_source.count(marker)!=1:raise RuntimeError('TwinVQ patch precondition changed')
        twin_source=twin_source.replace(marker,compatibility+marker)
    init_marker='    for (i = 0; i < 3; i++) {\n        int m       = 4 * mtab->size / mtab->fmode[i].sub;'
    init_compatibility='''#if defined(TTP_TWINVQ_KEEP_LEADING_FRAMES)
    /* The upstream decoder discards two priming frames. Returning them as the
     * legacy TTPlayer ABI does also requires a defined initial overlap history.
     * Without this, a fresh Reader can expose a previous heap allocation. */
    if (tctx->avctx->codec_id == AV_CODEC_ID_TWINVQ) {
        memset(tctx->curr_frame, 0, table_size * sizeof(*tctx->curr_frame));
        memset(tctx->prev_frame, 0, table_size * sizeof(*tctx->prev_frame));
    }
#endif

'''
    if 'defined initial overlap history' not in twin_source:
        if twin_source.count(init_marker)!=1:raise RuntimeError('TwinVQ initial history patch precondition changed')
        twin_source=twin_source.replace(init_marker,init_compatibility+init_marker)
    if twin.read_text(encoding='utf-8')!=twin_source:
        twin.write_text(twin_source,encoding='utf-8')
    rm_decoder=source/'libavformat/rmdec.c'
    rm_source=rm_decoder.read_text(encoding='utf-8')
    rm_marker='    if (codec_data_size == 0)\n        return 0;'
    rm_compatibility='''#if defined(TTP_RM_MP3_ADU)
    /* TTPlayer 5.7.9: MDPR audio/X-MP3-draft-00 carries MP3 ADU packets. */
    if (mime && !av_strncasecmp((const char *)mime, "audio/X-MP3-draft", 17)) {
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_MP3ADU;
        st->codecpar->codec_tag = MKTAG('a', 'd', 'u', 'U');
        avpriv_set_pts_info(st, 64, 1, 1000);
        avio_skip(pb, codec_data_size);
        return 0;
    }
#endif
'''
    if 'TTP_RM_MP3_ADU' not in rm_source:
        if rm_source.count(rm_marker)!=1:raise RuntimeError('RealAudio patch precondition changed')
        rm_decoder.write_text(rm_source.replace(rm_marker,rm_compatibility+rm_marker),encoding='utf-8')
    make=make_source/'WinRel/gnumake.exe'
    if not make.is_file():
        if not make_source.exists():
            tar=a.make_archive.resolve() if a.make_archive else b/'make-4.4.1.tar.gz'
            download('https://ftp.gnu.org/gnu/make/'+tar.name,tar,MAKE_SHA256);extract(tar,b)
        with (b/'make-build.log').open('w') as log:
            subprocess.run(['cmd','/d','/c','build_w32.bat','--without-guile'],cwd=make_source,
                env=vc_environment(vcvars,'x64',b),stdout=log,stderr=subprocess.STDOUT,check=True)
    out=b/('ffmpeg-'+a.kind);out.mkdir(exist_ok=True)
    decoders={'rm':'cook,atrac3,sipr,ra_144,ra_288,aac,ac3,eac3,mp3adu', 'vqf':'twinvq',
              'tak':'tak','asf':'wmav1,wmav2,wmapro,wmalossless,wmavoice'}[a.kind]
    options=['--toolchain=msvc','--target-os=win32','--arch=x86','--disable-asm',
        '--disable-everything','--disable-autodetect','--disable-programs','--disable-doc',
        '--disable-network','--disable-avdevice','--disable-avfilter','--disable-swscale',
        '--disable-swresample','--enable-w32threads','--disable-pthreads','--disable-debug',
        '--disable-iconv','--enable-small','--enable-static','--disable-shared',
        '--enable-decoder='+decoders,'--enable-demuxer='+a.kind,
        '--extra-cflags=-MT -O1 -Gy -Gw -D_WIN32_WINNT=0x0601'+(' -DTTP_TWINVQ_KEEP_LEADING_FRAMES=1' if a.kind=='vqf' else ' -DTTP_RM_MP3_ADU=1' if a.kind=='rm' else ''),
        '--extra-ldflags=-NODEFAULTLIB:libcmt -DEFAULTLIB:msvcrt']
    source_digest=hashlib.sha256()
    for source_file in sorted(source.rglob('*')):
        if source_file.is_file() and (source_file.suffix in ('.c','.h','.S','.asm','.mak') or source_file.name in ('configure','Makefile')):
            source_digest.update(source_file.relative_to(source).as_posix().encode())
            source_digest.update(source_file.read_bytes())
    identity={'source':str(source),'source_sha256':source_digest.hexdigest(),'version':FFMPEG_VERSION,'sha256':FFMPEG_SHA256,'options':options,
              'script':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
              'vs':str(a.vs),'ltl':str(a.ltl)}
    stamp=out/'build-identity.json'
    libraries=[out/('lib'+x)/(x+'.lib') for x in ('avcodec','avformat','avutil')]
    if stamp.exists() and json.loads(stamp.read_text())==identity and all(p.exists() for p in libraries):
        return
    env=vc_environment(vcvars,'x86',b)
    ltl=a.ltl.resolve()
    env['INCLUDE']=str(ltl/'TargetPlatform/5.1.2600.0/header')+';'+str(ltl/'TargetPlatform/header')+';'+env.get('INCLUDE','')
    env['LIB']=str(ltl/'TargetPlatform/5.1.2600.0/lib/Win32')+';'+env.get('LIB','')
    env['MSYS2_ARG_CONV_EXCL']='*'
    env['PATH']=str(bash.parent.parent/'usr/bin')+';'+str(bash.parent)+';'+env['PATH']
    import shlex
    configure=' '.join(shlex.quote(s) for s in ['bash',(source/'configure').as_posix()]+options)
    with (b/('ffmpeg-'+a.kind+'-configure.log')).open('w') as log:
        subprocess.run([str(bash),'-c',configure],cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    configuration=(out/'config.h').read_text(encoding='utf-8')
    for forbidden in ('CONFIG_GPL','CONFIG_NONFREE','CONFIG_VERSION3','CONFIG_GPLV3'):
        if '#define '+forbidden+' 0' not in configuration:raise RuntimeError('Non-LGPL-2.1 backend profile: '+forbidden)
    # Configure runs in Git's POSIX shell; native GNU make opens include files
    # itself and therefore needs drive-letter paths rather than /c or /d paths.
    for path in (out/'Makefile',out/'ffbuild/config.mak'):
        content=path.read_text(encoding='utf-8')
        for drive in 'abcdefghijklmnopqrstuvwxyz':
            content=content.replace('/'+drive+'/',drive.upper()+':/')
        path.write_text(content,encoding='utf-8')
    with (b/('ffmpeg-'+a.kind+'-build.log')).open('w') as log:
        # Fixed dependency sources: no locale-dependent /showIncludes parser is
        # needed. Any configuration/script change rebuilds all dependency objects.
        subprocess.run([str(make),'clean',
            'SHELL='+(bash.parent.parent/'usr/bin/sh.exe').as_posix()],cwd=out,env=env,
            stdout=log,stderr=subprocess.STDOUT,check=True)
        subprocess.run([str(make),'-j4','CCDEP=','CXXDEP=','HOSTCCDEP=',
            'SHELL='+(bash.parent.parent/'usr/bin/sh.exe').as_posix()],cwd=out,env=env,
            stdout=log,stderr=subprocess.STDOUT,check=True)
    if not all(p.exists() for p in libraries):raise RuntimeError('FFmpeg static libraries missing')
    stamp.write_text(json.dumps(identity,indent=2),encoding='utf-8')

if __name__=='__main__':main()
