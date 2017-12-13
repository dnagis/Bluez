/** gcc -o lescan lescan_vvnx.c -I/initrd/mnt/dev_save/packages/bluez-5.47 -lbluetooth
 * 
 * scp lescan_vvnx.c ks:/home/bluez_esp32/ble_pure
 * 
 * Basé sur tools/hcitool.c -- li 2504 surtout
 * Goals: 
 * -Avoir une boucle qui scan ble, pour récup de l'AdvData venant des esp32
 * -Whitelisting
 * -Directed Advertising Packets
 * -Scan Response -> principe?? 
 * 
 * hci_open_dev()
 * cmd_lescan()
 * hci_le_set_scan_parameters() hci_lib.h et hci.c
 * hci_le_set_scan_enable() hci_lib.h et hci.c
 * print_advertising_devices()
 * 
 * 
 * 
 * hcitool dev --> Devices: hci0	00:C2:C6:D1:E8:44
 * 
 * sockets: 
 * 	ls -l /proc/`pidof lescan`/fd
 * 
 */ 

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "lib/hci_lib.h"

static volatile int signal_received = 0;
#define EIR_FLAGS                   0x01  /* flags */
#define EIR_UUID16_SOME             0x02  /* 16-bit UUID, more available */
#define EIR_UUID16_ALL              0x03  /* 16-bit UUID, all listed */
#define EIR_UUID32_SOME             0x04  /* 32-bit UUID, more available */
#define EIR_UUID32_ALL              0x05  /* 32-bit UUID, all listed */
#define EIR_UUID128_SOME            0x06  /* 128-bit UUID, more available */
#define EIR_UUID128_ALL             0x07  /* 128-bit UUID, all listed */
#define EIR_NAME_SHORT              0x08  /* shortened local name */
#define EIR_NAME_COMPLETE           0x09  /* complete local name */
#define EIR_TX_POWER                0x0A  /* transmit power level */
#define EIR_DEVICE_ID               0x10  /* device ID */

void sigint_handler(int sig)
{
	signal_received = sig;
}

static void eir_parse_name(uint8_t *eir, size_t eir_len,
						char *buf, size_t buf_len)
{
	size_t offset;

	offset = 0;
	while (offset < eir_len) {
		uint8_t field_len = eir[0];
		size_t name_len;

		/* Check for the end of EIR */
		if (field_len == 0)
			break;

		if (offset + field_len > eir_len)
			goto failed;

		switch (eir[1]) {
		case EIR_NAME_SHORT:
		case EIR_NAME_COMPLETE:
			name_len = field_len - 1;
			if (name_len > buf_len)
				goto failed;

			memcpy(buf, &eir[2], name_len);
			return;
		}

		offset += field_len + 1;
		eir += field_len + 1;
	}

failed:
	snprintf(buf, buf_len, "(unknown)");
}

void run_lescan(int dd)
{
	unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr;
	struct hci_filter nf, of;
	socklen_t olen;
	struct sigaction sa;
	int len;
	//inspiration: hcitool lescan -> print_advertising_devices()
	//btmon is your friend
	fprintf(stderr, "On est dans run_lescan()...\n");
	
	/**Préparation du Socket**/
	olen = sizeof(of);
	if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) { //setsockopt(3) - Linux man page -- int socket, int level, int option_name, const void *option_value, socklen_t option_len
		printf("Could not get socket options\n");
	}
	
	hci_filter_clear(&nf);
	hci_filter_set_ptype(HCI_EVENT_PKT, &nf); //lib/hci.h
	hci_filter_set_event(EVT_LE_META_EVENT, &nf); //lib/hci.h

	if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
		printf("Could not set socket options\n");
	}
	
	
	/**Préparation du signal handling**/
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_NOCLDSTOP;
	sa.sa_handler = sigint_handler; //ouais c'est obligatoire, foire sinon...
	sigaction(SIGINT, &sa, NULL);
	
	while (1) {
		evt_le_meta_event *meta; //lib/hci.h
		le_advertising_info *info; //lib/hci.h
		char addr[18];
		
		while ((len = read(dd, buf, sizeof(buf))) < 0) {
		
			/**Signal Handling**/
			if(errno == EINTR && signal_received == SIGINT) {
				 fprintf(stderr, "Réception de SIGINT, ciao...\n");
		         goto done;
		    }
	    
		}
		
		ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
		len -= (1 + HCI_EVENT_HDR_SIZE);

		meta = (void *) ptr;
	    
	    if (meta->subevent != 0x02) //je suppose: #define EVT_LE_ADVERTISING_REPORT	0x02 --> lib/hci.h
			goto done;
			
		info = (le_advertising_info *) (meta->data + 1);
		char name[30];
		memset(name, 0, sizeof(name));
		ba2str(&info->bdaddr, addr);
		eir_parse_name(info->data, info->length, name, sizeof(name) - 1);
		printf("%s %s\n", addr, name);
		
	    
	sleep(1);
	}
	
done:
	setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
	
}

int main()
{
	int err, opt, dd;
	uint8_t bdaddr_type = LE_PUBLIC_ADDRESS;
	bdaddr_t bdaddr;
	
	//LE Set Scan Parameters Command. Core Specs p 1261. Vol. 2 Part E. HCI Func Specs
	uint8_t own_type = 0x00; // lib/hci.h (public 0x00 random 0x01)
	uint8_t scan_type = 0x01; //0:Passive 1:Active
	uint8_t filter_policy = 0x00; //p 1267. 0: tout accepter, 1:WL only, 2:Neg-Filter les directed adv non ciblés vers nous, 3:filtre 1+2 (?)
	uint16_t interval = htobs(0x0010); //10=default, 10ms
	uint16_t window = htobs(0x0010); //durée du scan. doit être <= à interval. 10=default
	
	//LE Set Scan Enable Command. Core Specs p 1264. Vol. 2 Part E. HCI Func Specs
	uint8_t filter_dup = 0x01; //1-filter duplicates enabled 0-Disabled
		
	//Ouverture d'un socket file descriptor vers le controller. Hard Codé "0" car c'est toujours hci0 chez moi.
	dd = hci_open_dev(0); // lib/hci_lib.h
	fprintf(stderr, "La valeur dd=%i\n", dd);
	
	/**Whitelist**/
	str2ba("30:AE:A4:04:C3:5A", &bdaddr);
	err = hci_le_add_white_list(dd, &bdaddr, bdaddr_type, 1000);
	fprintf(stderr, "Retour de add_white_list = %i\n", err);
	
	/**Set Scan Params**/
	err = hci_le_set_scan_parameters(dd, scan_type, interval, window, own_type, filter_policy, 10000); //dernier arg timeout pour hci_send_req()
	fprintf(stderr, "Retour de le_set_scan_parameters = %i\n", err);
	
	/**Scan Enable**/
	err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 10000); //arg2: 1=enable, dernier arg timeout pour hci_send_req()
	fprintf(stderr, "Retour de set_scan_enable 0x01 (enable) = %i\n", err);
	
	run_lescan(dd);
	
	/**Scan Disable**/
	err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 10000);
	fprintf(stderr, "Retour de set_scan_enable 0x00 (disable) = %i\n", err);

	hci_close_dev(dd);
	return 0;
}
