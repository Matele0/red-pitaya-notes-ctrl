source /opt/Xilinx/2025.2.1/Vitis/settings64.sh

DATE=`date +%Y%m%d`

make NAME=sdr_transceiver_hpsdr_122_88 PART=xc7z020clg400-1 all

sudo sh scripts/image.sh scripts/debian.sh red-pitaya-debian-13-armhf-$DATE.img 2048
zip red-pitaya-debian-13-armhf-$DATE.zip red-pitaya-debian-13-armhf-$DATE.img
