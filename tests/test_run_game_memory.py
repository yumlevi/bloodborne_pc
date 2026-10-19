from paths import ROOT
import os
import unittest
from unittest import mock

import run_game


class DirectMemoryBudgetTests(unittest.TestCase):
    def test_native_budget_through_1080p(self):
        for resolution in ('1280x720', '1600x900', '1920x1080'):
            with self.subTest(resolution=resolution):
                self.assertEqual(run_game.direct_memory_mb_for_output(resolution), '5056')

    def test_high_budget_above_1080p(self):
        for resolution in ('1920x1200', '2560x1440', '3840x2160'):
            with self.subTest(resolution=resolution):
                self.assertEqual(run_game.direct_memory_mb_for_output(resolution), '9152')

    def test_unknown_custom_value_is_conservative(self):
        self.assertEqual(run_game.direct_memory_mb_for_output('auto'), '9152')
        self.assertEqual(run_game.direct_memory_mb_for_output(''), '9152')

    def test_automatic_budget_is_recomputed_after_restart(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(run_game.configure_direct_memory('1280x720'), '5056')
            self.assertEqual(os.environ['BB_AUTO_DMEM'], '1')
            run_game.clear_automatic_direct_memory()
            self.assertNotIn('BB_DMEM_MB', os.environ)
            self.assertEqual(run_game.configure_direct_memory('3840x2160'), '9152')

    def test_explicit_override_is_preserved(self):
        with mock.patch.dict(os.environ, {'BB_DMEM_MB': '6144'}, clear=True):
            self.assertEqual(run_game.configure_direct_memory('1280x720'), '6144')
            self.assertNotIn('BB_AUTO_DMEM', os.environ)
            run_game.clear_automatic_direct_memory()
            self.assertEqual(os.environ['BB_DMEM_MB'], '6144')


if __name__ == '__main__':
    unittest.main()
