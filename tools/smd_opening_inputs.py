"""Prepare portable opening metadata directly from validated original imports."""
import hashlib
import json
from pathlib import Path

from snes_tutorial_assets import compile_worlds,ensure_original_imports


def prepare_smd_metadata(data,output,prepared_imports=None):
    """Emit simulation C only; no SNES palette images or graphics are produced."""
    recipes,identity=prepared_imports or ensure_original_imports(Path(data).resolve(),refresh_metadata=True)
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    sources=compile_worlds(recipes,output)
    report=dict(source_identity=identity,c_sources=sources,scope='portable simulation declarations from original imports',
                c_sha256={name:hashlib.sha256((output/name).read_bytes()).hexdigest() for name in sources})
    (output/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    return output
