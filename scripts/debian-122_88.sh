device=$1

boot_dir=`mktemp -d /tmp/BOOT.XXXXXXXXXX`
root_dir=`mktemp -d /tmp/ROOT.XXXXXXXXXX`

linux_dir=tmp/linux-6.12
linux_ver=6.12.52-xilinx

# Choose mirror automatically, depending the geographic and network location
mirror=http://deb.debian.org/debian

distro=trixie
arch=armhf

passwd=changeme
timezone=Europe/Brussels

# Create partitions

parted -s $device mklabel msdos
parted -s $device mkpart primary fat16 4MiB 16MiB
parted -s $device mkpart primary ext4 16MiB 100%

boot_dev=/dev/`lsblk -ln -o NAME -x NAME $device | sed '2!d'`
root_dev=/dev/`lsblk -ln -o NAME -x NAME $device | sed '3!d'`

# Create file systems

mkfs.vfat -v $boot_dev
mkfs.ext4 -F -j $root_dev

# Mount file systems

mount $boot_dev $boot_dir
mount $root_dev $root_dir

# Copy files to the boot file system

cp boot-rootfs.bin $boot_dir/boot.bin

# Install Debian base system to the root file system

debootstrap --foreign --arch $arch $distro $root_dir $mirror

# Install Linux modules

modules_dir=$root_dir/lib/modules/$linux_ver

mkdir -p $modules_dir/kernel

find $linux_dir -name \*.ko -printf '%P\0' | tar --directory=$linux_dir --owner=0 --group=0 --null --files-from=- -zcf - | tar -zxf - --directory=$modules_dir/kernel

cp $linux_dir/modules.order $linux_dir/modules.builtin $modules_dir/

depmod -a -b $root_dir $linux_ver

# Add missing configuration files and packages

cp /etc/resolv.conf $root_dir/etc/
cp /usr/bin/qemu-arm-static $root_dir/usr/bin/

rm $root_dir/etc/apt/sources.list

cp -r debian/etc/apt $root_dir/etc/
cp -r debian/etc/systemd $root_dir/etc/

# Create wrappers for gcc and cc to redirect to the cross-compiler
mkdir -p tmp/bin
echo '#!/bin/sh' > tmp/bin/gcc
echo 'exec arm-linux-gnueabihf-gcc "$@"' >> tmp/bin/gcc
chmod +x tmp/bin/gcc

echo '#!/bin/sh' > tmp/bin/cc
echo 'exec arm-linux-gnueabihf-gcc "$@"' >> tmp/bin/cc
chmod +x tmp/bin/cc

PATH_WRAPPER="$(pwd)/tmp/bin"

# Compile and copy Bazaar homepage CGI server
PATH=$PATH_WRAPPER:$PATH make -C alpine/apps/server CC=gcc clean
PATH=$PATH_WRAPPER:$PATH make -C alpine/apps/server CC=gcc
mkdir -p $root_dir/var/www/apps/server
cp alpine/apps/server/server $root_dir/var/www/apps/server/

# Copy static Bazaar assets
cp alpine/apps/index.html $root_dir/var/www/apps/
cp alpine/apps/index_122_88.html $root_dir/var/www/apps/
cp -r alpine/apps/css $root_dir/var/www/apps/
cp alpine/apps/stop.sh $root_dir/var/www/apps/

# Compile and copy all 122_88 projects
PRJS="led_blinker_122_88 sdr_receiver_122_88 sdr_receiver_hpsdr_122_88 sdr_receiver_wide_122_88 sdr_transceiver_122_88 sdr_transceiver_ft8_122_88 sdr_transceiver_hpsdr_122_88 sdr_transceiver_wspr_122_88 pulsed_nmr_122_88 vna_122_88"

for prj in $PRJS
do
  mkdir -p $root_dir/var/www/apps/$prj

  # Copy server and app source files to target first
  if [ -d projects/$prj/server ]
  then
    cp -r projects/$prj/server/* $root_dir/var/www/apps/$prj/
  fi
  if [ -d projects/$prj/app ]
  then
    cp -r projects/$prj/app/* $root_dir/var/www/apps/$prj/
  fi

  # Compile inside target using the wrapper
  if [ -f $root_dir/var/www/apps/$prj/Makefile ]
  then
    PATH=$PATH_WRAPPER:$PATH make -C $root_dir/var/www/apps/$prj CC=gcc clean
    PATH=$PATH_WRAPPER:$PATH make -C $root_dir/var/www/apps/$prj CC=gcc
  fi

  # Copy bitstream file to target
  cp tmp/$prj.bit $root_dir/var/www/apps/$prj/$prj.bit

  # Clean up source files and makefiles from the target system to save space
  rm -f $root_dir/var/www/apps/$prj/*.c
  rm -f $root_dir/var/www/apps/$prj/*.o
  rm -f $root_dir/var/www/apps/$prj/Makefile
done

# Run debootstrap second stage
chroot $root_dir /debootstrap/debootstrap --second-stage

# Mount virtual filesystems for chroot
mkdir -p $root_dir/proc
mkdir -p $root_dir/sys
mkdir -p $root_dir/dev/pts
mount -t proc proc $root_dir/proc
mount -t sysfs sys $root_dir/sys
mount --bind /dev $root_dir/dev
mount --bind /dev/pts $root_dir/dev/pts

chroot $root_dir <<- EOF_CHROOT
export LANG=C
export LC_ALL=C
export DEBIAN_FRONTEND=noninteractive
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

apt-get update
apt-get -y upgrade

apt-get -y install locales

sed -i "/^# en_US.UTF-8 UTF-8$/s/^# //" etc/locale.gen
locale-gen
update-locale LANG=en_US.UTF-8

ln -sf /usr/share/zoneinfo/$timezone etc/localtime
dpkg-reconfigure tzdata

apt-get -y install openssh-server ca-certificates chrony fake-hwclock \
  usbutils psmisc lsof parted curl vim wpasupplicant hostapd dnsmasq \
  firmware-atheros firmware-brcm80211 firmware-mediatek firmware-realtek \
  iw iptables dhcpcd-base ntfs-3g libubootenv-tool ucspi-tcp

systemctl enable dhcpcd
systemctl enable red-pitaya-bazaar
systemctl enable sdr-transceiver-hpsdr-autostart

systemctl disable hostapd
systemctl disable dnsmasq
systemctl disable nftables
systemctl disable wpa_supplicant

sed -i 's/^#PermitRootLogin.*/PermitRootLogin yes/' etc/ssh/sshd_config

echo root:$passwd | chpasswd

apt-get clean

service chrony stop
service ssh stop

history -c

sync
EOF_CHROOT

# Unmount virtual filesystems
umount $root_dir/dev/pts
umount $root_dir/dev
umount $root_dir/sys
umount $root_dir/proc

cp -r debian/etc $root_dir/

rm $root_dir/etc/resolv.conf
rm $root_dir/usr/bin/qemu-arm-static

# Unmount file systems

umount $boot_dir $root_dir

rmdir $boot_dir $root_dir

zerofree $root_dev
