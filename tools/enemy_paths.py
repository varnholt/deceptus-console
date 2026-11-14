"""Translate TmxEnemy::addPaths and orb.lua against original physics contours."""

from functools import lru_cache
import hashlib
from pathlib import Path
import xml.etree.ElementTree as ET


@lru_cache(maxsize=8)
def source_world_chains(map_path):
    """Follow Level::parseObj's clockwise winding and closed-face removal."""
    map_path = Path(map_path)
    chains, hashes = [], {}
    for layer in ET.parse(map_path).getroot().iter('layer'):
        name = layer.get('name')
        if name not in ('level', 'level_solid_onesided'):
            continue
        suffix = 'solid' if name == 'level' else 'solid_onesided'
        path = map_path.parent / f'layer_{name}_{suffix}.obj'
        if not path.exists():
            raise ValueError(f'original collision contours required for generated enemy paths: {path}')
        hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        vertices = []
        offset_x, offset_y = float(layer.get('offsetx', 0)), float(layer.get('offsety', 0))
        for line in path.read_text().splitlines():
            fields = line.split()
            if not fields:
                continue
            if fields[0] == 'v':
                vertices.append((int(float(fields[1]) + offset_x), int(float(fields[2]) + offset_y)))
            elif fields[0] == 'f':
                face = [vertices[int(field.split('/')[0]) - 1] for field in fields[1:]]
                if len(face) < 4 or face[0] != face[-1]:
                    raise ValueError('source physics face is not a closed contour')
                chains.append(list(reversed(face))[:-1])
    return chains, hashes


def intersects(a, b, c, d):
    """Match SfmlMath::intersect, including its parallel-edge exclusion."""
    s1 = (b[0] - a[0], b[1] - a[1])
    s2 = (d[0] - c[0], d[1] - c[1])
    denominator = -s2[0] * s1[1] + s1[0] * s2[1]
    if not denominator:
        return False
    s = (-s1[1] * (a[0] - c[0]) + s1[0] * (a[1] - c[1])) / denominator
    t = (s2[0] * (a[1] - c[1]) - s2[1] * (a[0] - c[0])) / denominator
    return 0 <= s <= 1 and 0 <= t <= 1


def generated_orb_path(data, map_name, obj):
    chains, hashes = source_world_chains(str((Path(data) / map_name).resolve()))
    left, top = int(obj['x']) - 12, int(obj['y']) - 12
    width, height = int(obj['width']) or 24, int(obj['height']) or 24
    corners = [(left, top), (left, top + height), (left + width, top + height), (left + width, top)]
    selected = None
    for chain in chains:
        for index, a in enumerate(chain):
            b = chain[(index + 1) % len(chain)]
            if (left <= a[0] < left + width and top <= a[1] < top + height) or any(
                    intersects(a, b, corners[edge], corners[(edge + 1) % 4]) for edge in range(4)):
                # the source deliberately continues the outer loop: the last
                # matching world chain wins, not the first matching contour.
                selected = chain
                break
    if selected is None:
        raise ValueError(f'orb {obj["id"]}: no original physics contour intersects its source placement')
    # TmxEnemy reverses the path again for the default clockwise world winding.
    path = selected if obj['properties'].get('inverse_path', False) else list(reversed(selected))
    min_x, max_x = min(x for x, _ in path), max(x for x, _ in path)
    min_y, max_y = min(y for _, y in path), max(y for _, y in path)
    factor_x = (max_x - min_x + 24) / (max_x - min_x) if max_x != min_x else 1
    factor_y = (max_y - min_y + 24) / (max_y - min_y) if max_y != min_y else 1
    expanded = [(min_x + (x - min_x) * factor_x - 12,
                 min_y + (y - min_y) * factor_y - 12) for x, y in path]
    if any(abs(x - round(x)) > 1e-6 or abs(y - round(y)) > 1e-6 for x, y in expanded):
        raise ValueError(f'orb {obj["id"]}: expanded path requires subpixel path keys not supported by this import')
    expanded = [(round(x), round(y)) for x, y in expanded]
    if any(a[0] != b[0] and a[1] != b[1] for a, b in zip(expanded, expanded[1:] + expanded[:1])):
        raise ValueError(f'orb {obj["id"]}: diagonal generated paths are not implemented')
    closest = min(range(len(expanded)), key=lambda index: (expanded[index][0] - obj['x']) ** 2 +
                  (expanded[index][1] - obj['y']) ** 2)
    # rotate the closed path so native construction starts on the same nearest
    # source key chosen by orb.lua::alignToSpawnIfNeeded.
    expanded = expanded[closest:] + expanded[:closest]
    return expanded, hashes
