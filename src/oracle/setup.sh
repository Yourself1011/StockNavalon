#!/bin/sh
# Pins a copy of the Steam build for the oracle (docs/oracle.md):
#   src/oracle/setup.sh [path to Polytopia.app]
# Everything goes into game/ (gitignored): game/<version>-<build>/ with the
# library, metadata, rules data and decompiled class layout, game/current
# pointing at it, and the tools in game/tools/.
set -e
cd "$(dirname "$0")/../.."
APP=${1:-"$HOME/Library/Application Support/Steam/steamapps/common/The Battle of Polytopia/Polytopia.app"}
CONTENTS="$APP/Contents"
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$CONTENTS/Info.plist")
# "2.18.1.16768" -> 2.18.1-16768
DIR=game/$(echo "$VERSION" | sed 's/\.\([0-9]*\)$/-\1/')
CPP2IL_RELEASE=2022.1.0-pre-release.21
UNITY_VERSION=$(head -c 64 "$CONTENTS/Resources/Data/data.unity3d" | strings | sed -n 3p | sed 's/f.*//')

echo "pinning $VERSION (Unity $UNITY_VERSION) into $DIR"
mkdir -p "$DIR/Metadata" game/tools
cp "$CONTENTS/Info.plist" "$DIR/"
cp "$CONTENTS/Resources/Data/il2cpp_data/Metadata/global-metadata.dat" "$DIR/Metadata/"
lipo -thin arm64 "$CONTENTS/Frameworks/GameAssembly.dylib" -output "$DIR/GameAssembly-arm64.dylib"

# the rules data, with UnityPy in its own environment
export UV_CACHE_DIR="$PWD/game/tools/uv-cache"
[ -x game/tools/venv/bin/python ] || uv venv -q game/tools/venv
uv pip install -q --python game/tools/venv/bin/python UnityPy
game/tools/venv/bin/python src/oracle/extract_data.py \
    "$CONTENTS/Resources/Data/data.unity3d" "$DIR/gamedata"

# the class layout (diffable C#) and method bodies (ISIL), with Cpp2IL
if [ ! -x game/tools/Cpp2IL ]; then
    curl -sL -o game/tools/Cpp2IL \
        "https://github.com/SamboyCoding/Cpp2IL/releases/download/$CPP2IL_RELEASE/Cpp2IL-$CPP2IL_RELEASE-OSX-ARM64"
    chmod +x game/tools/Cpp2IL
    # unsigned arm64 binaries are killed on launch
    codesign -s - -f game/tools/Cpp2IL
fi
export DOTNET_BUNDLE_EXTRACT_BASE_DIR="$PWD/game/tools/.dotnet"
mkdir -p "$DOTNET_BUNDLE_EXTRACT_BASE_DIR"
for format in diffable-cs isil; do
    out=$DIR/cpp2il; [ $format = isil ] && out=$DIR/isil
    game/tools/Cpp2IL --force-binary-path "$DIR/GameAssembly-arm64.dylib" \
        --force-metadata-path "$DIR/Metadata/global-metadata.dat" \
        --force-unity-version "$UNITY_VERSION" \
        --output-as $format --output-to "$out" > /dev/null
done

ln -sfn "$(basename "$DIR")" game/current
echo "done: build the oracle, then ./build/release/oracle game/current"
