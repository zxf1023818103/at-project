"""Run the unmodified SDK packer with current cryptography releases."""
import runpy
import sys
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric import ed25519

# The SDK accesses this removed private constant; RFC 8032 fixes its size at 64.
ed25519._ED25519_SIG_SIZE = 64
script = Path(sys.argv.pop(1)).resolve()
sys.argv[0] = str(script)
sys.path.insert(0, str(script.parent))
runpy.run_path(str(script), run_name='__main__')
