#!/usr/bin/env bash
# one command

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERB="${1:-editor}"
TREE="$ROOT/build-linux"

usage() {
	echo
	echo "  ./Build.sh            build the editor into build-linux/ (Debug)"
	echo "  ./Build.sh tests      ...run the test suite"
	echo "  ./Build.sh run        launch the editor"
	echo "  ./Build.sh shell      shell with pinned toolchain on PATH"
	echo "  ./Build.sh clean      delete build-linux/, start over"
	echo
	echo "  README.md has the same steps"
}

failed() {
	echo
	echo "  BUILD FAILED."
	exit 1
}

case "$VERB" in
help | -h | --help)
	usage
	exit 0
	;;
clean)
	echo "  deleting $TREE"
	read -r -p "   continue? [y/n] " answer
	if [ "$answer" != "y" ]; then
		echo "  nothing deleted."
		exit 0
	fi
	rm -rf "$TREE"
	echo "  gone. Run ./Build.sh to configure/build again."
	exit 0
	;;
editor | tests | run | shell) ;;
*)
	echo
	echo "  \"$VERB\" is not something this script knows how to build."
	usage
	exit 1
	;;
esac

if ! command -v python3 > /dev/null 2>&1; then
	echo
	echo "  Python 3 was not found on PATH."
	failed
fi
echo "[1/4] python      $(command -v python3)"

if [ ! -e "$ROOT/.git" ]; then
	echo "[2/4] submodules  SKIPPED: not a git checkout"
	echo
	echo "  Downloading the ZIP off GitHub doesn't bring submodules with"
	echo "  it, the engine doesn't build without them. Clone instead:"
	echo "    git clone --recursive https://github.com/mewiof/Shalltear.git"
	exit 1
fi
if ! command -v git > /dev/null 2>&1; then
	echo "  git was not found on PATH (submodules cannot be fetched)."
	exit 1
fi
echo "[2/4] submodules  googletest, Vulkan-Headers, VMA, freetype, ufbx, box3d"
git -C "$ROOT" submodule update --init --recursive || failed

echo "[3/4] prebuilts   dxc, llvm-mingw, cmake, ninja, linux-x64"
python3 "$ROOT/Scripts/fetch_prebuilt.py" --toolchain || failed

export PATH="$ROOT/External/Prebuilt/llvm-mingw/bin:$ROOT/External/Prebuilt/cmake/bin:$ROOT/External/Prebuilt/ninja:$PATH"
if [ ! -x "$ROOT/External/Prebuilt/llvm-mingw/bin/clang++" ]; then
	echo "  compiler is still missing after the fetch -> expected it at"
	echo "  $ROOT/External/Prebuilt/llvm-mingw/bin"
	exit 1
fi
echo "[4/4] toolchain   $ROOT/External/Prebuilt/llvm-mingw/bin/clang++"
echo

if [ "$VERB" = "shell" ]; then
	echo "  Pinned toolchain is on PATH for this shell. cmake, ninja and"
	echo "  clang++ resolve to External/Prebuilt/."
	echo "  Type \`exit\` to leave it."
	echo
	cd "$ROOT" && exec "${SHELL:-bash}"
fi

if [ "$VERB" = "run" ]; then
	if [ ! -x "$TREE/bin/Shalltear" ]; then
		echo "  build-linux/bin/Shalltear doesn't exist yet -> run ./Build.sh."
		failed
	fi
	echo "==== Launching the editor ===="
	shift
	"$TREE/bin/Shalltear" "$@" &
	exit 0
fi

echo "==== Configuring build-linux/ ===="
cmake -S "$ROOT" --preset linux-debug || failed
echo
echo "==== Building ===="
cmake --build "$TREE" --parallel || failed
echo
echo "  Editor:  $TREE/bin/Shalltear"
echo "  Run it:  ./Build.sh run"

if [ "$VERB" = "tests" ]; then
	echo
	echo "==== Tests ===="
	cd "$TREE/bin" || failed
	"$TREE/bin/ShalltearTests"
	code=$?
	if [ "$code" != "0" ]; then
		echo
		echo "  the suite exited $code"
		failed
	fi
fi
exit 0
