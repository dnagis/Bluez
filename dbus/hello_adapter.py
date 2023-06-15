#!/usr/bin/env python3

#cf bluez-X.YY/test/test-adapter
#API: cf bluez-X.YY/doc/adapter-api.txt


import dbus

bus = dbus.SystemBus()

#J'hardcode car à mon avis toujours hci0 dans mon cas, sinon cf. bluez-X.YY/test/luezutils.py 
adapter = dbus.Interface(bus.get_object("org.bluez", "/org/bluez/hci0"),
					"org.freedesktop.DBus.Properties")
					
#Check power state (bluez-X.YY/doc/adapter-api.txt)					
powered = adapter.Get("org.bluez.Adapter1", "Powered")
print("powered: ", powered)

if (powered == 0):
	print("power = off --> set à ON")
	adapter.Set("org.bluez.Adapter1", "Powered", dbus.Boolean(1))
