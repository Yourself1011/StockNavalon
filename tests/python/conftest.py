# Under ctest, test the module just built by CMake (in the build tree) rather
# than whatever pip installed last.
import os
import sys

package = os.environ.get("STOCKNAVALON_BUILD_PACKAGE")
if package:
    sys.path.insert(0, package)
    # an editable pip install adds an import hook that would win over the
    # path; drop it so the build tree's copy is the one imported
    sys.meta_path[:] = [
        finder for finder in sys.meta_path
        if not type(finder).__module__.startswith("_editable_skbc_stocknavalon")
    ]
