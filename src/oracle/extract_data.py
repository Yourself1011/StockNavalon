"""Extracts the game's rules data from its Unity assets (docs/oracle.md).

Writes every GameLogicData<version> and AvatarData text asset in data.unity3d
to <out>/<name>.txt. Run by setup.sh with UnityPy installed.
"""

import os
import sys

import UnityPy


def main():
    assets, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    count = 0
    for obj in UnityPy.load(assets).objects:
        if obj.type.name != "TextAsset":
            continue
        data = obj.read()
        if data.m_Name.startswith("GameLogicData") or data.m_Name == "AvatarData":
            script = data.m_Script
            if isinstance(script, str):
                script = script.encode("utf-8", "surrogateescape")
            with open(os.path.join(out, data.m_Name + ".txt"), "wb") as f:
                f.write(script)
            count += 1
    print(f"{count} data files")


if __name__ == "__main__":
    main()
