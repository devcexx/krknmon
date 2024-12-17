pkgname=krknmon
pkgver=0.4
pkgrel=1
pkgdesc="NZXT Kraken 4th generation AIO cooler (X53, X63, X73) HWMon driver"
arch=('x86_64')
url="https://github.com/devcexx/krknmon"
license=('GPL3')
source=("Makefile"
        "krknmon.c"
	"dkms.conf")
sha256sums=('SKIP'
            'SKIP'
            'SKIP')
validpgpkeys=()

build() {
	make
}

package() {
	ROOTDIR="${pkgdir}" make install
}
