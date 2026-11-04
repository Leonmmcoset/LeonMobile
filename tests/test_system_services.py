"""Real Unix peers, bounded transports, persistence and core capture fixtures.

Every daemon uses --test and temporary roots: host clocks, mounts, network,
core_pattern and power state are never modified.
"""
import contextlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


def rpc(endpoint, request, uid=0):
    if uid:
        code = ('import os,socket,json,sys;os.setgroups([]);os.setgid(int(sys.argv[3]));'
                'os.setuid(int(sys.argv[3]));s=socket.socket(socket.AF_UNIX);s.settimeout(4);'
                's.connect(sys.argv[1]);s.sendall(sys.argv[2].encode()+b"\\n");'
                's.shutdown(socket.SHUT_WR);out=bytearray();'
                '\nwhile b:=s.recv(8192): out.extend(b)'
                '\nprint(out.decode())')
        result = subprocess.run(['python3', '-c', code, str(endpoint), json.dumps(request), str(uid)],
                                capture_output=True, text=True, timeout=6, check=True)
        return json.loads(result.stdout)
    with socket.socket(socket.AF_UNIX) as s:
        s.settimeout(5)
        s.connect(str(endpoint))
        s.sendall(json.dumps(request).encode() + b'\n')
        s.shutdown(socket.SHUT_WR)
        out = bytearray()
        while b := s.recv(8192): out.extend(b)
        return json.loads(out)


@unittest.skipUnless(os.geteuid() == 0, 'system services require root')
class SystemServices(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.root.chmod(0o755)
        self.builtin = self.root / 'builtin'
        self.builtin.mkdir()
        for name in ('settings', 'gallery'):
            (self.builtin / (name+'.json')).write_text(json.dumps(dict(
                format=1,id='org.leon.'+name,version='1.0.0',arch='aarch64',lmui=1,
                name=name,entry='/usr/bin/true',permissions=['network','files.import'])))
        self.procroot = self.root / 'proc'
        (self.procroot/'pressure').mkdir(parents=True)
        (self.procroot/'meminfo').write_text('MemTotal: 1048576 kB\nMemAvailable: 65536 kB\n')
        (self.procroot/'pressure/memory').write_text('some avg10=0.21 avg60=0.12 avg300=0.10 total=1000\n')
        (self.root/'sys/class/block/sda').mkdir(parents=True)
        (self.root/'sys/class/block/sda/removable').write_text('1\n')
        (self.root/'sys/class/block/sda/size').write_text('2048\n')

    def tearDown(self):
        self.temp.cleanup()

    @contextlib.contextmanager
    def daemon(self, name, state=None):
        endpoint = self.root/(name+'.sock')
        args = [BUILD/'bin'/name, '--test', '--socket',endpoint,
                '--state', state or self.root/(name+'.json'), '--root',self.root,
                '--builtin',self.builtin,'--proc',self.procroot,'--sys',self.root/'sys']
        p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic()+4
            ready=False
            while time.monotonic()<deadline and p.poll() is None:
                try:
                    rpc(endpoint,{'op':'__ready_probe'})
                    ready=True;break
                except (OSError,ValueError):time.sleep(.01)
            self.assertTrue(ready, p.poll())
            yield endpoint, p
        finally:
            if p.poll() is None: p.terminate()
            stdout, stderr = p.communicate(timeout=6)
            self.assertEqual(p.returncode, 0, stderr.decode())

    def test_memory_policy_persistence_and_pressure(self):
        with self.daemon('memd') as (s,p):
            self.assertEqual(rpc(s,{'op':'status'})['available_kb'],65536)
            self.assertTrue(rpc(s,{'op':'status'})['psi_available'])
            self.assertFalse(rpc(s,{'op':'policy','min_available_mb':32},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'policy','min_available_mb':'32'})['ok'])
            self.assertTrue(rpc(s,{'op':'policy','min_available_mb':32})['ok'])
        with self.daemon('memd') as (s,p):
            self.assertEqual(rpc(s,{'op':'status'})['min_available_mb'],32)

    def test_power_idle_inhibitors_identity_and_expiration(self):
        with self.daemon('powerd') as (s,p):
            self.assertTrue(rpc(s,{'op':'policy','idle_seconds':1})['ok'])
            self.assertTrue(rpc(s,{'op':'locked','value':False},1000)['ok'])
            grant=rpc(s,{'op':'inhibit','reason':'playing','seconds':2},11002)
            self.assertTrue(grant['ok'],grant)
            self.assertFalse(rpc(s,{'op':'release','token':grant['token']},11000)['ok'])
            time.sleep(1.2)
            self.assertFalse(rpc(s,{'op':'status'},1000)['lock_requested'])
            time.sleep(1.1)
            self.assertTrue(rpc(s,{'op':'status'},1000)['lock_requested'])
            self.assertFalse(rpc(s,{'op':'poweroff'},11002)['ok'])
            self.assertFalse(rpc(s,{'op':'poweroff'},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'locked','value':'false'},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'inhibit','reason':'spoof','seconds':1},12345)['ok'])
            self.assertFalse(rpc(s,{'op':'suspend'},1000)['ok'])

    def test_storage_device_inventory_and_unsafe_mutations(self):
        with self.daemon('storaged',self.root) as (s,p):
            status=rpc(s,{'op':'status'})
            self.assertEqual(status['devices'],[dict(name='sda',removable=True,bytes=1048576)])
            self.assertGreater(status['filesystems'][0]['available_bytes'],0)
            for device in ('../vda','vda1','sda'):
                self.assertFalse(rpc(s,{'op':'mount','device':device},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'unmount','device':'sda'},11002)['ok'])

    def test_network_reads_and_mutation_boundaries(self):
        state=self.root/'resolver'; state.write_text('nameserver 10.0.2.3\n')
        with self.daemon('netd',state) as (s,p):
            status=rpc(s,{'op':'status'},11002)
            self.assertEqual(status['resolver'],'nameserver 10.0.2.3\n')
            self.assertEqual(status['dhcp_pid'],0)
            for op in ('disable','enable','renew'):
                self.assertFalse(rpc(s,{'op':op},11002)['ok'])
                self.assertFalse(rpc(s,{'op':op},1000)['ok'])

    def test_timezone_persistence_strict_types_and_no_clock_mutation(self):
        with self.daemon('timed') as (s,p):
            self.assertFalse(rpc(s,{'op':'configure','timezone':'../../etc'},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'configure','automatic':'yes'},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'configure','timezone':False},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'configure','timezone':'Asia/Shanghai'},11002)['ok'])
            self.assertTrue(rpc(s,{'op':'configure','timezone':'Asia/Shanghai','automatic':False},11000)['ok'])
            self.assertTrue(rpc(s,{'op':'configure','server':'127.0.0.1'},11000)['ok'])
            self.assertEqual(rpc(s,{'op':'status'})['timezone'],'Asia/Shanghai')
            self.assertFalse(rpc(s,{'op':'set_time','unix_seconds':1700000000},1000)['ok'])
        with self.daemon('timed') as (s,p):
            self.assertEqual(rpc(s,{'op':'status'})['timezone'],'Asia/Shanghai')
            self.assertFalse(rpc(s,{'op':'status'})['automatic'])

    def test_portal_confirmation_identity_and_import_traversal(self):
        with self.daemon('portald') as (s,p):
            self.assertFalse(rpc(s,{'op':'query','uid':11000,'permission':'network'},11002)['ok'])
            self.assertFalse(rpc(s,{'op':'request','permission':'camera'},11002)['ok'])
            q=rpc(s,{'op':'request','permission':'files.import'},11002)
            self.assertFalse(q['granted']); token=q['token']
            self.assertEqual(rpc(s,{'op':'request','permission':'files.import'},11002)['token'],token)
            self.assertFalse(rpc(s,{'op':'pending'},11002)['ok'])
            self.assertFalse(rpc(s,{'op':'decide','token':token,'allow':True},11002)['ok'])
            self.assertFalse(rpc(s,{'op':'decide','token':token,'allow':'true'},1000)['ok'])
            self.assertTrue(rpc(s,{'op':'decide','token':token,'allow':True},1000)['ok'])
            self.assertTrue(rpc(s,{'op':'query','permission':'files.import'},11002)['granted'])
            shared=self.root/'shared'; shared.mkdir(); (shared/'note').write_text('hello 中文')
            (shared/'escape').symlink_to('/etc/passwd')
            (shared/'fifo').mkfifo() if hasattr(Path,'mkfifo') else os.mkfifo(shared/'fifo')
            for file in ('../builtin/gallery.json','escape','/etc/passwd','fifo'):
                self.assertFalse(rpc(s,{'op':'import','uid':11002,'file':file},1000)['ok'])
            result=rpc(s,{'op':'import','uid':11002,'file':'note'},1000)
            self.assertTrue(result['ok'],result)
            target=Path(result['path'])
            self.assertEqual(target.read_text(),'hello 中文')
            self.assertEqual(target.stat().st_uid,11002)
            self.assertEqual(target.stat().st_mode & 0o777,0o600)
            self.assertTrue(rpc(s,{'op':'revoke','app':'org.leon.gallery','permission':'files.import'},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'import','uid':11002,'file':'note'},1000)['ok'])
        with self.daemon('portald') as (s,p):
            self.assertFalse(rpc(s,{'op':'query','permission':'files.import'},11002)['granted'])

    def test_selected_file_identity_cancel_and_symlink_destination(self):
        with self.daemon('portald') as (s,p):
            token=rpc(s,{'op':'request','permission':'files.import'},11002)['token']
            self.assertTrue(rpc(s,{'op':'decide','token':token,'allow':True},1000)['ok'])
            shared=self.root/'shared';shared.mkdir();(shared/'note').write_text('selected')
            token=rpc(s,{'op':'choose'},11002)['token']
            self.assertTrue(rpc(s,{'op':'pending'},1000)['requests'][0]['file_request'])
            self.assertFalse(rpc(s,{'op':'selection','token':token},11000)['ok'])
            self.assertFalse(rpc(s,{'op':'pick','token':token,'file':'note'},11002)['ok'])
            self.assertTrue(rpc(s,{'op':'pick','token':token,'file':'note'},1000)['ok'])
            result=rpc(s,{'op':'selection','token':token},11002)
            self.assertTrue(result['done']);self.assertEqual(Path(result['path']).read_text(),'selected')
            token=rpc(s,{'op':'choose'},11002)['token']
            self.assertTrue(rpc(s,{'op':'decide','token':token,'allow':False},1000)['ok'])
            self.assertEqual(rpc(s,{'op':'selection','token':token},11002)['path'],'')
            self.assertFalse(rpc(s,{'op':'files'},11002)['ok'])
            self.assertEqual(rpc(s,{'op':'files'},1000)['files'][0]['name'],'note')
            self.assertFalse(rpc(s,{'op':'files','directory':'..'},1000)['ok'])
            imports=self.root/'data/org.leon.gallery/imports'
            for file in imports.iterdir():file.unlink()
            imports.rmdir();imports.symlink_to(shared,target_is_directory=True)
            self.assertFalse(rpc(s,{'op':'import','uid':11002,'file':'note'},1000)['ok'])
            self.assertFalse(rpc(s,{'op':'revoke','app':'missing.app','permission':'network'},1000)['ok'])

    def test_exit_records_are_bounded_persistent_and_private(self):
        state=self.root/'reports'
        with self.daemon('crashd',state) as (s,p):
            self.assertFalse(rpc(s,{'op':'record_exit','app':'org.leon.gallery','pid':123,'status':139},1000)['ok'])
            for index in range(68):
                self.assertTrue(rpc(s,{'op':'record_exit','app':'org.leon.gallery','pid':index+1,'status':139})['ok'])
            exits=rpc(s,{'op':'list'},11002)['exits']
            self.assertEqual(len(exits),64);self.assertEqual(exits[0]['pid'],5)
            self.assertEqual(rpc(s,{'op':'list'},11000)['exits'],[])
        with self.daemon('crashd',state) as (s,p):
            self.assertEqual(len(rpc(s,{'op':'list'})['exits']),64)

    def test_stale_socket_and_duplicate_listener(self):
        endpoint=self.root/'memd.sock'
        stale=socket.socket(socket.AF_UNIX);stale.bind(str(endpoint));stale.close()
        with self.daemon('memd') as (s,p):
            duplicate=subprocess.run([BUILD/'bin/memd','--test','--socket',s,
                                      '--state',self.root/'unused.json'],capture_output=True,timeout=4)
            self.assertNotEqual(duplicate.returncode,0)
            self.assertTrue(rpc(s,{'op':'status'})['ok'])
        target=self.root/'intact';target.write_text('keep');endpoint.symlink_to(target)
        failed=subprocess.run([BUILD/'bin/memd','--test','--socket',endpoint],capture_output=True,timeout=4)
        self.assertNotEqual(failed.returncode,0);self.assertEqual(target.read_text(),'keep')

    def test_bounded_transport_malformed_and_slow_clients(self):
        with self.daemon('memd') as (endpoint,p):
            slow=[]
            for _ in range(16):
                s=socket.socket(socket.AF_UNIX);s.connect(str(endpoint));s.sendall(b'{');slow.append(s)
            time.sleep(1.3)
            self.assertTrue(rpc(endpoint,{'op':'status'})['ok'])
            for s in slow: s.close()
            for data in (b'[]\n',b'{bad}\n',b'{"op":"status"}\x00\n',b'x'*9000+b'\n', b'{"op":"status\\u0000extra"}\n', b'['*40+b'0'+b']'*40+b'\n'):
                with socket.socket(socket.AF_UNIX) as s:
                    s.settimeout(4);s.connect(str(endpoint));s.sendall(data);s.shutdown(socket.SHUT_WR)
                    out=bytearray()
                    while b:=s.recv(8192):out.extend(b)
                    self.assertFalse(json.loads(out)['ok'])
            self.assertTrue(rpc(endpoint,{'op':'status'})['ok'])

    def test_crash_capture_quota_privacy_and_invalid_removal(self):
        state=self.root/'reports'
        for index in range(10):
            subprocess.run([BUILD/'bin/crashd','--capture','123','11002','11',str(1700000000+index),
                            '--state',state],input=b'fake-core',check=True,timeout=7)
        self.assertEqual(len(list(state.glob('report-*'))),8)
        with self.daemon('crashd',state) as (s,p):
            reports=rpc(s,{'op':'list'},11002)['reports']
            self.assertEqual(len(reports),8)
            self.assertTrue(all(r['core_bytes']==9 and r['signal']==11 for r in reports))
            self.assertEqual(rpc(s,{'op':'list'},11000)['reports'],[])
            self.assertFalse(rpc(s,{'op':'remove','id':reports[0]['id']},11002)['ok'])
            self.assertFalse(rpc(s,{'op':'remove','id':'../../builtin'},1000)['ok'])
            self.assertTrue(rpc(s,{'op':'remove','id':reports[0]['id']},1000)['ok'])
            self.assertEqual(len(rpc(s,{'op':'list'})['reports']),7)
