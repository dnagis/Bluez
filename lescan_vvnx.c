/** gcc -o lescan lescan_vvnx.c -I/initrd/mnt/dev_save/packages/bluez-5.45 -lbluetooth -lpthread -lsqlite3
 * 
 * rpi:
 * export PATH=/initrd/mnt/dev_save/cross/bin:$PATH
 * arm-linux-gnueabihf-gcc -o lescan_rpi lescan_vvnx.c -I/initrd/mnt/dev_save/packages/bluez-5.45 -lbluetooth -lpthread -lsqlite3
 * 
 * sur github, repo Bluez-BLE
 * 
 * Gestion de plusieurs capteurs: arrêt quand tout lu, repose sur la whitelist et le filter duplicates qui n'autorise qu'un retour par bdaddr
 * 
 * de l'autre côté, esp32, "esp32_ble_pure" sur github, et sur le NUC: esp32_vince/ble_pure
 * 
 * Basé sur tools/hcitool.c -- li 2504 surtout
 * 
 * 
 * Attention si l'appli plante on ne passe pas par hci_le_set_scan_enable(dd, 0x00, filter_dup, 10000); 
 * 	et donc le enable suivant ne marchera pas!
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
#include <pthread.h>
#include <time.h>
#include <sqlite3.h>

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

int timeout = 20; //auto shutdown timeout, en secondes
int ret_dsbl;
int dd;
int nb_capteurs_lu = 0;
int nb_total_capteurs = 2;


/**Pour ne pas scanner indéfiniment si n capteur(s) pas lu**/
void *thread_timeout(void *arg)
{
    fprintf(stderr, "Lancement du timeout on va laisser %i secondes au scan avant shutdown\n", timeout);
    sleep(timeout);
    //maintenance obligatoire, sinon controller pas content et fail à toutes commandes hci ultérieures
	ret_dsbl = hci_le_set_scan_enable(dd, 0x00, 0x01, 2000); //attention des fois pas de retour et bloque ici: à tester +++
	fprintf(stderr, "Retour de set_scan_enable 0x00 (disable) = %i\n", ret_dsbl); //j'ai parfois -1 mais c'est pas grave, quand répétition trop rapide?	
	hci_close_dev(dd);
	
	exit(0);
}



void sigint_handler(int sig)
{
	signal_received = sig;
}

/**Parsage de ce qu'il y a dans la le_advertising_info, basé sur eir_parse_name() de hcitool
*je n'ai pas besoin de me prendre la tête avec le format défini par les core specs pour la data (31 bytes avec des
*contraintes de format: bytes de longueur etc...
* 
* 
* 
**/
static float recup_temp(uint8_t *eir)
{
uint8_t intPart;
uint8_t decPart;
memcpy(&intPart, &eir[5], 1);
memcpy(&decPart, &eir[6], 1);
float temperature = intPart + (0.01 * decPart);
//fprintf(stderr, "intpart = %i, decPart = %i, temperature = %.2f \n", intPart, decPart, temperature);
return temperature;
}

void write_bdd(float temp, char *mac)
{
	//CREATE TABLE logtemp (epoch INT, mac TEXT, temp INT, sent INT default 0);
	//1549355401|1601|102573|1
	//sqlite3 /var/log/homedata.db "select datetime(epoch, 'unixepoch','localtime'), mac, temp from logtemp;"
	char time_as_string[20]; //pour réceptionner la conversion d'un int en string
	char temp_as_string[20];
	//int temp_int = (int)(temp * 100);
	int rc;
	sqlite3 *db;
	
	sprintf(time_as_string, "%i", (unsigned long)time(NULL)); //sprintf: printf dans une string au format désiré printf - like
	sprintf(temp_as_string, "%i", (int)(temp * 100));

	//printf("on va écrire dans bdd en sqlite mac = %s temp= %s epoch = %s\n", mac, temp_as_string, time_as_string);

	//"insert into logtemp values(time_as_string, mac, temp_int);"
	
	/**attention à la taille de char stmt[]: si vide segfault au runtime, et si taille trop petite sqlite3_exec retourne -1 sans explication
	(je suppose parce qu'avec le pointeur tu vois la totalité inscrite en mémoire, quand tu printf %s, stmt, alors que la variable stmt passée à
	sqlite3_exec, elle, a une longueur qui si trop courte tronque la fin de la commande.**/
	char stmt[80] = "";
	char debut_stmt[] = "insert into logtemp values(";
	strcpy(stmt, debut_stmt);
	strcat(stmt, time_as_string);
	
	strcat(stmt, ", \'");	
	//strcat(stmt, "abcdef");
	strcat(stmt, mac);
	strcat(stmt, "\', ");
	strcat(stmt, temp_as_string);	
	char fin_stmt[] = ", 0);";
	strcat(stmt, fin_stmt);	
	
	//printf("commande en string = %s\n", stmt);		
	
	rc = sqlite3_open("/var/log/homedata.db", &db);
	rc = sqlite3_exec(db, stmt, NULL, 0, NULL); 
	
	//printf("retour commande sqlite = %i\n", rc);		
	
	sqlite3_close(db);
	
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
	//fprintf(stderr, "On est dans run_lescan()...\n");
	
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
	    
	    if (meta->subevent != 0x02) //#define EVT_LE_ADVERTISING_REPORT	0x02 --> lib/hci.h. Core Specs p1193 LE Advertising Report Event
			goto done; //Attention ça fait sortir de la boucle, y aura pas de deuxième chance!
			
		info = (le_advertising_info *) (meta->data + 1);
			
		if (info->evt_type == 0x00) {			//Filtrer par Event Type (SCAN_RSP ou ADV_IND...). btmon... Core Specs p. 1193 LE Advertising Report Event
			//si je filtre par evt_type == 0x04 ça marche sur le NUC mais sur le rpi j'ai de la data sticky, c'est à dire qu'au deuxième capteur j'ai les mêmes infos.
			nb_capteurs_lu ++; //wl only et filter duplicates: on passe une fois pour chaque capteur
			float temp;
			memset(&temp, 0, sizeof(temp));
			ba2str(&info->bdaddr, addr);
			temp = recup_temp(info->data);
			printf("bdaddr = %s et retour de parse_vvnx: %.2f\n", addr, temp);	
			write_bdd(temp, addr);
			if ( nb_capteurs_lu == nb_total_capteurs ) goto done; //c'est on a tout ciao
		}

	sleep(1);
	}
	
done:
	setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
	
}

int main()
{
	int err, opt;
	uint8_t bdaddr_type = LE_PUBLIC_ADDRESS;
	bdaddr_t bdaddr;
	pthread_t thread_to;
	
	//LE Set Scan Parameters Command. Core Specs p 1261. Vol. 2 Part E. HCI Func Specs
	uint8_t own_type = 0x00; // lib/hci.h (public 0x00 random 0x01)
	uint8_t scan_type = 0x01; //0:Passive 1:Active
	uint8_t filter_policy = 0x01; //p 1267. 0: tout accepter, 1:WL only, 2:Neg-Filter les directed adv non ciblés vers nous, 3:filtre 1+2 (?)
	uint16_t interval = htobs(0x0010); //10=default, 10ms
	uint16_t window = htobs(0x0010); //durée du scan. doit être <= à interval. 10=default
	//LE Set Scan Enable Command. Core Specs p 1264. Vol. 2 Part E. HCI Func Specs
	uint8_t filter_dup = 0x01; //1-filter duplicates enabled 0-Disabled
		
	//Ouverture d'un socket file descriptor vers le controller. Hard Codé "0" car c'est toujours hci0 chez moi.
	dd = hci_open_dev(0); // lib/hci_lib.h
	fprintf(stderr, "La valeur dd=%i\n", dd);
	
	/**Whitelist -- voir scan param filter_policy -- je cleare la whitelist à chaque passage (i.e. hcitool lewlclr) pour ne pas avoir d'embrouille**/
	err = hci_le_clear_white_list(dd, 10000);
	fprintf(stderr, "Retour de clear_white_list = %i\n", err);
	str2ba("30:AE:A4:45:C8:86", &bdaddr); 
	err = hci_le_add_white_list(dd, &bdaddr, bdaddr_type, 1000); //i.e. hcitool lewladd 30:AE:A4:45:C8:86
	fprintf(stderr, "Retour de add_white_list = %i\n", err);
	str2ba("30:AE:A4:04:C3:5A", &bdaddr);
	err = hci_le_add_white_list(dd, &bdaddr, bdaddr_type, 1000); //i.e. hcitool lewladd 30:AE:A4:04:C3:5A
	fprintf(stderr, "Retour de add_white_list = %i\n", err);
	//str2ba("24:0A:C4:00:1F:78", &bdaddr);
	
	
	
	/**Set Scan Params**/
	err = hci_le_set_scan_parameters(dd, scan_type, interval, window, own_type, filter_policy, 10000); //dernier arg timeout pour hci_send_req()
	fprintf(stderr, "Retour de le_set_scan_parameters = %i\n", err);
	
	/**Scan Enable**/
	err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 10000); //arg2: 1=enable, dernier arg timeout pour hci_send_req()
	fprintf(stderr, "Retour de set_scan_enable 0x01 (enable) = %i\n", err);
	
	
	//auto shutdown au bout de n secondes via le pote thread
	if(pthread_create(&thread_to, NULL, thread_timeout, NULL) == -1) {
	perror("pthread_create");
	return EXIT_FAILURE;
    }
	
	
	run_lescan(dd);
	
	/**Scan Disable**/
	err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 10000);
	fprintf(stderr, "Retour de set_scan_enable 0x00 (disable) = %i\n", err);

	hci_close_dev(dd);
	return 0;
}
