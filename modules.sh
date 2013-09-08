export PATH=/opt/codesourcery/arm-2013q1/bin:$PATH
echo $PATH
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- modules
make ARCH=arm CROSS_COMPILE=arm-none-linux-gnueabi- modules_install
