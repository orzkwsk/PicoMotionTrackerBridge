"""Real UDP/CLI tests using synthetic wire v1 bytes; no hardware or private SDK."""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

EXE = str(Path(sys.argv.pop(1)).resolve())


def packet(kind, seq, payload):
    return struct.pack('<IHHQII', 0x42544f50, 1, kind, 2**64-1, seq, len(payload)) + payload


def pose(seq=1, serial='synthetic-"\\\nテ', x=0.1):
    payload = struct.pack('<32siIB3xQqq13dBB', serial.encode(), 7, 5, 2,
                          9007199254740993, 777, 666, x, -0.2, 0.3,
                          0.1, 0.2, 0.3, 0.9, 1, 2, 3, 4, 5, 6, 0, 1)
    return packet(1, seq, payload)


def qpc():
    value = subprocess.check_output(['powershell', '-NoProfile', '-Command',
        '[Console]::WriteLine(("{0},{1}" -f [Diagnostics.Stopwatch]::GetTimestamp(),[Diagnostics.Stopwatch]::Frequency))'], text=True)
    return tuple(int(v) for v in value.strip().split(','))


class CaptureCLI(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='pico-cli-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.raw = self.root / 'capture テ.ndjson'
        self.ready = self.root / 'ready.json'
        self.stop = self.root / 'stop'
        self.completed = self.root / 'completed.json'
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1',0))
            self.port = sock.getsockname()[1]

    def start(self, *extra):
        self.process = subprocess.Popen([EXE,'--port',str(self.port),'--output',str(self.raw),
            '--stop-file',str(self.stop),'--ready-file',str(self.ready),
            '--completion-file',str(self.completed),'--flush-interval','100',*extra],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        self.addCleanup(self.kill)
        for _ in range(200):
            if self.ready.exists():
                try: return json.loads(self.ready.read_text())
                except ValueError: pass
            if self.process.poll() is not None:
                self.fail(self.process.communicate()[1].decode())
            time.sleep(0.02)
        self.fail('capture readiness timed out')

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.communicate()

    def finish(self):
        self.stop.touch()
        stdout, stderr = self.process.communicate(timeout=10)
        self.assertEqual(self.process.returncode,0,stderr.decode())
        self.assertEqual(stdout,b'')
        self.assertEqual(json.loads(self.completed.read_text())['exit_code'],0)
        return [json.loads(line) for line in self.raw.read_text(encoding='utf-8').splitlines()]

    def test_udp_fields_errors_and_dotnet_qpc(self):
        readiness = self.start()
        before = qpc() if os.name == 'nt' else None
        state=packet(2,2,struct.pack('<32siIBq',b'synthetic-state',7,7,3,555))
        values = [pose(), state, b'bad', b'', pose(3,x=float('nan')),
                  packet(3,4,struct.pack('<Qq',12,13)),packet(4,5,struct.pack('<Qqqq',12,13,14,15))]
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
            for value in values: sock.sendto(value,('127.0.0.1',self.port))
        time.sleep(0.1)
        after = qpc() if os.name == 'nt' else None
        records=self.finish()
        self.assertEqual(len(records),len(values))
        self.assertEqual([v['record_type'] for v in records],['pose','device_state','decode_error','decode_error','decode_error','time_sync_request','time_sync_response'])
        self.assertEqual(records[0]['serial'],'synthetic-"\\\nテ')
        self.assertEqual(records[0]['orientation_xyzw'],[0.1,0.2,0.3,0.9])
        self.assertEqual(records[0]['position_m'],[0.1,-0.2,0.3])
        self.assertEqual(records[0]['sender_session_id'],2**64-1)
        self.assertEqual(records[0]['source_timestamp_ns'],9007199254740993)
        self.assertFalse(records[0]['position_valid'])
        self.assertTrue(records[0]['orientation_sample_present'])
        self.assertEqual(records[4]['decode_status'],'invalid_numeric_value')
        self.assertEqual(records[3]['wire_size_bytes'],0)
        if before:
            for record in records:
                self.assertLessEqual(before[0],record['pc_stopwatch_ticks'])
                self.assertLessEqual(record['pc_stopwatch_ticks'],after[0])
                self.assertEqual(record['pc_stopwatch_frequency'],before[1])
                self.assertEqual(record['pc_stopwatch_frequency'],after[1])
                self.assertEqual(record['pc_clock_kind'],'windows-qpc')
        self.assertEqual(readiness['pc_stopwatch_frequency'],records[0]['pc_stopwatch_frequency'])

    def test_exclusive_bind_and_no_overwrite(self):
        self.start()
        second=subprocess.run([EXE,'--port',str(self.port),'--output',str(self.root/'other.ndjson'),'--duration-sec','1'],capture_output=True)
        self.assertEqual(second.returncode,2)
        self.assertFalse((self.root/'other.ndjson').exists())
        self.finish()
        original=self.raw.read_bytes()
        second=subprocess.run([EXE,'--port',str(self.port),'--output',str(self.raw),'--duration-sec','1'],capture_output=True)
        self.assertEqual(second.returncode,2)
        self.assertEqual(self.raw.read_bytes(),original)

    def test_duration_and_frequency_mismatch(self):
        result=subprocess.run([EXE,'--port',str(self.port),'--output',str(self.raw),'--expected-frequency','1'],capture_output=True)
        self.assertEqual(result.returncode,2)
        self.assertFalse(self.raw.exists())
        self.start('--duration-sec','1')
        self.process.wait(timeout=5)
        self.assertEqual(self.finish(),[])

    def test_five_trackers_120hz_smoke(self):
        self.start()
        started=time.perf_counter()
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
            for cycle in range(240):
                for tracker in range(5):
                    sock.sendto(pose(cycle*5+tracker,serial=f'synthetic-{tracker}'),('127.0.0.1',self.port))
                remaining=started+(cycle+1)/120-time.perf_counter()
                if remaining>0: time.sleep(remaining)
        time.sleep(0.1)
        records=self.finish()
        self.assertEqual(len(records),1200)
        self.assertEqual([r['wire_sequence'] for r in records],list(range(1200)))
        print(f'synthetic UDP capture: 1200/1200 datagrams, {time.perf_counter()-started:.3f}s elapsed')


if __name__ == '__main__':
    unittest.main()
