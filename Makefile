#
#
#  Copyright (c) 2008  Guillemot Corporation S.A. 
#
#  Philip Lukidis plukidis@guillemot.com
#  Alexis Rousseau-Dupuis
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
#
#

# Comment/uncomment the following line to enable/disable debugging
#DEBUG = y

ifeq ($(DEBUG),y)
  # "-O" is needed to expand inlines
  DEBFLAGS = -O0 -g -DDEBUG 
else
  #DEBFLAGS = -O2
  DEBFLAGS = -Os
endif

EXTRA_CFLAGS += $(DEBFLAGS) -I$(LDDINC) -Wall -Wshadow -Wuninitialized

TARGET = hdj_mod

INSTALLDIR = /lib/modules/$(shell uname -r)/kernel/sound/usb

ifneq ($(KERNELRELEASE),)

hdj_mod-objs := device.o bulk.o configuration_manager.o midi.o midicapture.o midirender.o

obj-m	:= hdj_mod.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD) modules

endif

install:
	install -d $(INSTALLDIR)
	install -c $(TARGET).ko $(INSTALLDIR)
	
uninstall:
	rm -f $(INSTALLDIR)/$(TARGET).ko

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

depend .depend dep:
	$(CC) $(EXTRA_CFLAGS) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
