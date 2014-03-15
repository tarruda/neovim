. scripts/common.sh

msgpack_ver=0.5.8
msgpack_repo=msgpack/msgpack-c
msgpack_sha1=da39a3ee5e6b4b0d3255bfef95601890afd80709
msgpack_dir="$pkgroot/third-party/msgpack"

github_download "$msgpack_repo" "$msgpack_ver" "$msgpack_dir" \
	"$msgpack_sha1"

cd "$msgpack_dir"

./configure --prefix="$prefix" --enable-shared=no
make
make install
