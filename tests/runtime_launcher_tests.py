"""Fixed runtime checks using inert files and isolated user-state fixtures."""
from pathlib import Path
from unittest.mock import patch
from contextlib import redirect_stdout
import importlib.util,io,os,tempfile,unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('runtime_launcher',ROOT/'scripts/launch_runtime.py')
launch=importlib.util.module_from_spec(spec);spec.loader.exec_module(launch)

class RuntimeTests(unittest.TestCase):
    def setUp(self):
        test_root=ROOT/'Build/Tests';test_root.mkdir(parents=True,exist_ok=True)
        self.directory=tempfile.TemporaryDirectory(prefix='Receiver with spaces ',dir=test_root)
        self.root=Path(self.directory.name).resolve()
        assert self.root.resolve().is_relative_to(test_root.resolve())
        self.original=launch.ROOT;launch.ROOT=self.root
        self.environment=patch.dict(os.environ,{'LOCALAPPDATA':str(self.root/'User state')});self.environment.start()
    def tearDown(self):
        self.environment.stop();launch.ROOT=self.original;self.directory.cleanup()
    def ready(self,state=True):
        for rel in ('Release/pico_ot_bridge_service.exe','control/Release/pico_ot_steamvr_control.exe'):
            path=self.root/'Build/runtime'/rel;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'inert fixture')
        if state:
            path=self.root/'User state/PicoMotionTrackerBridge';path.mkdir(parents=True)
            (path/'observation-provider.conf').write_text('fixture identity')
            (path/'output_route.txt').write_text('steamvr\n')
    def test_missing_artifacts_and_state_are_separate(self):
        result=launch.check();self.assertEqual(len(result['missing_files']),2);self.assertEqual(len(result['state_missing']),2)
    def test_pure_check_preserves_configured_route(self):
        self.ready()
        with patch.object(launch.subprocess,'Popen',side_effect=AssertionError('process forbidden')),patch.object(launch.subprocess,'run',side_effect=AssertionError('process forbidden')),redirect_stdout(io.StringIO()):self.assertEqual(launch.main(['--check']),0)
        self.assertEqual(launch.check()['output_route'],'steamvr')
    def test_missing_state_is_not_ready_check(self):
        self.ready(False)
        with patch.object(launch.subprocess,'Popen',side_effect=AssertionError('process forbidden')),redirect_stdout(io.StringIO()):self.assertEqual(launch.main(['--check']),1)
    def test_fixed_target_and_tray_argument(self):
        self.ready();args=launch.check(True)['argv']
        self.assertEqual(args[0],str(self.root/'Build/runtime/control/Release/pico_ot_steamvr_control.exe'))
        self.assertIn(str(self.root/'Build/runtime/Release/pico_ot_bridge_service.exe'),args);self.assertIn('--tray-only',args)
    def test_application_failure_is_not_retried(self):
        self.ready()
        with patch.object(launch.subprocess,'Popen') as spawn,redirect_stdout(io.StringIO()):
            spawn.return_value.wait.return_value=37
            self.assertEqual(launch.main([]),37);self.assertEqual(spawn.call_count,1)

if __name__=='__main__':unittest.main()
