export PATH=/opt/codesourcery/arm-2013q1/bin:./../uboot/tools:$PATH
echo $PATH
make -j4 ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- CONFIG_DEBUG_SECTION_MISMATCH=y uImage

