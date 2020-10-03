pkgname=krknmon
pkgver=0.2
pkgrel=1
pkgdesc="NZXT Kraken 4th generation AIO cooler (X53, X63, X73) HWMon driver"
arch=('x86_64')
url="https://github.com/devcexx/krknmon"
license=('GPL3')
source=("Makefile"
        "krknmon.c"
	"dkms.conf")
sha256sums=('5599de3d396e3714315ff64b99e40869a74529e17c074b791dc61f4829fbf87f'
            '90803a99e541149ffb6f1cbf81972a6320ffe14e598e239521bec87007e02a7c'
            '99ba940d74ce3dc41e23d76d869b2c9c2717895af3a04bc099e2d54afe06025e')
validpgpkeys=()

build() {
	make
}

package() {
	ROOTDIR="${pkgdir}" make install
}
