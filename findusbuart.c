/*
 * Copyright (C) 2012 Nozomu Ando <nand@mac.com> and others, see file forum.txt
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>

#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#define EXIT_SUCCESS		0
#define EXIT_NOTFOUND		1
#define EXIT_ERROR		2
#define EXIT_IOREGCHANGE	3

typedef struct find_device_match_s {
  CFNumberRef		vendor_id;
  CFNumberRef		product_id;
  int			address_p;
  USBDeviceAddress	address;
  CFNumberRef		location_id;
  CFStringRef		product_string;
  CFStringRef		vendor_string;
  CFStringRef		serial_number_string;
} find_device_match_t;

#define cfptr_set(var, val)	do { \
    if (var) \
      CFRelease(var); \
    (var) = (val); \
} while (0);

#define ioptr_set(var, val)	do { \
    if (var) \
      IOObjectRelease(var); \
    (var) = (val); \
} while (0);

int
check_equal(io_service_t service, CFTypeRef val, CFStringRef propertyname)
{
  CFTypeRef property;
  Boolean r;

  if (!val)
    return 1;

  property = IORegistryEntryCreateCFProperty(service, propertyname, NULL, 0);
  if (!property)
    return 0;

  r = CFEqual(val, property);
  CFRelease(property);
  return !!r;
}

int
check_substring(io_service_t service, CFStringRef string, CFStringRef property)
{
  CFStringRef s;
  CFRange r;

  if (!string)
    return 1;

  s = IORegistryEntryCreateCFProperty(service, property, NULL, 0);
  if (!s)
    return 0;

  r = CFStringFind(s, string, 0);
  CFRelease(s);
  return !!r.length;
}

int
findDevice(mach_port_t masterPort, find_device_match_t *match,
	   CFStringRef *ttydevicename)
{
  kern_return_t err = KERN_SUCCESS;
  CFMutableDictionaryRef mat_dict = 0;
  io_iterator_t iterator = 0;
  io_service_t usbDevice = 0;
  io_service_t usbDeviceDescendant = 0;
  CFStringRef ttydev = 0;
  int rc = EXIT_SUCCESS;
  int found;

  mat_dict = IOServiceMatching(kIOUSBDeviceClassName);
  if (mat_dict == 0) {
    fprintf(stderr, "failed: IOServiceMatching(kIOUSBDeviceClassName)\n");
    err = kIOReturnError;
  }

  if (err == KERN_SUCCESS) {
    err = IOServiceGetMatchingServices(masterPort, mat_dict, &iterator);
    mat_dict = 0;	/* IOServiceGetMatchingServices release mat_dict */

    if (err != KERN_SUCCESS)
      fprintf(stderr, "failed: IOServiceGetMatchingServices: err = 0x%x\n",
	      err);
  }

  if (err == KERN_SUCCESS) {
    while ((usbDevice = IOIteratorNext(iterator)) != 0) {
      if (check_equal(usbDevice, match->vendor_id, CFSTR(kUSBVendorID))
	  && check_equal(usbDevice, match->product_id, CFSTR(kUSBProductID))
	  && check_equal(usbDevice, match->location_id,
			 CFSTR(kUSBDevicePropertyLocationID))
	  && check_equal(usbDevice, match->product_string,
			 CFSTR(kUSBProductString))
	  && check_equal(usbDevice, match->vendor_string,
			 CFSTR(kUSBVendorString))
	  && check_equal(usbDevice, match->serial_number_string,
			 CFSTR(kUSBSerialNumberString)))
	break;

      ioptr_set(usbDevice, 0);
    }
    if (err == KERN_SUCCESS && !IOIteratorIsValid(iterator)) {
      fprintf(stderr,
	      "IORegistry modified while traversing(1), may need retry\n");
      rc = EXIT_IOREGCHANGE;
    }

    ioptr_set(iterator, 0);
  }

  if (usbDevice) {
    err = IORegistryEntryCreateIterator((io_registry_entry_t)usbDevice,
					kIOServicePlane,
					kIORegistryIterateRecursively,
					&iterator);
    if (err != KERN_SUCCESS) {
      fprintf(stderr, "failed: IORegistryEntryCreateIterator: "
	      "err = 0x%x\n", err);
    }

    while (err == KERN_SUCCESS
	   && (usbDeviceDescendant = IOIteratorNext(iterator)) != 0) {
      CFStringRef s;

      s = IORegistryEntryCreateCFProperty(usbDeviceDescendant,
					  CFSTR(kIOClassKey),
					  NULL, 0);
      found = 0;
      if (s) {
	found = (CFStringCompare(s, CFSTR("IOSerialBSDClient"), 0)
		 == kCFCompareEqualTo);
	CFRelease(s);
      }
      if (found)
	break;

      ioptr_set(usbDeviceDescendant, 0);
    }

    if (err == KERN_SUCCESS && !IOIteratorIsValid(iterator)) {
      fprintf(stderr,
	      "IORegistry modified while traversing(2), may need retry\n");
      rc = EXIT_IOREGCHANGE;
    }

    ioptr_set(iterator, 0);
    ioptr_set(usbDevice, 0);
  }

  if (usbDeviceDescendant)
    ttydev = IORegistryEntryCreateCFProperty(usbDeviceDescendant,
					     CFSTR("IOTTYDevice"), NULL, 0);
  ioptr_set(usbDeviceDescendant, 0);

  if (err != KERN_SUCCESS)
    rc = EXIT_ERROR;
  else if (rc == EXIT_SUCCESS) {
    if (ttydev)
      *ttydevicename = ttydev;
    else
      rc = EXIT_NOTFOUND;
  }
  return rc;
}

CFStringRef
make_CFString(const char *s)
{
  CFStringRef r;

  r = CFStringCreateWithCString(NULL, s, kCFStringEncodingMacRoman);
  if (!r) {
    fprintf(stderr, "cannot make CFString from `%s'\n", s);
    exit(1);
  }
  return r;
}

CFNumberRef
make_CFNumberSInt32(SInt32 i)
{
  return CFNumberCreate(NULL, kCFNumberSInt32Type, &i);
}

CFNumberRef
make_CFNumberSInt64(SInt64 i)
{
  return CFNumberCreate(NULL, kCFNumberSInt64Type, &i);
}

void
usage()
{
  fprintf(stderr, "usage:\n");
  exit(2);
}

int
main(int argc, char **argv)
{
  kern_return_t err = KERN_SUCCESS;
  mach_port_t masterPort = 0;
  CFStringRef ttydevicename = 0;
  find_device_match_t *match;
  int ch;
  int rc;

  err = IOMasterPort (MACH_PORT_NULL, &masterPort);
  if (err != KERN_SUCCESS)
    {
      fprintf(stderr, "failed: IOMasterPort: err = 0x%08x\n", err);
      exit (1);
    }

  match = calloc(1, sizeof(find_device_match_t));
  if (match == 0)
    exit(1);

  while ((ch = getopt(argc, argv, "v:p:l:P:V:S:")) != -1)
    switch (ch) {
    case 'v':
      cfptr_set(match->vendor_id,
		make_CFNumberSInt32((SInt32)strtol(optarg, NULL, 0)));
      break;
    case 'p':
      cfptr_set(match->product_id,
		make_CFNumberSInt32((SInt32)strtol(optarg, NULL, 0)));
      break;
    case 'l':
      cfptr_set(match->location_id,
		make_CFNumberSInt32((SInt32)strtol(optarg, NULL, 0)));
      break;
    case 'P':
      cfptr_set(match->product_string, make_CFString(optarg));
      break;
    case 'V':
      cfptr_set(match->vendor_string, make_CFString(optarg));
      break;
    case 'S':
      cfptr_set(match->serial_number_string, make_CFString(optarg));
      break;
    case 'h':
    default:
      usage();
    }
  argc -= optind;
  argv += optind;

  rc = findDevice(masterPort, match, &ttydevicename);

  if (ttydevicename)
    fprintf(stdout, "%s\n",
	    CFStringGetCStringPtr(ttydevicename, kCFStringEncodingMacRoman));

  cfptr_set(ttydevicename, 0);

  mach_port_deallocate(mach_task_self(), masterPort);

  exit(rc);
}
