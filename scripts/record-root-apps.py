#!/usr/bin/env python3
"""Record actual QEMU Root application authorization with the production image and SDK demo."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import tempfile
import time
from PIL import Image

REPO=Path(__file__).resolve().parents[1]


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try: process.wait(timeout=10)
        except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=REPO/'build/recordings/LeonMobile-third-party-root.mp4')
    parser.add_argument('--images',type=Path,default=REPO/'output/images')
    parser.add_argument('--package',type=Path,default=REPO/'build/root-demo/org.example.rootdemo-1.0.0.lmpkg')
    args=parser.parse_args()
    output=args.output.resolve(); images=args.images.resolve()
    if output.suffix.lower()!='.mp4' or output.exists(): parser.error('--output must be a new MP4 path')
    for binary in ('qemu-system-aarch64','Xvfb','xdotool','ffmpeg','ffprobe','debugfs','sfdisk'):
        if not shutil.which(binary): parser.error('Missing host tool: '+binary)
    if not args.package.is_file(): parser.error('Build the SDK example with scripts/build-root-example.sh')
    output.parent.mkdir(parents=True,exist_ok=True)
    artifacts=output.with_suffix(''); artifacts.mkdir(exist_ok=False)
    spec=importlib.util.spec_from_file_location('leon_root_record_controls',REPO/'scripts/test-qemu.py')
    controls=importlib.util.module_from_spec(spec);spec.loader.exec_module(controls)
    with tempfile.TemporaryDirectory(prefix='leon-root-record-') as name:
        temp=Path(name); disk=temp/'disk.img'; rootfs=temp/'root.ext4'
        subprocess.run(['cp','--reflink=auto','--sparse=always',images/'leonmobile-qemu-aarch64.img',disk],check=True)
        subprocess.run(['cp','--reflink=auto','--sparse=always',images/'rootfs.ext4',rootfs],check=True)
        # Only installable demo bytes enter this cloned disk. No Root-test services or bypasses.
        commands=temp/'inject.txt'
        commands.write_text('write '+json.dumps(str(args.package.resolve()))+' /usr/share/root-demo.lmpkg\nset_inode_field /usr/share/root-demo.lmpkg mode 0100644\n')
        subprocess.run(['debugfs','-w','-f',commands,rootfs],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        partition=json.loads(subprocess.check_output(['sfdisk','--json',disk]))['partitiontable']['partitions'][0]
        with disk.open('r+b') as target,rootfs.open('rb') as source:
            target.seek(partition['start']*512); shutil.copyfileobj(source,target,1024*1024)
        xvfb=qemu=encoder=qmp=None
        chapters=[]; started=time.monotonic()
        def mark(label):
            chapters.append(dict(seconds=round(time.monotonic()-started,3),title=label))
            print(label,flush=True)
        try:
            with (artifacts/'serial.log').open('wb') as serial,(artifacts/'xvfb.log').open('wb') as xlog,(artifacts/'ffmpeg.log').open('wb') as elog:
                r,w=os.pipe()
                try:
                    xvfb=subprocess.Popen(['Xvfb','-displayfd',str(w),'-screen','0','480x960x24','-nolisten','tcp','-noreset'],pass_fds=(w,),stdout=xlog,stderr=xlog)
                finally:
                    os.close(w)
                with os.fdopen(r) as ready:
                    if not select.select([ready],[],[],10)[0]: raise RuntimeError('Xvfb did not start')
                    # Xvfb can write the display number and newline separately.
                    # Keep its pipe open until the complete readiness line arrives.
                    display=':'+ready.readline().strip()
                    if display==':': raise RuntimeError('Xvfb exited; inspect xvfb.log')
                env=dict(os.environ,DISPLAY=display,SDL_VIDEODRIVER='x11')
                endpoint=temp/'qmp.sock'
                qemu=subprocess.Popen(['qemu-system-aarch64','-M','virt','-cpu','cortex-a72','-smp','4','-m','2048','-S',
                    '-kernel',str(images/'Image'),'-initrd',str(images/'leon-initramfs.cpio.gz'),'-append',controls.KERNEL_COMMAND_LINE,
                    '-drive',f'if=none,file={disk},format=raw,id=system','-device','virtio-blk-pci,drive=system','-device','virtio-gpu-pci,xres=480,yres=960',
                    '-device','qemu-xhci','-device','usb-kbd','-device','usb-tablet','-netdev','user,id=net0','-device','virtio-net-pci,netdev=net0',
                    '-audiodev','none,id=audio','-device','intel-hda','-device','hda-duplex,audiodev=audio','-display','sdl,gl=off',
                    '-monitor','none','-serial','stdio','-qmp',f'unix:{endpoint},server=on,wait=off'],env=env,stdin=subprocess.PIPE,stdout=serial,stderr=subprocess.STDOUT)
                end=time.monotonic()+15; window=None
                while time.monotonic()<end:
                    if qemu.poll() is not None: raise RuntimeError('QEMU exited; inspect serial.log')
                    result=subprocess.run(['xdotool','search','--onlyvisible','--pid',str(qemu.pid)],env=env,text=True,capture_output=True)
                    if result.returncode==0 and endpoint.exists():window=result.stdout.splitlines()[-1];break
                    time.sleep(.1)
                if not window:raise RuntimeError('QEMU display did not start')
                for action in (('windowsize',window,'480','960'),('windowmove',window,'0','0')):
                    subprocess.run(['xdotool',*action],env=env,check=True)
                qmp=controls.QMP(endpoint)
                encoder=subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','warning','-y','-f','x11grab','-framerate','30','-video_size','480x960','-draw_mouse','0',
                    '-i',display+'+0,0','-an','-c:v','libx264','-preset','veryfast','-crf','18','-pix_fmt','yuv420p','-threads','2','-r','30','-movflags','+faststart',str(output)],
                    stdin=subprocess.PIPE,stdout=elog,stderr=subprocess.STDOUT)
                started=time.monotonic();mark('开机 · LeonMobile / Linux 7.3.1 / AArch64')
                time.sleep(.5);qmp.command('cont')
                end=time.monotonic()+180
                while time.monotonic()<end:
                    text=(artifacts/'serial.log').read_text(errors='replace').replace('\r','')
                    if 'leond: started shell' in text and '\n$ ' in text:break
                    if qemu.poll() is not None:raise RuntimeError('QEMU exited before boot')
                    time.sleep(.2)
                else:raise RuntimeError('Boot timed out')
                guest=controls.Guest(qemu,artifacts/'serial.log')
                def pixel(frame,x,y):return frame[(y*480+x)*3:(y*480+x)*3+3]
                qmp.wait_frame(artifacts/'boot-ready.ppm',lambda f:pixel(f,0,100)==bytes.fromhex('fff8ff'),'Boot lock not rendered')
                time.sleep(1);qmp.click(240,790);time.sleep(1)
                mark('设置 Root 密码、第三方应用确认与单次执行')
                from root_apps_qemu import exercise_root_applications
                exercise_root_applications(guest,qmp,artifacts,180,reboot=False)
                mark('完成 · 应用保持沙盒身份，Root helper 返回诊断结果')
                time.sleep(3)
                guest.run('lmsys power \'{"op":"poweroff"}\'')
                qemu.wait(timeout=30)
                if qemu.returncode:raise RuntimeError('QEMU did not shut down cleanly')
                time.sleep(1)
                encoder.stdin.write(b'q\n');encoder.stdin.flush();encoder.communicate(timeout=30)
                if encoder.returncode:raise RuntimeError('Video encoder failed')
                info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',output],text=True))
                (artifacts/'recording.json').write_text(json.dumps(dict(video=output.name,width=480,height=960,fps=30,audio=False,real_time=True,
                    duration_seconds=float(info['format']['duration']),chapters=chapters,
                    image_directory=str(images),source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip()),ensure_ascii=False,indent=2)+'\n')
                print('Video: '+str(output),flush=True)
        finally:
            if encoder and encoder.poll() is None:
                try:encoder.stdin.write(b'q\n');encoder.stdin.flush();encoder.communicate(timeout=10)
                except (OSError,subprocess.TimeoutExpired):stop(encoder)
            if qmp:qmp.stream.close();qmp.socket.close()
            stop(qemu);stop(xvfb)
if __name__=='__main__':main()
