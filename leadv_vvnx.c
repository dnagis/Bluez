/** gcc -o leadv leadv_vvnx.c -I/initrd/mnt/dev_save/packages/bluez-5.47 -lbluetooth 
 * 
 * voir lescan en premier. Celui ci c'est pour advertiser pour faire du scan rsp si possible
 * et passer des paramètres vers l'esp32
 * 
 * hci_le_set_advertise_enable() définie dans lib/hci_lib.h -- 
 * 		Correspond à:
 * 			Core specs > Core Sys Pkg [BR/EDR Ctrller Vol] Spec Vol. 2 > Part E - HCI func specs > 7-HCI cmds + evts > LE ctrller commands > p 1259 LE Set Advertising Enable Command
 * 
 * 
 * 
 * **/

#include <stdio.h>
#include <unistd.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "lib/hci_lib.h"


int main()
{
	int dd;
	
	dd = hci_open_dev(0);
	sleep(5);
	hci_close_dev(dd);
	
	
	
	
	return 0;
}
