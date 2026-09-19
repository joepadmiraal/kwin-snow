# Arch package

The release workflow creates `kwin-effect-snow-${pkgver}.tar.gz` from the
release tag and runs `makepkg` with this recipe. For a local build, create the
same archive from a tagged checkout in the directory containing `PKGBUILD`.

Because native C++ KWin effects have no stable out-of-tree ABI, rebuild this
package whenever Arch updates KWin.
