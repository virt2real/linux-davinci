export PATH=/opt/codesourcery/arm-2010q1/bin:./../uboot/tools:$PATH
echo $PATH
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- install

