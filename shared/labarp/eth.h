#ifndef ETH_H
#define ETH_H

#include <arpa/inet.h>
#include <linux/if_packet.h>   // Para sockaddr_ll y afines
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>            // Para operaciones de interfaz de red
#include <netinet/ether.h>     // Para direcciones Ethernet
#include <unistd.h>

/* Longitud del payload */
#define ETHER_TYPE   60

/* Con 2000 bytes son suficientes para la trama, ya que va de 64 a 1518 */
#ifndef BUF_SIZ
#define BUF_SIZ      2000    
#endif

#ifndef MAX_NAME_LEN
#define MAX_NAME_LEN 50
#endif

/* Tipos de tramas */
#define TYPE_REQUEST  0x01
#define TYPE_RESPONSE 0x02

/* Tipo de dato sin signo */
typedef unsigned char byte;

/* Función para convertir direcciones MAC de texto a bytes */
void ConvierteMAC (char *Mac, char *Org)
{
    int i, j, Aux, Acu;
    for (i = 0, j = 0, Acu = 0; i < 12; i++) {
        if ((Org[i] > 47) && (Org[i] < 58)) Aux = Org[i] - 48;
        if ((Org[i] > 64) && (Org[i] < 97)) Aux = Org[i] - 55;
        if (Org[i] > 96) Aux = Org[i] - 87;
        if ((i % 2) == 0) Acu = Aux * 16;
        else {
            Mac[j] = Acu + Aux;  
            j++;
        }
    }
}

#endif /* ETH_H */
