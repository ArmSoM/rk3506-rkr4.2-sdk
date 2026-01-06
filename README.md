# rk3506-rkr4.2-sdk

# Prerequisites
Note: WSL is not supported.
```
sudo apt update && sudo apt-get upgrade -y
sudo apt install -y git gcc g++ make libncurses5-dev libssl-dev bc bison flex build-essential libncursesw5-dev device-tree-compiler libglib2.0-dev wget cpio unzip rsync file python3 bc build-essential libncurses5-dev libncursesw5-dev make bc bison flex
```
## Additional host dependencies (Ubuntu 24)

- The following packages complement the existing prerequisite list and are required by some build and packaging scripts.
```bash
sudo apt update && sudo apt upgrade -y

sudo apt install -y \
  git gcc g++ make build-essential bc bison flex \
  libmpc-dev libgmp-dev libncurses5-dev libncursesw5-dev \
  libssl-dev libglib2.0-dev device-tree-compiler \
  wget cpio unzip rsync file \
  python3 python-is-python3
```
## Python 2.7 installation (required by legacy build tools)

- Some Rockchip build scripts still depend on Python 2.7.
- This section only applies if Python 2 is not already available on your system.
```bash
wget https://www.python.org/ftp/python/2.7.18/Python-2.7.18.tgz
tar -xzvf Python-2.7.18.tgz
cd Python-2.7.18

./configure --enable-optimizations
make
sudo make altinstall

python2.7 --version
```
- Create a compatibility symlink:
```bash
sudo ln -s /usr/local/bin/python2.7 /usr/local/bin/python2
```
## Compilation steps

### Select profile

Enter the top-level directory of SDK source code, execute the following command, and select the board level configuration file

```bash
./build.sh lunch
```
After running this code it will show:
```bash
1. armsom_cm1_nand_squashfs_rofs_defconfig
2. armsom_cm1_nand_squashfs_rofs_thunderboot_defconfig
3. armsom_cm1_nand_ubi_rwfs_defconfig
4. armsom_cm1_nand_ubi_rwfs_thunderboot_defconfig
5. armsom_cm1_sd_defconfig
6. armsom_forge1_nand_squashfs_rofs_defconfig
7. armsom_forge1_nand_squashfs_rofs_thunderboot_defconfig
8. armsom_forge1_nand_ubi_rwfs__thunderboot_defconfig
9. armsom_forge1_nand_ubi_rwfs_defconfig
10. armsom_forge1_sd_defconfig
Which would you like? [1]:
```
- Choose 5 if you want to use sdcard.

### Compilation

After selecting the board level configuration, compile it completely once directly

```
./build.sh
```
Compile the Uboot:

```
./build.sh uboot
```

Compile the Kernel:

```
./build.sh kernel
```
## Writing the firmware image to a Micro SD card
- If the file to be placed on the SD card has been generated correctly, you can use the Windows application "Rockchip Create Upgrade Disk Tool" (SDDiskTool) to write it to the memory card. To do this:
1. Navigate to the output directory shown after the build process (`rk3506-rkr4.2-sdk/output/firmware`)
2. Locate the `update.img` file
3. Specify its path in the SDDiskTool application

**Additionally, to change the application language to English:**
- Open the `config.ini` file
- Modify the specified section as shown in the accompanying image
![Armsom SD card writer](https://sisoog.com/wp-content/uploads/2025/10/armsom_sdcard_writer_.png)
Connect the memory card to your computer, follow the on-screen installation instructions, then insert the card into the development board.
![armsom_SDcard](https://sisoog.com/wp-content/uploads/2025/10/armsom_SDcard.png)

### Problem discussion

Common issues can be discussed on the [forum](https://forum.armsom.org/c/armsom-cm1/12 "跳转到armsom forum")

### Tips

The LVGL display conflicts with the Weston display (required by QT). The user will see the LVGL interface appear, but after 1-2 seconds, the screen goes black again. After logging into the system and rebooting, execute mv /etc/init.d/S49weston / and the display will return to normal.
