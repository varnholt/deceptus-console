import json
from contextlib import contextmanager
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from assets import collision_masks
from tiled import TiledMap, collision_grid
from world_export import export_world

ROOT = Path(__file__).resolve().parents[1]


class CollisionRoleTests(unittest.TestCase):
    @contextmanager
    def temporary(self):
        workspace = ROOT / 'build/tests/collision_roles'
        workspace.mkdir(parents=True,exist_ok=True)
        folder = workspace / f'{os.getpid()}-{self._testMethodName}'
        folder.mkdir(exist_ok=True)
        yield folder

    def test_box_only_boundary_does_not_support_the_player(self):
        with self.temporary() as directory:
            self.assertTrue(Path(directory).resolve().is_relative_to((ROOT/'build/tests/collision_roles').resolve()))
            path = Path(directory) / 'roles.tmx'
            path.write_text('''<map orientation="orthogonal" width="4" height="4" tilewidth="8" tileheight="8">
                <objectgroup name="level"><object x="0" y="24" width="32" height="8"/></objectgroup>
                <objectgroup name="box_colliders"><object id="88" x="8" y="0" width="16" height="24"/></objectgroup>
                </map>''')
            recipe = dict(collision_layers=['level','box_colliders'],one_way_layers=[])
            masks = collision_masks(TiledMap(path),[0,0,32,32],recipe)
            self.assertEqual(collision_grid(masks['player']),[0]*12+[1]*4)
            self.assertEqual(masks['box_only'].getbbox(),(8,0,24,24))
            self.assertEqual(masks['player'].getpixel((8,0)),0)

    def test_original_mausoleum_box88_cannot_create_a_hovering_player_platform(self):
        source = ROOT.parent / 'deceptus_engine/data/level-graveyard/graveyard.tmx'
        if not source.exists():
            self.skipTest('authoritative graveyard checkout unavailable')
        # box88 begins at1656; the authored gravestone surface begins at1680.
        crop = [2496,1656,48,72]
        recipe = dict(collision_layers=['level','box_colliders'],one_way_layers=['level_solid_onesided'],
                      collision_cell=8,coverage_threshold=.5,spawn=[16,24],patrol=[-100,24],door=[-100,24])
        tiled = TiledMap(source)
        masks = collision_masks(tiled,crop,recipe)
        self.assertEqual(masks['box_only'].getbbox(),(0,0,48,72))
        self.assertEqual(masks['player'].getpixel((16,0)),0)
        self.assertEqual(masks['player'].getpixel((16,24)),255)
        # role separation changes collision only; source artwork stays at1:1.
        artwork = tiled.render(crop,['level','level-2']).tobytes()
        with self.temporary() as directory:
            self.assertTrue(Path(directory).resolve().is_relative_to((ROOT/'build/tests/collision_roles').resolve()))
            folder = Path(directory)
            masks['player'].save(folder/'collision.png')
            masks['one_way'].save(folder/'one_way.png')
            (folder/'mechanisms.json').write_text(json.dumps(dict(definitions=[],targets=[])))
            export_world(folder,folder,recipe)
            cells = (folder/'collision.bin').read_bytes()
            self.assertEqual(cells[:18],bytes(18))
            self.assertEqual(cells[18:24],bytes([1]*6))
            self.assertIn('room_collision',(folder/'room.c').read_text())
        self.assertEqual(artwork,tiled.render(crop,['level','level-2']).tobytes())
