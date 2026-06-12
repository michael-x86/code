BOOT_SIZE=$(stat -c%s bootloader.bin)
BOOT_SECTORS=$(((BOOT_SIZE + 511) / 512))
BOOT_BYTES=$((BOOT_SECTORS * 512))

KERNEL_SIZE=$(stat -c%s kernel.bin)
KERNEL_SECTORS=$(((KERNEL_SIZE + 511) / 512))
KERNEL_BYTES=$((KERNEL_SECTORS * 512))

echo "Boot: $BOOT_SECTORS sectors, $BOOT_BYTES bytes"
echo "Kernel: $KERNEL_SECTORS sectors, $KERNEL_BYTES bytes"
echo "boot.asm: mov al,$KERNEL_SECTORS"
