/*
 * $Id: findioreg.c,v 1.3 2012/06/28 23:28:09 nand Exp $
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
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#define EXIT_SUCCESS		0
#define EXIT_NOTFOUND		1
#define EXIT_ERROR		2
#define EXIT_IOREGCHANGE	3
#define EXIT_USAGE		4

#define FINDIOREG_MATCH_SIZE	100
#define FINDIOREG_ACTION_SIZE	100

#define FINDIOREG_ACTION_RECURSIVE	1
#define FINDIOREG_ACTION_HEXVALUE	2

typedef struct findioreg_match_s {
  CFStringRef		propertyname;
  CFTypeRef		value;
} findioreg_match_t;

#define FINDIOREG_ACTION_VERBOSE_NAME		1
#define FINDIOREG_ACTION_VERBOSE_CLASSNAME	2

typedef struct findioreg_action_s {
  CFTypeRef		propertyname;
  int			action_type;
  int			verbose;
} findioreg_action_t;

typedef struct findioreg_s {
  const char		*name;
  int			name_is_classname;
  findioreg_match_t	match[FINDIOREG_MATCH_SIZE];
  int			match_cnt;
  findioreg_action_t	action[FINDIOREG_ACTION_SIZE];
  int			action_cnt;
} findioreg_t;

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
check_equal(io_service_t service, CFStringRef propertyname, CFTypeRef val)
{
  CFTypeRef property;
  Boolean r;

  property = IORegistryEntryCreateCFProperty(service, propertyname, NULL, 0);
  if (!property)
    return 0;

  if (!val)
    return 1;

  r = CFEqual(val, property);
  CFRelease(property);
  return !!r;
}

int
check_substring(io_service_t service, CFStringRef propertyname,
		CFStringRef string)
{
  CFStringRef s;
  CFRange r;

  s = IORegistryEntryCreateCFProperty(service, propertyname, NULL, 0);
  if (!s)
    return 0;

  if (!string)
    return 1;

  r = CFStringFind(s, string, 0);
  CFRelease(s);
  return !!r.length;
}

void
print_property(io_object_t io_obj, findioreg_action_t *action)
{
  CFTypeRef v;
  CFTypeID typ;
  kern_return_t err;

  v = IORegistryEntryCreateCFProperty(io_obj, action->propertyname, NULL, 0);
  if (v) {
    if (action->verbose >= FINDIOREG_ACTION_VERBOSE_NAME) {
      io_name_t name;

      err = IORegistryEntryGetName(io_obj, name);
      if (err != KERN_SUCCESS) {
	fprintf(stderr, "failed: IORegistryEntryGetName: err = 0x%x\n", err);
	return;
      }
      fprintf(stdout, "%s", name);
      if (action->verbose >= FINDIOREG_ACTION_VERBOSE_CLASSNAME) {
	err = IOObjectGetClass(io_obj, name);
	if (err != KERN_SUCCESS) {
	  fprintf(stderr, "failed: IOObjectGetClass: err = 0x%x\n", err);
	  return;
	}
	fprintf(stdout, "<%s>", name);
      }
      fprintf(stdout, ".%s = ",
	      CFStringGetCStringPtr(action->propertyname,
				    kCFStringEncodingMacRoman));
    }

    typ = CFGetTypeID(v);
    if (typ == CFStringGetTypeID()) {
      fprintf(stdout, "%s\n",
	      CFStringGetCStringPtr(v, kCFStringEncodingMacRoman));
    } else if (typ == CFNumberGetTypeID()) {
      SInt32 s32;
      CFNumberGetValue(v, kCFNumberSInt32Type, &s32); /* XXX */
      fprintf(stdout,
	      (action->action_type & FINDIOREG_ACTION_HEXVALUE)
	      ? "0x%x\n" : "%d\n",
	      s32);
    } else {
      fprintf(stderr, "cannot handle CFType id = %d\n", (int)typ);
    }
    CFRelease(v);
  }
}

int
find_ioreg(mach_port_t master_port, findioreg_t *params)
{
  kern_return_t err = KERN_SUCCESS;
  CFMutableDictionaryRef mat_dict = 0;
  io_object_t io_obj = 0, io_obj_action = 0;
  io_iterator_t find_iterator = 0;
  io_iterator_t action_iterator = 0;
  int rc = EXIT_SUCCESS;
  int found = 0, found_ever = 0;

  if (err == KERN_SUCCESS) {
    if (params->name_is_classname)
      mat_dict = IOServiceMatching(params->name);
    else
      mat_dict = IOServiceNameMatching(params->name);

    if (mat_dict == 0) {
      fprintf(stderr, "failed: IOServiceMatching(%s)\n", params->name);
      err = kIOReturnError;
    }
  }

  if (err == KERN_SUCCESS) {
    err = IOServiceGetMatchingServices(master_port, mat_dict, &find_iterator);
    mat_dict = 0;	/* IOServiceGetMatchingServices release mat_dict */

    if (err != KERN_SUCCESS)
      fprintf(stderr, "failed: IOServiceGetMatchingServices: err = 0x%x\n",
	      err);
  }

  if (err == KERN_SUCCESS) {
    while ((io_obj = IOIteratorNext(find_iterator)) != 0) {
      int i;

      found = 1;
      for (i = 0; found && i < params->match_cnt; i++) {
	if (!check_equal(io_obj, params->match[i].propertyname,
			 params->match[i].value))
	  found = 0;
      }

      if (found) {
	found_ever++;

	for (i = 0; i < params->action_cnt; i++) {
	  if (params->action[i].action_type
	      & FINDIOREG_ACTION_RECURSIVE) {
	    err = IORegistryEntryCreateIterator(io_obj, kIOServicePlane,
						kIORegistryIterateRecursively,
						&action_iterator);
	    if (err != KERN_SUCCESS) {
	      fprintf(stderr, "failed: IORegistryEntryCreateIterator: "
		      "err = 0x%x\n", err);
	    }

	    while (err == KERN_SUCCESS
		   && (io_obj_action = IOIteratorNext(action_iterator)) != 0) {
	      print_property(io_obj_action, &params->action[i]);
	      ioptr_set(io_obj_action, 0);
	    }
	    if (err == KERN_SUCCESS && !IOIteratorIsValid(action_iterator)) {
	      fprintf(stderr,
		      "IORegistry modified while traversing(2), "
		      "may need retry\n");
	      rc = EXIT_IOREGCHANGE;
	    }
	    ioptr_set(action_iterator, 0);
	  } else {
	    print_property(io_obj, &params->action[i]);
	  }
	}
      }

      ioptr_set(io_obj, 0);
    }
    if (err == KERN_SUCCESS && !IOIteratorIsValid(find_iterator)) {
      fprintf(stderr,
	      "IORegistry modified while traversing(1), may need retry\n");
      rc = EXIT_IOREGCHANGE;
    }

    ioptr_set(find_iterator, 0);
  }

  if (err != KERN_SUCCESS)
    rc = EXIT_ERROR;
  else if (rc == EXIT_SUCCESS)
    if (!found_ever)
      rc = EXIT_NOTFOUND;
  
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
get_name_value(const char *s, char **name_ptr, char **value_ptr)
{
  char *buffer, *p;

  buffer = malloc(strlen(s) + 1);
  if (buffer == 0) {
    fprintf(stderr, "malloca failed\n");
    exit(EXIT_ERROR);
  }
  strcpy(buffer, s);

  p = strrchr(buffer, '=');
  if (p) {
    *p = 0;
    *value_ptr = p + 1;
  }
  *name_ptr = buffer;
}

void
usage()
{
  fprintf(stderr, "usage:\n");
  exit(EXIT_USAGE);
}

int
main(int argc, char **argv)
{
  kern_return_t err = KERN_SUCCESS;
  mach_port_t master_port = 0;
  findioreg_t *params;
  char *name, *value;
  int verbose = 0;
  int ch, rc, cnt;

  params = calloc(1, sizeof(findioreg_t));
  if (params == 0)
    exit(1);

  while ((ch = getopt(argc, argv, "c:n:i:s:p:h:P:H:vV:")) != -1)
    switch (ch) {
    case 'c':
      if (params->name)
	usage();
      params->name = optarg;
      params->name_is_classname = 1;
      break;

    case 'n':
      if (params->name)
	usage();
      params->name = optarg;
      break;

    case 'i':
    case 's':
      cnt = params->match_cnt++;
      if (cnt >= FINDIOREG_MATCH_SIZE)
	usage();
      get_name_value(optarg, &name, &value);
      params->match[cnt].propertyname = make_CFString(name);
      if (value)
	params->match[cnt].value =
	  (ch == 's') ? (CFTypeRef)make_CFString(value) :
	  (CFTypeRef)make_CFNumberSInt32((SInt32)strtol(value, NULL, 0));
      break;

    case 'p':
    case 'h':
    case 'P':
    case 'H':
      cnt = params->action_cnt++;
      if (cnt >= FINDIOREG_ACTION_SIZE)
	usage();
      if (strlen(optarg) > 0)
	params->action[cnt].propertyname = make_CFString(optarg);
      if (ch == 'P' || ch == 'H')
	params->action[cnt].action_type |=
	  FINDIOREG_ACTION_RECURSIVE;
      if (ch == 'h' || ch == 'H')
	params->action[cnt].action_type |=
	  FINDIOREG_ACTION_HEXVALUE;
      params->action[cnt].verbose = verbose;
      break;

    case 'v':
      verbose++;
      break;

    case 'V':
      verbose = strtol(optarg, NULL, 0);
      break;

    case '?':
    default:
      usage();
    }
  argc -= optind;
  argv += optind;
  if (!params->name) {
    params->name = "IORegistryEntry";
    params->name_is_classname = 1;
  }

  err = IOMasterPort(MACH_PORT_NULL, &master_port);
  if (err != KERN_SUCCESS) {
    fprintf(stderr, "failed: IOMasterPort: err = 0x%08x\n", err);
    exit(EXIT_ERROR);
  }

  rc = find_ioreg(master_port, params);

  mach_port_deallocate(mach_task_self(), master_port);

  exit(rc);
}
