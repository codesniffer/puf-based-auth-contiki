/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "net/ip/uip.h"
#include "net/ipv6/uip-ds6.h"
#include "net/ip/uip-udp-packet.h"
#include "net/rpl/rpl.h"
#include "dev/serial-line.h"

#if CONTIKI_TARGET_Z1
#include "dev/uart0.h"
#else
#include "dev/uart1.h"
#endif
#include "collect-common.h"
#include "collect-view.h"

#include "sha256.h"
#include <stdio.h>
#include <string.h>




#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define UDP_CLIENT_PORT 8775
#define UDP_SERVER_PORT 5688


#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

static struct uip_udp_conn *client_conn;
static uip_ipaddr_t server_ipaddr;

#define MAX_CERT_FLIGHT 18
static uint8_t cert_flight_count = 0;

static unsigned long rstart_time = 0; 
static unsigned long rend_time = 0;
static unsigned long relasped_time = 0;

static unsigned long cstart_time = 0;
static unsigned long cend_time = 0;
static unsigned long celasped_time = 0;

static unsigned long cpu_energy_start, cpu_energy_stop,lpm_energy_start, lpm_energy_stop, transmit_energy_start, transmit_energy_stop, listen_energy_start, listen_energy_stop;




/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client process");
AUTOSTART_PROCESSES(&udp_client_process, &collect_common_process);
/*---------------------------------------------------------------------------*/
void
collect_common_set_sink(void)
{
  /* A udp client can never become sink */
}
/*---------------------------------------------------------------------------*/

void
collect_common_net_print(void)
{
  rpl_dag_t *dag;
  uip_ds6_route_t *r;

  /* Let's suppose we have only one instance */
  dag = rpl_get_any_dag();
  if(dag->preferred_parent != NULL) {
    PRINTF("Preferred parent: ");
    PRINT6ADDR(rpl_get_parent_ipaddr(dag->preferred_parent));
    PRINTF("\n");
  }
  for(r = uip_ds6_route_head();
      r != NULL;
      r = uip_ds6_route_next(r)) {
    PRINT6ADDR(&r->ipaddr);
  }
  PRINTF("---\n");
}
/*---------------------------------------------------------------------------*/
void
energy_tracking_start (void) 
{
  /*
  // might need to enable before reading energy values
  ENERGETST_ON(ENERGEST_TYPE_CPU);
  ENERGETST_ON(ENERGEST_TYPE_LPM);
  ENERGETST_ON(ENERGEST_TYPE_TRANSMIT);
  ENERGETST_ON(ENERGEST_TYPE_LISTEN);
 */ 

  energest_flush();
  cpu_energy_start = energest_type_time(ENERGEST_TYPE_CPU);
  lpm_energy_start = energest_type_time(ENERGEST_TYPE_LPM);
  transmit_energy_start = energest_type_time(ENERGEST_TYPE_TRANSMIT);
  listen_energy_start = energest_type_time(ENERGEST_TYPE_LISTEN);
}
/*---------------------------------------------------------------------------*/
void
energy_tracking_stop (void) 
{
  unsigned long energy_consumed;
  int cpu_current  = 2; // 1.2mA
  int lpm_current = 1; 
  int transmit_current = 23;
  int listen_current = 21;
  int volt = 4;

  energest_flush();
  cpu_energy_stop = energest_type_time(ENERGEST_TYPE_CPU) - cpu_energy_start;
  lpm_energy_stop = energest_type_time(ENERGEST_TYPE_LPM) - lpm_energy_start;
  transmit_energy_stop = energest_type_time(ENERGEST_TYPE_TRANSMIT) - transmit_energy_start;
  listen_energy_stop = energest_type_time(ENERGEST_TYPE_LISTEN) - transmit_energy_start;

  energy_consumed =  (cpu_current* cpu_energy_stop);
  energy_consumed = energy_consumed + (lpm_current * lpm_energy_stop); 
  energy_consumed = energy_consumed + (transmit_current * transmit_energy_stop);
  energy_consumed = energy_consumed + (listen_current * listen_energy_stop);
  energy_consumed = energy_consumed* volt;
  energy_consumed = energy_consumed / RTIMER_SECOND;
  printf("energy consumption [%lu] mJ\n", energy_consumed); 
  
}
/*---------------------------------------------------------------------------*/
void 
time_tracking_start (void) 
{
  rstart_time = RTIMER_NOW();
  cstart_time = clock_time();
  printf("first packet at rtime [%lu]  ctime [%lu]\n", rstart_time, cstart_time);
}

void 
time_tracking_stop (void) 
{
  rend_time = RTIMER_NOW();
  relasped_time = rend_time - rstart_time;

  cend_time = clock_time();
  celasped_time = cend_time - cstart_time;

  printf("relasped_time [%lu] ticks, rlatency [%lu] sec\n", relasped_time, relasped_time/RTIMER_SECOND ); // RTIMER_ARCH_SECOND
  printf("celasped_time [%lu] ticks, clatency [%lu] sec\n", celasped_time, celasped_time/CLOCK_SECOND );
}
/*---------------------------------------------------------------------------*/
void 
hash_generation(void)
{
  uint8_t buffer[1024]; /* certificate buffer*/
  int i =0;
  int hash_output =1;
  //BYTE text2[] = {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
 // BYTE hash2[SHA256_BLOCK_SIZE] = {0x24,0x8d,0x6a,0x61,0xd2,0x06,0x38,0xb8,0xe5,0xc0,0x26,0x93,0x0c,0x3e,0x60,0x39,
                                  // 0xa3,0x3c,0xe4,0x59,0x64,0xff,0x21,0x67,0xf6,0xec,0xed,0xd4,0x19,0xdb,0x06,0xc1};
  BYTE buf[SHA256_BLOCK_SIZE];
  SHA256_CTX ctx;
  int idx;
  int pass = 1;

  /*sha256_init(&ctx);
  sha256_update(&ctx, text2, strlen(text2));
  sha256_final(&ctx, buf);
  pass = pass && !memcmp(hash2, buf, SHA256_BLOCK_SIZE);*/

  printf("SHA test resutl: [%lu]", pass);


  memset(buffer, 'A', 1024);
  for(i=0; i<1024; i++) {
    hash_output = hash_output ^ buffer[i];
  }


}
/*---------------------------------------------------------------------------*/
void 
encryption_decryption(void)
{
  hash_generation();

}
/*---------------------------------------------------------------------------*/
void 
singnature_varification(void)
{
  hash_generation();
  encryption_decryption();
}
/*---------------------------------------------------------------------------*/
void 
key_generation_exponential(void)
{
  unsigned long a =65300 ;
  unsigned long b=65300;
  unsigned long key;
  unsigned long i, j;
  for (i =0; i < a; i++) {
    for (j =0; j< b; j++) {
      key = key + a;
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  uint8_t *appdata;
  linkaddr_t sender;
  uint8_t seqno;
  uint8_t hops;

  if(uip_newdata()) {
    appdata = (uint8_t *)uip_appdata;
    sender.u8[0] = UIP_IP_BUF->srcipaddr.u8[15];
    sender.u8[1] = UIP_IP_BUF->srcipaddr.u8[14];
    seqno = *appdata;
    hops = uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl + 1;
    //collect_common_recv(&sender, seqno, hops, appdata + 2, uip_datalen() - 2-128); // 128 is the size of the payload

    cert_flight_count = cert_flight_count+ 1 ;
    if(cert_flight_count == MAX_CERT_FLIGHT) {
      cert_flight_count =0;
      time_tracking_stop();
      energy_tracking_stop();
      clock_wait(CLOCK_SECOND * 120) ; /*wait for 120s second and then go ahead*/
      collect_common_send();
    } else {
      if (cert_flight_count == 1) { // first packet
        time_tracking_start();
        energy_tracking_start();
        singnature_varification();
        key_generation_exponential();
        hash_generation();
      }
      collect_common_send();
    }
  } else if(uip_rexmit()) { // packet drop need to retransmit
    collect_common_send();
  }
}
/*---------------------------------------------------------------------------*/
void
collect_common_send(void)
{
  static uint8_t seqno;
  struct {
    uint8_t seqno;
    uint8_t for_alignment;
    struct collect_view_data_msg msg;
    char payload [256];
  } msg;
  uint16_t packet_size;

  /* struct collect_neighbor *n; */
  uint16_t parent_etx;
  uint16_t rtmetric;
  uint16_t num_neighbors;
  uint16_t beacon_interval;
  rpl_parent_t *preferred_parent;
  linkaddr_t parent;
  rpl_dag_t *dag;

  if(client_conn == NULL) {
    /* Not setup yet */
    return;
  }
  memset(&msg, 0, sizeof(msg));
  seqno++;
  if(seqno == 0) {
    /* Wrap to 128 to identify restarts */
    seqno = 128;
  }
  msg.seqno = seqno;

  linkaddr_copy(&parent, &linkaddr_null);
  parent_etx = 0;

  /* Let's suppose we have only one instance */
  dag = rpl_get_any_dag();
  if(dag != NULL) {
    preferred_parent = dag->preferred_parent;
    if(preferred_parent != NULL) {
      uip_ds6_nbr_t *nbr;
      nbr = uip_ds6_nbr_lookup(rpl_get_parent_ipaddr(preferred_parent));
      if(nbr != NULL) {
        /* Use parts of the IPv6 address as the parent address, in reversed byte order. */
        parent.u8[LINKADDR_SIZE - 1] = nbr->ipaddr.u8[sizeof(uip_ipaddr_t) - 2];
        parent.u8[LINKADDR_SIZE - 2] = nbr->ipaddr.u8[sizeof(uip_ipaddr_t) - 1];
        parent_etx = rpl_get_parent_rank((uip_lladdr_t *) uip_ds6_nbr_get_ll(nbr)) / 2;
      }
    }
    rtmetric = dag->rank;
    beacon_interval = (uint16_t) ((2L << dag->instance->dio_intcurrent) / 1000);
    num_neighbors = uip_ds6_nbr_num();
  } else {
    rtmetric = 0;
    beacon_interval = 0;
    num_neighbors = 0;
  }

  /* packet size without payload*/
  packet_size = sizeof(msg) - sizeof(msg.payload);


  memset(msg.payload, 'A', 128);
  msg.payload[127] = 0; 
  packet_size = packet_size  + 128;
 
  /* num_neighbors = collect_neighbor_list_num(&tc.neighbor_list); */
  collect_view_construct_message(&msg.msg, &parent,parent_etx, rtmetric, num_neighbors, beacon_interval);
  //uip_udp_packet_sendto(client_conn, &msg, sizeof(msg), &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));
  uip_udp_packet_sendto(client_conn, &msg,packet_size, &server_ipaddr, UIP_HTONS(UDP_SERVER_PORT));

 

  //PRINTF("Service client  -> service provider IP: ");
 // PRINT6ADDR(&server_ipaddr);
 // PRINTF("  Port: %u", UIP_HTONS(UDP_SERVER_PORT));
 // PRINTF("\n");
}
/*---------------------------------------------------------------------------*/
void
collect_common_net_init(void)
{
#if CONTIKI_TARGET_Z1
  uart0_set_input(serial_line_input_byte);
#else
  uart1_set_input(serial_line_input_byte);
#endif
  serial_line_init();

  PRINTF("I am service-client!\n");

}
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Client IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE) {
        uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
static void
set_global_address(void)
{
  uip_ipaddr_t ipaddr;

  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  /* set server address */
  uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 1);

}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  PROCESS_BEGIN();

  PROCESS_PAUSE();

  set_global_address();

  PRINTF("UDP client process started\n");

  print_local_addresses();

  /* new connection with remote host */
  client_conn = udp_new(NULL, UIP_HTONS(UDP_SERVER_PORT), NULL);
  udp_bind(client_conn, UIP_HTONS(UDP_CLIENT_PORT));

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

/*

#define   CLOCK_SECOND //A second, measured in system clock time. 

clock_time_t  clock_time (void) //Get the current clock time. 

unsigned long  clock_seconds (void) //Get the current value of the platform seconds. 

rtimer_clock_t  RTIMER_NOW()

RTIMER_ARCH_SECOND

RTIMER_SECOND

*/
