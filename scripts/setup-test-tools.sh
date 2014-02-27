. scripts/common.sh

luarocks_dir="$pkgroot/third-party/luarocks"

cd "$luarocks_dir"

./configure --prefix="$prefix" --force-config --with-lua="$prefix" \
	--with-lua-include="$prefix/include/luajit-2.0" \
	--lua-suffix="jit"

make bootstrap

# install tools for testing
luarocks install moonrocks --server=http://rocks.moonscript.org
moonrocks install moonscript
moonrocks install busted
