#include <stdint.h>
#include <stdio.h>

#include "os/storage/cfs/cfs.h"
#include "project-conf.h"
#include "stdio.h"
#include "contiki.h"
#include "stdio.h"
#include "stdlib.h"
#include "net/netstack.h"
#include "net/routing/routing.h"
#include "net/ipv6/simple-udp.h"
#include "packetbuf.h"
#include "dev/serial-line.h"
#include "dev/leds.h"
#include "sys/log.h"
#include "cpu/arm/common/SD-card/sdcard.h"
#include "arch/cpu/arm/common/SD-card/sdcard.h"
#include "lib/sensors.h"
#include "dev/button-sensor.h"
#include "dev/button-hal.h"

#ifndef NEED_FORMATTING
#define NEED_FORMATTING 0
#endif

#define MAX_FILENAME_LEN 64
#define MAX_READ_SIZE 512

#define LOG_MODULE "Client"
#define LOG_LEVEL LOG_LEVEL_INFO

#define RPL_CONF_DAG_ROOT_RANK 1

static struct simple_udp_connection udp_conn;
static process_event_t message_received_event;

PROCESS(end_process, "End Node Process");
PROCESS(file_transfer, "File Transfer Process");

AUTOSTART_PROCESSES(&end_process);

static void generate_message(int32_t message_id, size_t payload_size, char payload_value, char *buffer) {
	memcpy(buffer, &message_id, sizeof(message_id));
	memset(buffer + sizeof(message_id), payload_value, payload_size);
}


static int32_t extract_message_id(const char *data) {
	int32_t message_id;
	memcpy(&message_id, data, sizeof(message_id));
	return message_id;
}


// udp callback function
// responsible for logging the messages received to a file in the flash memory 
// of the node, as well as saving some extra values
static void udp_rx_callback(
	struct simple_udp_connection *conn,
	const uip_ipaddr_t *sender_addr,
	uint16_t sender_port,
	const uip_ipaddr_t *receiver_addr,
	uint16_t receiver_port,
	const uint8_t *data,
	uint16_t datalen
) 
{
	int16_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
	int16_t lqi = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);
	long message_id = extract_message_id((char *) data);

	clock_time_t curr_time_in_ticks = clock_time();
	unsigned long curr_time_in_seconds = (unsigned long)(curr_time_in_ticks / CLOCK_SECOND);

	char buffer[128];  // allocate a buffer, ensure it is large enough
	sprintf(buffer, "%ld, %lu, %d, %d\n", message_id, curr_time_in_seconds, rssi, lqi);

	LOG_INFO("%s\n", buffer);

	char filename[100];
	sprintf(filename, "%d-%d-%d-%d.txt", INTERVAL_BETWEEN_MESSAGES_SECONDS, MESSAGE_SIZE, RADIO_TX_POWER, RADIO_CHANNEL);
	
	static int file;
	file = cfs_open(filename, CFS_WRITE | CFS_APPEND);

	cfs_write(file, (void*) buffer, strlen(buffer));
	cfs_close(file);

	process_post(&end_process, message_received_event, NULL);
}


// end process, sends periodical messages to a relay node, that relays 
// them to the other end node
PROCESS_THREAD(end_process, ev, data)
{
  static struct etimer timer;
	static char str[32];
	static long curr_message = 0;

	// the ip_addr of the relay node - the DAG root.
	uip_ipaddr_t relay_ipaddr; 

  PROCESS_BEGIN();
		
		// format the node if needed
		#if NEED_FORMATTING
			cfs_coffee_format();
		#endif

		// sends message to the server
		simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);
		etimer_set(&timer, CLOCK_SECOND * INTERVAL_BETWEEN_MESSAGES_SECONDS);	
		apply_config();

		// main process loop, sends messags periodically
		while (1) {
			PROCESS_WAIT_EVENT();

			if (ev == message_received_event) etimer_reset(&timer);

			if (ev == button_hal_press_event) {
				process_start(&file_transfer, NULL);
				PROCESS_EXIT();
			}

			if (etimer_expired(&timer)) {
				if (NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&relay_ipaddr)) {
					char payload_value = 'A';
					char buffer[sizeof(curr_message) + MESSAGE_SIZE];
					generate_message(curr_message, sizeof(buffer), payload_value, buffer);
					simple_udp_sendto(&udp_conn, str, strlen(str), &relay_ipaddr);
					curr_message++;
				} else {
					LOG_INFO("Not reachable yet\n");
				}
				etimer_reset(&timer);
			}
		}

  PROCESS_END();
}



// file transfer process,
// responsible for transfering all the files from the flash memory of the node
// into another device using the serial interface
PROCESS_THREAD(file_transfer, ev, data)
{
	PROCESS_BEGIN();
	
		// read and send a log file...
		struct cfs_dir dir;
		struct cfs_dirent dirent;
		int fd;

		// Open the current directory
		if (cfs_opendir(&dir, "/") == -1) {
			printf("Failed to open directory\n");
			PROCESS_EXIT();
		}

		char buffer[MAX_READ_SIZE];

		// Loop over all files in the directory
		while (cfs_readdir(&dir, &dirent) != -1) {
			// Print the filename
			printf("FILENAME:%s\n", dirent.name);
			printf("FILESTART:\n");

			// Open the file
			fd = cfs_open(dirent.name, CFS_READ);
			if (fd != -1) {
				// Read the file and print its contents
				int n;
				while ((n = cfs_read(fd, buffer, MAX_READ_SIZE)) > 0) {
					buffer[n] = '\0';  // Ensure null termination
					printf("%s", buffer);
				}
				printf("\nFILEEND:\n");
				cfs_close(fd);
			} else {
				printf("Failed to open file %s\n", dirent.name);
			}
		}

		printf("Finished.");

		cfs_closedir(&dir);

		PROCESS_EXIT();

	PROCESS_END();
}
