#export PATH=/opt/codesourcery/arm-2013q1/bin:$PATH
export PATH=/home/dlinyj/install-sdk/codesourcery/arm-2013.05/bin:./../uboot/tools:$PATH
echo $PATH
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- modules
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- modules_install
