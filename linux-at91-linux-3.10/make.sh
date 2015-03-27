DESTDIR=/media/sf_shr/

if ! [ -f ".config" ]; then
	make ARCH=arm CROSS_COMPILE=arm-linux- sama5d4_defconfig
	! [ $? = 0 ] && exit 1
fi

make ARCH=arm CROSS_COMPILE=arm-linux- zImage -j5
! [ $? = 0 ] && exit 1

make ARCH=arm CROSS_COMPILE=arm-linux- dtbs
! [ $? = 0 ] && exit 1

cp -f arch/arm/boot/zImage $DESTDIR
cp -f arch/arm/boot/dts/at91-sama5d4_xplained_hdmi.dtb $DESTDIR
