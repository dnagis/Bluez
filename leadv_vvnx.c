/** gcc -o leadv leadv_vvnx.c -I/initrd/mnt/dev_save/packages/bluez-5.47 -lbluetooth 
 * 
 * voir lescan en premier. Celui ci c'est pour advertiser pour faire du scan rsp si possible
 * et passer des paramètres vers l'esp32
 * 
 * hci_le_set_advertise_enable() définie dans lib/hci_lib.h -- 
 * 		Correspond à:
 * 			Core specs > Core Sys Pkg [BR/EDR Ctrller Vol] Spec Vol. 2 > Part E - HCI func specs > 7-HCI cmds + evts > LE ctrller commands > p 1259 LE Set Advertising Enable Command
 * 
 * btmon et hcitool lescan sur un autre bluez (un zero en ssh par exemple...)
 * 
 * **/

#include <stdio.h>
#include <unistd.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "lib/hci_lib.h"


/**SET ADVERTTISING DATA
 * 
 * la commande n'existe pas dans la librairie bluez, je suis donc obligé de faire ma request à la mano
 * Core Sys Pkg [BR/EDR Ctrller Vol] Spec Vol. 2 > E-HCI func specs > 7-HCI cmds&evts > LE ctrlr cmds > p 1256 LE Set Advertising Data Command
 * 	Pour faire la request cf lib/hci.c (par exemple li.2978 hci_le_set_advertise_enable)
 * 
 * cp = Command Parameter
 * rp = Return Parameter
 * 
 * **/
int vvnx_hci_le_set_adv_data(int dd)
{
	struct hci_request rq;
	le_set_advertising_data_cp adv_cp;
	uint8_t status, taille;
	taille = 8;
	uint8_t adv_data_vvnx[31] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0e};
	
	memset(&adv_cp, 0, sizeof(adv_cp));
	adv_cp.length = taille;
	memcpy(adv_cp.data, adv_data_vvnx, taille); 
	
	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_ADVERTISING_DATA;
	rq.cparam = &adv_cp;
	rq.clen = LE_SET_ADVERTISING_DATA_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;
	
	if (hci_send_req(dd, &rq, 10000) < 0) //dernier argument = timeout
		return -1;
		
	if (status) {
	fprintf(stderr, "set_adv_data -> On a du return parameter va falloir lire status\n");
	return -1;
	}

	return 0;
	
}

/**SET ADVERTISING PARAMETERS
 * 
 * idem: la commande n'existe pas dans la librairie bluez
 * ... 7-HCI cmds&evts > LE ctrlr cmds > p 1251 LE Set Advertising Data Command
 * 	adapté à partir de: lib/hci.c li.2978 hci_le_set_scan_parameters
 * 
 ***/
int vvnx_hci_le_set_adv_parameters(int dd)
{
	struct hci_request rq;
	le_set_advertising_parameters_cp param_cp;
	uint8_t status;
	uint8_t bdaddr_type = LE_PUBLIC_ADDRESS;
	bdaddr_t bdaddr;
	
	memset(&param_cp, 0, sizeof(param_cp));
	param_cp.min_interval = htobs(0x0800);
	param_cp.max_interval = htobs(0x0800);
	param_cp.advtype = 0x00;
	param_cp.own_bdaddr_type = 0x00;
	param_cp.direct_bdaddr_type = 0x00; //Peer Address Type
	//direct_bdaddr (bluez) - peer_address (core specs)
	str2ba("00:00:00:00:00:00", &bdaddr); //adresse "any" ???
	bacpy(&param_cp.direct_bdaddr, &bdaddr); 
	param_cp.chan_map = 7; //00000111b - 1 octet - 00000111 to int --> 7	
	param_cp.filter = 0x03; //0x03: whitelist only
	
	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_ADVERTISING_PARAMETERS;
	rq.cparam = &param_cp;
	rq.clen = LE_SET_ADVERTISING_PARAMETERS_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(dd, &rq, 10000) < 0)
		return -1;

	if (status) {
		fprintf(stderr, "set_adv_parameters -> On a du return parameter avec status =  0x%02x\n", status); //Vol.2 Part D Error codes p643
		return -1;
	}	
	
	return 0;
}


int main()
{
	int err, dd;
	uint8_t bdaddr_type = LE_PUBLIC_ADDRESS;
	bdaddr_t bdaddr;
		
	dd = hci_open_dev(0);
	
	/**Whitelist**/
	str2ba("B8:27:EB:A4:33:13", &bdaddr); //adresse du zero
	err = hci_le_add_white_list(dd, &bdaddr, bdaddr_type, 1000);
	fprintf(stderr, "Retour de add_white_list = %i\n", err);
		
	/**Ma custom Set Adv Param définie plus haut**/
	err = vvnx_hci_le_set_adv_parameters(dd);
	fprintf(stderr, "Retour de set_adv_parameters = %i\n", err);
	
	/**Ma custom Set Adv Data définie plus haut**/
	err = vvnx_hci_le_set_adv_data(dd);
	fprintf(stderr, "Retour de set_adv_data = %i\n", err);
	
	/**Adv Enable**/
	err = hci_le_set_advertise_enable(dd, 0x01, 10000); //core specs p 1259
	fprintf(stderr, "Retour de set_advertise_enable 0x01 (enable) = %i\n", err); //tu auras du -1 si l'adv est déjà enabled, mais les AdvData seront updatées anyway
	

	
	hci_close_dev(dd);
	
	
	
	
	return 0;
}
