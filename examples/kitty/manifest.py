# For our client code, we need the asyncio module from MicroPython
include("$(MPY_DIR)/extmod/asyncio/manifest.py")
# Include our client scripts
module("kitty.py", base_path="$(PORT_DIR)/../../client")
module("pn532.py", base_path="$(PORT_DIR)/../../client")
module("font.py", base_path="$(PORT_DIR)/../../client/font")
module("writer.py", base_path="$(PORT_DIR)/../../client/font")
