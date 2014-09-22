export PATH=/home/dlinyj/install-sdk/codesourcery/arm-2013.05/bin:./../uboot/tools:$PATH
echo $PATH
make -j4 ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- CONFIG_DEBUG_SECTION_MISMATCH=y uImage

