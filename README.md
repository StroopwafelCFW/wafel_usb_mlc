# wafel_usb_mlc

This plugin will mount the first USB storage device it finds as the internal memory (MLC). It's similar to MLC only redNAND.

## How to use

- Copy the `5usbmlc.ipx` to `sd:/wiiu/ios_plugins` and optionally `/storage_slc/sys/hax/ios_plugins`
- Create one FAT32 partition as the first partition on the USB device. Create a second partition, which will be used as USB.
- Make sure you have only the USB device connected you want to install to
- If you use the usb partition plugin, make sure to to also put that in `sd:/wiiu/ios_plugins`
- Rebuild the MLC with the wafel_mlc_setup. **This will format the USB storage if it is detected for whatever reason as unformatted**

## Building

```bash
export STROOPWAFEL_ROOT=/path/too/stroopwafel-repo
make
```
