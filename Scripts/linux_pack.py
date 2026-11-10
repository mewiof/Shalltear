import io
import lzma
import os
import shutil
import subprocess
import tarfile
from pathlib import Path

TRIPLE = "x86_64-unknown-linux-gnu"
MULTIARCH = "x86_64-linux-gnu"

PACK_NAME = "linux-x64"

LLVM_VERSION = "23.1.1"
LLVM_ARCHIVE = f"LLVM-{LLVM_VERSION}-Linux-X64"
LLVM_URL = f"https://github.com/llvm/llvm-project/releases/download/llvmorg-{LLVM_VERSION}/{LLVM_ARCHIVE}.tar.xz"
LLVM_SHA256 = "832aeb58d105de1cabc7b982dd2c65de0610f7377df48ae8fc2dd8e97420a15c"

DEBIAN_SNAPSHOT = "20260601T000000Z"
DEBIAN_URL = f"https://snapshot.debian.org/archive/debian/{DEBIAN_SNAPSHOT}"

SYSROOT_PACKAGES = (
	("pool/main/g/glibc/libc6_2.31-13+deb11u11_amd64.deb", "05f7264da867b37f4c5ce49266b558ea1e81e05a9464f623152fca70f3550282"),
	("pool/main/g/glibc/libc6-dev_2.31-13+deb11u11_amd64.deb", "e7f7b45d9c5cfcf37609f0b6efd3c645272c812144703af89dfd32218fcb0fd3"),
	("pool/main/l/linux/linux-libc-dev_5.10.223-1_amd64.deb", "e3be603bd12377bb90fcfb0f2048ca3a81c886e49d6f09945b82159d1c54f7b2"),
	("pool/main/g/gcc-10/libgcc-s1_10.2.1-6_amd64.deb", "e478f2709d8474165bb664de42e16950c391f30eaa55bc9b3573281d83a29daf"),
	("pool/main/libx/libx11/libx11-dev_1.7.2-1+deb11u2_amd64.deb", "9fbcda6af06d8db9cd8a48b581570aa8fee862bfc43b6f569a66503ae7d83211"),
	("pool/main/x/xorgproto/x11proto-dev_2020.1-1_all.deb", "d5568d587d9ad2664c34c14b0ac538ccb3c567e126ee5291085a8de704a565f5"),
	("pool/main/libx/libxi/libxi-dev_1.7.10-1_amd64.deb", "736309ff476f0e1594f855cf44e2fb20bf1e594518ce2259eb9b2dd93917f2db"),
	("pool/main/libx/libxrandr/libxrandr-dev_1.5.1-1_amd64.deb", "67d40174015e8b4e3e2fe3b6fb2990943321a168e0fbb2d12082f637914a0a2e"),
	("pool/main/libx/libxrender/libxrender-dev_0.9.10-1_amd64.deb", "135ed7c8a589e17d21718a91b5a7da48159f33c85e0b337aae9b9f484d3a4954"),
	("pool/main/libx/libxcursor/libxcursor-dev_1.2.0-2_amd64.deb", "c84c43ad4d596bd673288f6035ced4755468b873149181936a0c1fc99cff78aa"),
	("pool/main/libx/libxfixes/libxfixes-dev_5.0.3-2_amd64.deb", "bacfcd67ca931839a2e2ae87922ecb40e2870470afa86d0c7245288825da2340"),
	("pool/main/libx/libxext/libxext-dev_1.3.3-1.1_amd64.deb", "0aa17565287ca8a37914e043789ee33c6e1f987acf346dac7175165009c5db7c"),
	("pool/main/w/wayland/libwayland-dev_1.18.0-2~exp1.1_amd64.deb", "3265bf05c0cea760d0e8f5fb5fc68b0f154911de23503e02232dfa59f6b6490c"),
	("pool/main/libx/libxkbcommon/libxkbcommon-dev_1.0.3-2_amd64.deb", "1202d8c64e670876b58f5b3d3f797b1848ec462f7caeef8b1e597ecea18570b6"),
)

# bump when a pin above/layout below changes
PACK_VERSION = f"2-llvm{LLVM_VERSION}-debian{DEBIAN_SNAPSHOT}"

RUNTIME_ARCHIVES = ("libc++.a", "libc++abi.a")
COMPILER_RT_FILES = ("libclang_rt.builtins.a", "clang_rt.crtbegin.o", "clang_rt.crtend.o")
SHARED_RUNTIMES = ("libc++abi.so.1", "libc++.so.1")

# system unwinder (JIT frame registration broken by a 2nd one in process)
UNWINDER = f"/lib/{MULTIARCH}/libgcc_s.so.1"

LINKER_SCRIPTS = {
	"libgcc_s.so": f"INPUT({UNWINDER})\n",
	"libc++abi.so": "INPUT(libc++abi.so.1)\n",
	"libc++.so": "INPUT(libc++.so.1 libc++abi.so.1)\n",
}


def pack_dir(prebuilt_dir):
	return Path(prebuilt_dir) / PACK_NAME


def sysroot_dir(prebuilt_dir):
	return pack_dir(prebuilt_dir) / "sysroot"


def library_dir(prebuilt_dir):
	return sysroot_dir(prebuilt_dir) / "usr" / "lib" / MULTIARCH


def is_current(prebuilt_dir):
	marker = pack_dir(prebuilt_dir) / ".fetched"
	return marker.exists() and marker.read_text(encoding="utf-8").strip() == PACK_VERSION


def _ar_members(data):
	if data[:8] != b"!<arch>\n":
		raise RuntimeError("not an ar archive")
	offset = 8
	while offset + 60 <= len(data):
		header = data[offset:offset + 60]
		name = header[:16].decode("ascii").strip().rstrip("/")
		size = int(header[48:58].decode("ascii").strip())
		offset += 60
		yield name, data[offset:offset + size]
		offset += size + (size & 1)


def _unpack_deb(data, sysroot, links):
	for name, payload in _ar_members(data):
		if not name.startswith("data.tar"):
			continue
		if name.endswith(".xz"):
			payload = lzma.decompress(payload)
		elif name != "data.tar":
			raise RuntimeError(f"unsupported package payload '{name}'")
		with tarfile.open(fileobj=io.BytesIO(payload)) as tar:
			for member in tar:
				relative = member.name[2:] if member.name.startswith("./") else member.name.lstrip("/")
				if not relative:
					continue
				target = sysroot / relative
				if member.isdir():
					target.mkdir(parents=True, exist_ok=True)
				elif member.isfile():
					target.parent.mkdir(parents=True, exist_ok=True)
					target.write_bytes(tar.extractfile(member).read())
				elif member.issym():
					links.append((relative, member.linkname))
		return
	raise RuntimeError("package has no data member")


def _resolve_links(sysroot, links):
	# links copied (Windows has no unprivileged symlinks)
	pending = list(links)
	for _ in range(8):
		unresolved = []
		for relative, linkname in pending:
			target = sysroot / relative
			if linkname.startswith("/"):
				source = sysroot / linkname.lstrip("/")
			else:
				source = (target.parent / linkname)
			source = Path(os.path.normpath(source))
			if source.is_file():
				target.parent.mkdir(parents=True, exist_ok=True)
				shutil.copyfile(source, target)
			elif source.is_dir():
				if not target.exists():
					shutil.copytree(source, target)
			else:
				unresolved.append((relative, linkname))
		if len(unresolved) == len(pending):
			break
		pending = unresolved


def _fetch_sysroot(sysroot, fetch_archive):
	links = []
	for filename, sha256 in SYSROOT_PACKAGES:
		archive = fetch_archive(f"{DEBIAN_URL}/{filename}", sha256, Path(filename).name)
		_unpack_deb(archive.read_bytes(), sysroot, links)
	_resolve_links(sysroot, links)
	if not (sysroot / "usr" / "lib" / MULTIARCH / "libc.so").is_file():
		raise RuntimeError("libc.so is absent from the unpacked sysroot. Package layout changed?")
	if not (sysroot / UNWINDER.lstrip("/")).is_file():
		raise RuntimeError("libgcc_s.so.1 is absent from the unpacked sysroot. Package layout changed?")
	if not (sysroot / "usr" / "include" / "X11" / "Xlib.h").is_file():
		raise RuntimeError("X11/Xlib.h is absent from the unpacked sysroot. Package layout changed?")


def _fetch_llvm_runtimes(pack, sysroot, fetch_archive):
	archive = fetch_archive(LLVM_URL, LLVM_SHA256, f"{LLVM_ARCHIVE}.tar.xz")
	include = sysroot / "usr" / "include"
	library = sysroot / "usr" / "lib" / MULTIARCH
	compiler_rt = pack / "compiler-rt"
	routes = (
		(f"{LLVM_ARCHIVE}/include/c++/v1/", include / "c++" / "v1", None),
		(f"{LLVM_ARCHIVE}/include/{TRIPLE}/c++/v1/", include / TRIPLE / "c++" / "v1", None),
		(f"{LLVM_ARCHIVE}/lib/{TRIPLE}/", library, RUNTIME_ARCHIVES),
		(f"{LLVM_ARCHIVE}/lib/clang/{LLVM_VERSION.split('.')[0]}/lib/{TRIPLE}/", compiler_rt, COMPILER_RT_FILES),
	)
	extracted = 0
	print(f"  unpacking the C++ runtime out of {archive.name} (reads the whole archive)")
	with tarfile.open(archive, mode="r|xz") as tar:
		for member in tar:
			if not member.isfile():
				continue
			for prefix, destination, allowed in routes:
				if not member.name.startswith(prefix):
					continue
				relative = member.name[len(prefix):]
				if allowed is not None and relative not in allowed:
					continue
				out = destination / relative
				out.parent.mkdir(parents=True, exist_ok=True)
				out.write_bytes(tar.extractfile(member).read())
				extracted += 1
	for name in RUNTIME_ARCHIVES:
		if not (library / name).is_file():
			raise RuntimeError(f"{name} was not found in {archive.name}. Layout changed?")
	for name in COMPILER_RT_FILES:
		if not (compiler_rt / name).is_file():
			raise RuntimeError(f"{name} was not found in {archive.name}. Layout changed?")
	if not (include / TRIPLE / "c++" / "v1" / "__config_site").is_file():
		raise RuntimeError(f"__config_site was not found in {archive.name}. Layout changed?")
	print(f"  {extracted} file(s) unpacked")


# compiler-rt searched by clang only in own resource dir
def install_compiler_rt(prebuilt_dir, clang):
	resource = subprocess.run([str(clang), "-print-resource-dir"], capture_output=True, text=True, check=True).stdout.strip()
	destination = Path(resource) / "lib" / TRIPLE
	destination.mkdir(parents=True, exist_ok=True)
	for name in COMPILER_RT_FILES:
		source = pack_dir(prebuilt_dir) / "compiler-rt" / name
		target = destination / name
		if not target.is_file() or target.read_bytes() != source.read_bytes():
			shutil.copyfile(source, target)


def _link_shared_runtimes(sysroot, library, clang):
	common = [
		str(clang), f"--target={TRIPLE}", f"--sysroot={sysroot}", "-fuse-ld=lld", "-rtlib=compiler-rt",
		"-nostdlib++", "-shared", "-Wl,--no-undefined", "-Wl,--as-needed",
	]

	def link(output, archive, needs):
		command = common + [
			f"-Wl,-soname,{output}", "-o", str(library / output),
			"-Wl,--whole-archive", str(library / archive), "-Wl,--no-whole-archive",
		] + [str(library / need) for need in needs] + ["-lpthread", "-ldl", "-lm", "-lrt", "-lc"]
		result = subprocess.run(command, capture_output=True, text=True)
		if result.returncode != 0:
			raise RuntimeError(f"linking {output} failed:\n{result.stdout}{result.stderr}")

	for name, script in LINKER_SCRIPTS.items():
		(library / name).write_bytes(script.encode("ascii"))
	link("libc++abi.so.1", "libc++abi.a", ["libgcc_s.so"])
	link("libc++.so.1", "libc++.a", ["libc++abi.so.1", "libgcc_s.so"])


# fetch_archive(url, sha256, name) -> verified local Path
def fetch(prebuilt_dir, fetch_archive, clang):
	pack = pack_dir(prebuilt_dir)
	if is_current(prebuilt_dir):
		print(f"Linux target pack {PACK_VERSION} already built. Skipping")
		install_compiler_rt(prebuilt_dir, clang)
		return
	print(f"building the Linux target pack {PACK_VERSION} (glibc 2.31 sysroot + LLVM {LLVM_VERSION} C++ runtime, ~2 GB download)")
	if pack.exists():
		shutil.rmtree(pack)
	sysroot = pack / "sysroot"
	sysroot.mkdir(parents=True)
	_fetch_sysroot(sysroot, fetch_archive)
	_fetch_llvm_runtimes(pack, sysroot, fetch_archive)
	install_compiler_rt(prebuilt_dir, clang)
	_link_shared_runtimes(sysroot, library_dir(prebuilt_dir), clang)
	(pack / ".fetched").write_text(PACK_VERSION + "\n", encoding="utf-8")
	print(f"Linux target pack -> {pack}")
