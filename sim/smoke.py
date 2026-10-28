"""Boot the factory image in Renode and print the console. Handy for demos.

    python sim/smoke.py [build/fw]
"""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from renode import Renode  # noqa: E402

build = Path(sys.argv[1] if len(sys.argv) > 1 else "build/fw")
r = Renode(build)
try:
    r.setup()
    r.start()
    r.uart.expect("app running", timeout=60)
    time.sleep(0.5)
    print(r.uart.text())
finally:
    r.close()
