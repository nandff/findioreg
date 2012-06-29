findioreg
=========

traverse IORegistryEntry tree and print properties (Mac OS X)

Example:
  To search USB uart bridge, whose vendor id/product id is 0x10c4/0xea60
  and serial number is "SN1001" and print tty device name:

	% findioreg -c IOUSBDevice -i idVendor=0x10c4 -i idProduct=0xea60 \
		-s "USB Serial Number"="SN1001" -P IOTTYDevice

	-> SLAB_USBtoUART17

  then you can use "/dev/cu.SLAB_USBtoUART17".
