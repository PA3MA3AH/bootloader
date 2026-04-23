#!/usr/bin/env bash
set -e

IMG=disk.img
SIZE=128M
MNT=mnt

if [ -f "$IMG" ]; then
    echo "[mkdisk] $IMG уже существует — пропуск. 'make disk-rebuild' пересоздаст."
    exit 0
fi

truncate -s "$SIZE" "$IMG"
parted "$IMG" --script mklabel msdos
parted "$IMG" --script mkpart primary fat32 1MiB 100%

LOOP=$(sudo losetup -Pf --show "$IMG")
trap 'sudo umount "$MNT" 2>/dev/null || true; sudo losetup -d "$LOOP" 2>/dev/null || true' EXIT

sudo mkfs.fat -F 32 "${LOOP}p1" >/dev/null

mkdir -p "$MNT"
sudo mount "${LOOP}p1" "$MNT"

# seed-файлы для тестов FAT32 R/W
sudo sh -c "echo 'Hello, FAT32!' > $MNT/HELLO.TXT"
sudo sh -c "echo '1234567890ABCDEF' > $MNT/TEST.TXT"
sudo dd if=/dev/zero of=$MNT/ZEROES.BIN bs=1024 count=4 status=none

sudo umount "$MNT"
rmdir "$MNT"

echo "[mkdisk] $IMG создан: FAT32 + 3 тестовых файла (HELLO.TXT, TEST.TXT, ZEROES.BIN)"
