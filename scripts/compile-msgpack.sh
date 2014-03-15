. scripts/common.sh

msgpack_ver=cpp-0.5.8
msgpack_repo=msgpack/msgpack-c
msgpack_sha1=e8791a6a6c51895455ebef347d7b7d8f364970c9
msgpack_dir="$pkgroot/third-party/msgpack"

github_download "$msgpack_repo" "$msgpack_ver" "$msgpack_dir" \
	"$msgpack_sha1"

cd "$msgpack_dir"

./bootstrap
./configure --prefix="$prefix" --enable-shared=no
make
make install
