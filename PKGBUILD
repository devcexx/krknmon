pkgname=krknmon
pkgver=0.3
pkgrel=1
pkgdesc="NZXT Kraken 4th generation AIO cooler (X53, X63, X73) HWMon driver"
arch=('x86_64')
url="https://github.com/devcexx/krknmon"
license=('GPL3')
source=("Makefile"
        "krknmon.c"
	"dkms.conf")
sha256sums=('eae60aa70481ffcc0d558fa0f7a4fb379e615fcc7298fcfbc2ee593e601cf445'
            '818da094dde768235f706859f4ec83113c9b5e7c3d36709f167a358cf2ab9ce5'
            '99ba940d74ce3dc41e23d76d869b2c9c2717895af3a04bc099e2d54afe06025e')
validpgpkeys=()

build() {
	make
}

package() {
	ROOTDIR="${pkgdir}" make install
}
