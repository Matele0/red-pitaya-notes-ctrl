source /opt/Xilinx/2025.2.1/Vitis/settings64.sh

DATE=`date +%Y%m%d`

# Build master boot project
make NAME=sdr_transceiver_hpsdr_122_88 PART=xc7z020clg400-1 all

# Build bitstreams for all 122_88 projects in parallel
JOBS=`nproc 2> /dev/null || echo 1`
PRJS="led_blinker_122_88 sdr_receiver_122_88 sdr_receiver_hpsdr_122_88 sdr_receiver_wide_122_88 sdr_transceiver_122_88 sdr_transceiver_ft8_122_88 sdr_transceiver_hpsdr_122_88 sdr_transceiver_wspr_122_88 pulsed_nmr_122_88 vna_122_88"

printf "%s\n" $PRJS | xargs -n 1 -P $JOBS -I {} make NAME={} PART=xc7z020clg400-1 bit

# Build Debian image
sudo sh scripts/image.sh scripts/debian-122_88.sh red-pitaya-debian-122_88-armhf-$DATE.img 2048
zip red-pitaya-debian-122_88-armhf-$DATE.zip red-pitaya-debian-122_88-armhf-$DATE.img
