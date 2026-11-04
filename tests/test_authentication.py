"""Device authentication with real Unix peers and private, durable policy fixtures."""
import array
import contextlib
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time
import unittest
from test_system_services import rpc

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('LEON_TEST_BIN', REPO / 'build/host'))
PIN = '739182'
PASSWORD = 'Device passphrase 2026!'


@unittest.skipUnless(os.geteuid() == 0, 'authentication fixtures require root')
class Authentication(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='leon-auth-')
        self.root = Path(self.temp.name)
        self.root.chmod(0o755)
        self.state = self.root / 'state'
        self.endpoint = self.root / 'run/control.sock'
        self.process = None
        self.start()

    def tearDown(self):
        if self.process is not None:
            self.stop()
        self.temp.cleanup()

    def start(self, boot='fixture-boot-a', test=True):
        args = [BUILD / 'bin/authd', '--state-dir', self.state, '--socket', self.endpoint]
        if test:
            args += ['--test', '--boot-id', boot]
        self.process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self.fail(self.process.stderr.read().decode())
            try:
                self.call('status')
                return
            except (OSError, ValueError):
                time.sleep(.02)
        self.fail('authd did not become ready')

    def stop(self, crash=False):
        (self.process.kill if crash else self.process.terminate)()
        out, error = self.process.communicate(timeout=5)
        if not crash:
            self.assertEqual(self.process.returncode, 0, error.decode())
        self.assertNotIn(PIN.encode(), out + error)
        self.assertNotIn(PASSWORD.encode(), out + error)
        self.process = None

    def call(self, op, uid=0, **values):
        return rpc(self.endpoint, dict(op=op, **values), uid)

    def unlock(self, secret=''):
        reply = self.call('authenticate', secret=secret, purpose='unlock')
        self.assertTrue(reply['ok'], reply)
        result = self.call('consume', token=reply['token'], purpose='unlock')
        self.assertTrue(result['ok'], result)
        self.assertFalse(result['locked'])
        return reply['token']

    def configure(self, kind='pin', secret='', replacement=PIN):
        reply = self.call('configure', uid=11000, kind=kind, secret=secret, new_secret=replacement)
        self.assertTrue(reply['ok'], reply)
        return reply

    def enrolled(self):
        self.unlock()
        self.configure()
        self.call('lock')

    @contextlib.contextmanager
    def monitor(self):
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(3)
            client.connect(str(self.endpoint))
            client.sendall(b'{"op":"watch"}\n')
            reader = client.makefile('rb', buffering=0)
            self.assertTrue(json.loads(reader.readline())['locked'])
            try:
                yield client, reader
            finally:
                reader.close()

    def test_separate_policy_rotation_and_disable_require_current_credential(self):
        self.assertFalse(self.call('status')['configured'])
        self.assertFalse(self.call('configure', kind='pin', new_secret=PIN)['ok'])
        self.unlock()
        self.configure()
        path = self.state / 'policy.json'
        original = json.loads(path.read_text())
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.state.stat().st_mode & 0o777, 0o700)
        self.assertNotIn(PIN, path.read_text())
        self.assertEqual(original['kdf'], 'scrypt-32768-8-1')
        self.configure('password', PIN, PASSWORD)
        changed = json.loads(path.read_text())
        self.assertNotEqual(original['salt'], changed['salt'])
        self.assertNotEqual(original['verifier'], changed['verifier'])
        self.assertFalse(self.call('configure', kind='none', secret=PIN)['ok'])
        time.sleep(1.05)
        self.configure('none', PASSWORD, '')
        self.assertFalse(self.call('status')['configured'])
        self.stop()
        self.start()
        self.assertFalse(self.call('status')['configured'])
        self.assertTrue(self.call('status')['locked'])

    def test_retry_limits_survive_crash_and_new_boot_without_wall_clock(self):
        self.enrolled()
        for attempt in range(5):
            self.assertFalse(self.call('authenticate', secret='bad-password')['ok'])
            if attempt < 4:
                time.sleep(1.05)
        before = self.call('status')['retry_after_ms']
        self.assertGreater(before, 28000)
        self.stop(crash=True)
        self.start()
        self.assertGreater(self.call('status')['retry_after_ms'], 27000)
        self.assertFalse(self.call('authenticate', secret=PIN)['ok'])
        self.stop()
        self.start(boot='fixture-boot-b')
        self.assertGreater(self.call('status')['retry_after_ms'], 29000)
        policy = json.loads((self.state / 'policy.json').read_text())
        self.assertEqual(policy['failures'], 5)
        self.assertEqual(policy['boot'], 'fixture-boot-b')

    def test_grants_are_one_use_epoch_bound_and_cannot_cross_purpose(self):
        self.enrolled()
        reply = self.call('authenticate', secret=PIN)
        token = reply['token']
        self.assertFalse(self.call('consume', token=token, purpose='settings')['ok'])
        self.assertFalse(self.call('consume', token=token, purpose='unlock', action='other')['ok'])
        self.assertFalse(self.call('consume', uid=1000, token=token, purpose='unlock')['ok'])
        self.assertTrue(self.call('consume', token=token, purpose='unlock')['ok'])
        self.assertFalse(self.call('consume', token=token, purpose='unlock')['ok'])
        self.call('lock')
        token = self.call('authenticate', secret=PIN)['token']
        self.call('lock')
        self.assertFalse(self.call('consume', token=token, purpose='unlock')['ok'])
        self.assertTrue(self.call('status')['locked'])
        self.unlock(PIN)

    def test_expired_grant_is_rejected(self):
        token = self.call('authenticate')['token']
        time.sleep(15.1)
        self.assertFalse(self.call('consume', token=token, purpose='unlock')['ok'])
        self.assertTrue(self.call('status')['locked'])

    def test_settings_grants_are_bound_to_pid_uid_and_action(self):
        self.enrolled()
        self.unlock(PIN)
        self.assertFalse(self.call('authenticate', uid=1000, purpose='settings', secret=PIN)['ok'])
        token = self.call('authenticate', uid=11000, purpose='settings', action='security', secret=PIN)['token']
        self.assertFalse(self.call('consume', uid=11000, purpose='settings', action='security', token=token)['ok'])
        code = '''import json,os,socket,sys
os.setgroups([]);os.setgid(11000);os.setuid(11000)
def call(r):
 s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1]);s.sendall(json.dumps(r).encode()+b'\\n');s.shutdown(socket.SHUT_WR)
 out=b''
 while b:=s.recv(4096):out+=b
 s.close();return json.loads(out)
a=call(dict(op='authenticate',purpose='settings',action='security',secret=sys.argv[2]))
b=call(dict(op='consume',purpose='settings',action='security',token=a['token']))
print(json.dumps(b))
'''
        p = subprocess.run(['python3', '-c', code, str(self.endpoint), PIN], check=True,
                           capture_output=True, text=True, timeout=5)
        self.assertTrue(json.loads(p.stdout)['ok'])

    def test_monitor_heartbeat_during_kdf_consumption_and_disconnect_lock(self):
        self.enrolled()
        with self.monitor() as (client, reader):
            with socket.socket(socket.AF_UNIX) as worker:
                worker.connect(str(self.endpoint))
                worker.sendall(json.dumps(dict(op='authenticate', secret=PIN)).encode() + b'\n')
                start = time.monotonic()
                self.assertTrue(self.call('status')['locked'])
                self.assertLess(time.monotonic() - start, .25)
                reply = json.loads(worker.makefile('rb').readline())
            # Let a heartbeat become pending while the consume command arrives.
            time.sleep(.3)
            client.sendall(json.dumps(dict(op='consume', purpose='unlock', token=reply['token'])).encode() + b'\n')
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                reply = json.loads(reader.readline())
                if not reply['locked']:
                    break
            self.assertFalse(reply['locked'])
        deadline = time.monotonic() + 2
        while not self.call('status')['locked'] and time.monotonic() < deadline:
            time.sleep(.02)
        self.assertTrue(self.call('status')['locked'])

    def test_production_rejects_forged_uids_and_ordinary_apps(self):
        self.stop()
        self.start(test=False)
        for uid in (1000, 11000, 11001, 65534):
            with self.assertRaises((OSError, ValueError, subprocess.CalledProcessError)):
                self.call('authenticate', uid=uid, secret='')
        self.assertFalse(rpc(self.endpoint, dict(op='verify_peer', pid=os.getpid(), uid=1000, role='shell'))['trusted'])
        self.assertTrue(self.call('status')['locked'])

    def test_invalid_policy_never_resets_or_unlocks(self):
        self.enrolled()
        self.stop()
        path = self.state / 'policy.json'
        original = path.read_bytes()
        for mode, content in ((0o644, original), (0o600, b'{}'), (0o600, b'not json')):
            path.write_bytes(content)
            path.chmod(mode)
            self.start()
            state = self.call('status')
            self.assertFalse(state['ok'])
            self.assertTrue(state['locked'])
            self.assertFalse(self.call('authenticate', secret=PIN)['ok'])
            self.assertFalse(self.call('configure', kind='none')['ok'])
            self.stop()
            self.assertEqual(path.read_bytes(), content)
        path.unlink()
        path.symlink_to(self.root / 'missing')
        self.start()
        self.assertFalse(self.call('status')['ok'])

    def test_validation_transport_and_control_capacity(self):
        self.unlock()
        for kind, secret in [('pin', '12345'), ('pin', '12345a'), ('pin', '1' * 13),
                             ('password', 'short'), ('password', 'x' * 129),
                             ('password', 'invalid\nsecret'), ('none', 'not empty')]:
            self.assertFalse(self.call('configure', kind=kind, new_secret=secret)['ok'])
        self.configure()
        self.call('lock')
        self.assertTrue(self.call('authenticate', secret=PIN, kind='x' * 1500, new_secret='x' * 1500)['ok'])
        slow = []
        try:
            for _ in range(10):
                client = socket.socket(socket.AF_UNIX)
                client.connect(str(self.endpoint))
                client.sendall(b'{"op":')
                slow.append(client)
            self.assertTrue(self.call('status')['ok'])
            with self.monitor() as (_, reader):
                self.assertTrue(json.loads(reader.readline())['locked'])
        finally:
            for client in slow:
                client.close()
        for data in (b'{}\x00\n', b'{"op":"authenticate","secret":"bad\\u0000value"}\n', b'x' * 4096):
            with socket.socket(socket.AF_UNIX) as client:
                client.settimeout(3)
                client.connect(str(self.endpoint))
                client.sendall(data)
                try:
                    result = client.recv(4096)
                    if result:
                        self.assertFalse(json.loads(result)['ok'])
                except ConnectionResetError:
                    pass
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(3)
            client.connect(str(self.endpoint))
            fd = os.open('/dev/null', os.O_RDONLY)
            try:
                client.sendmsg([b'{"op":"status"}\n'], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [fd]))])
            finally:
                os.close(fd)
            try:
                self.assertFalse(client.recv(4096))
            except ConnectionResetError:
                pass
        self.assertTrue(self.call('status')['ok'])
